/**
 * captive_dns.c — DNS server tối giản cho SoftAP provisioning. Xem captive_dns.h.
 *
 * Chỉ làm đúng một việc: mọi truy vấn A (và ANY) đều trả IP của AP.
 * Truy vấn loại khác (AAAA, HTTPS/65, SRV…) trả NOERROR + 0 answer — đúng
 * chuẩn và làm điện thoại bỏ IPv6 rồi quay sang IPv4 ngay, không phải chờ
 * timeout.
 */
#include "captive_dns.h"
#include "rapid4p.h"
#include "core/task_profile.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <string.h>

#define TAG_DNS "r4p.dns"

/* RFC 1035: message qua UDP tối đa 512 byte. Cộng chỗ cho answer mình chèn. */
#define DNS_MAX_MSG      512
#define DNS_REPLY_MAX    (DNS_MAX_MSG + 16)
#define DNS_HEADER_LEN   12
#define DNS_PORT         53
#define DNS_TTL_SEC      60

#define DNS_QTYPE_A      1
#define DNS_QTYPE_ANY    255
#define DNS_QCLASS_IN    1

static int s_sock = -1;
static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;
static uint32_t s_answer_ip = 0;   /* network byte order */

/* Bỏ qua QNAME dạng label (len,bytes…,0). Trả offset ngay sau QNAME, hoặc -1
 * nếu méo/tràn. Không hỗ trợ compression pointer trong câu hỏi — query hợp lệ
 * không bao giờ dùng. */
static int dns_skip_qname(const uint8_t *buf, int len, int off) {
    while (off < len) {
        uint8_t l = buf[off];
        if (l == 0) {
            return off + 1;
        }
        if ((l & 0xC0) != 0) {
            return -1;          /* compression pointer — không xử lý */
        }
        off += 1 + l;
    }
    return -1;
}

/* Dựng câu trả lời tại chỗ. Trả độ dài reply, hoặc -1 nếu không trả lời được. */
static int dns_build_reply(const uint8_t *req, int req_len,
                           uint8_t *out, int out_cap) {
    if (req_len < DNS_HEADER_LEN || req_len > DNS_MAX_MSG) {
        return -1;
    }
    /* QR phải = 0 (là câu hỏi), OPCODE phải = 0 (QUERY) */
    if (req[2] & 0x80) return -1;
    if ((req[2] >> 3) & 0x0F) return -1;

    uint16_t qdcount = (uint16_t)((req[4] << 8) | req[5]);
    if (qdcount != 1) return -1;     /* thực tế luôn là 1 */

    int qend = dns_skip_qname(req, req_len, DNS_HEADER_LEN);
    if (qend < 0 || qend + 4 > req_len) return -1;

    uint16_t qtype  = (uint16_t)((req[qend] << 8) | req[qend + 1]);
    uint16_t qclass = (uint16_t)((req[qend + 2] << 8) | req[qend + 3]);
    int question_end = qend + 4;

    bool answer_it = (qclass == DNS_QCLASS_IN) &&
                     (qtype == DNS_QTYPE_A || qtype == DNS_QTYPE_ANY);

    int reply_len = question_end + (answer_it ? 16 : 0);
    if (reply_len > out_cap) return -1;

    /* Header + question copy nguyên */
    memcpy(out, req, question_end);

    /* Flags: QR=1, AA=1, giữ RD của client, RA=1, RCODE=0 */
    out[2] = (uint8_t)(0x84 | (req[2] & 0x01));
    out[3] = 0x80;
    /* ANCOUNT */
    out[6] = 0;
    out[7] = answer_it ? 1 : 0;
    /* NSCOUNT + ARCOUNT = 0. Bỏ luôn OPT record của client (nếu có) — mình
     * không phát EDNS0 nên phải trả arcount=0. */
    out[8] = 0; out[9] = 0;
    out[10] = 0; out[11] = 0;

    if (answer_it) {
        uint8_t *a = out + question_end;
        a[0] = 0xC0; a[1] = 0x0C;                    /* pointer về QNAME ở offset 12 */
        a[2] = 0x00; a[3] = DNS_QTYPE_A;             /* TYPE A */
        a[4] = 0x00; a[5] = DNS_QCLASS_IN;           /* CLASS IN */
        a[6] = 0x00; a[7] = 0x00;
        a[8] = (uint8_t)(DNS_TTL_SEC >> 8);
        a[9] = (uint8_t)(DNS_TTL_SEC & 0xFF);
        a[10] = 0x00; a[11] = 0x04;                  /* RDLENGTH = 4 */
        memcpy(a + 12, &s_answer_ip, 4);             /* RDATA — đã là network order */
    }
    return reply_len;
}

static void captive_dns_task(void *arg) {
    (void)arg;
    /* 15/09/2026 (P4): 1040 B buffer tren stack 3072 + ESP_LOGI/vfprintf + lwip
     * recvfrom = tran stack ("A stack overflow in task captive_dns"), reboot giua
     * provisioning. Buffer sang PSRAM (task song suot provisioning, free khi thoat). */
    uint8_t *req = heap_caps_malloc(DNS_MAX_MSG + DNS_REPLY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!req) req = malloc(DNS_MAX_MSG + DNS_REPLY_MAX);
    if (!req) {
        ESP_LOGE(TAG_DNS, "khong cap duoc buffer DNS");
        s_running = false;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    uint8_t *rep = req + DNS_MAX_MSG;

    ESP_LOGI(TAG_DNS, "Captive DNS listening on :%d", DNS_PORT);

    while (s_running) {
        struct sockaddr_storage src;
        socklen_t slen = sizeof(src);
        int n = recvfrom(s_sock, req, DNS_MAX_MSG, 0,
                         (struct sockaddr *)&src, &slen);
        if (n < 0) {
            if (!s_running) break;
            /* Timeout của SO_RCVTIMEO là đường thoát bình thường khi stop. */
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            ESP_LOGW(TAG_DNS, "recvfrom lỗi errno=%d — dừng DNS", errno);
            break;
        }
        int rl = dns_build_reply(req, n, rep, DNS_REPLY_MAX);
        if (rl < 0) {
            continue;   /* query méo hoặc không phải QUERY — im lặng bỏ qua */
        }
        if (sendto(s_sock, rep, rl, 0, (struct sockaddr *)&src, slen) < 0) {
            ESP_LOGD(TAG_DNS, "sendto lỗi errno=%d", errno);
        }
    }

    ESP_LOGI(TAG_DNS, "Captive DNS stopped (stack HWM=%u B)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    free(req);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t captive_dns_start(esp_ip4_addr_t resolve_to) {
    if (s_running) {
        return ESP_OK;
    }
    s_answer_ip = resolve_to.addr;   /* esp_ip4_addr_t.addr đã là network order */

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG_DNS, "socket lỗi errno=%d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG_DNS, "bind :%d lỗi errno=%d", DNS_PORT, errno);
        close(sock);
        return ESP_FAIL;
    }

    /* Timeout 1s để vòng lặp còn kiểm tra được cờ s_running lúc stop. */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    s_sock = sock;
    s_running = true;

    /* Core IO cùng chỗ WiFi/lwIP; ưu tiên NETWORK như các task mạng khác. */
    /* 3072 -> 3584 (15/09, P4): tran stack ngay dong log dau du buffer da sang heap;
     * log "captive_dns stack HWM" luc thoat de chinh tiep. */
    BaseType_t r = xTaskCreatePinnedToCore(captive_dns_task, "captive_dns", 3584,
                                           NULL, R4P_TASK_PRIO_NETWORK,
                                           &s_task, R4P_TASK_CORE_IO);
    if (r != pdPASS) {
        ESP_LOGE(TAG_DNS, "không tạo được task DNS");
        s_running = false;
        close(s_sock);
        s_sock = -1;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void captive_dns_stop(void) {
    if (!s_running) {
        return;
    }
    s_running = false;
    /* Task tự thoát trong <= 1s nhờ SO_RCVTIMEO rồi tự xoá mình. Chờ nó buông
     * socket trước khi close để không đóng fd đang có người đọc. */
    for (int i = 0; i < 20 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}
