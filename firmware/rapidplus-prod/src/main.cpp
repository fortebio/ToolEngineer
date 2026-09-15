// main.cpp — NỐI DÂY, không chứa logic quyết định.
//
// LUẬT (AGENTS.md §3): file này đọc phần cứng, gọi module thuần, rồi tác động.
// Mọi quyết định an toàn nằm ở src/safety/, validate ở src/config/, OTA ở
// src/ota/ — vì chỉ khi tách ra chúng mới test được trên host trong CI.
//
// Thêm một nhánh `if` quyết định an toàn VÀO ĐÂY là vi phạm: nó sẽ không có
// test và sẽ không ai phát hiện khi nó sai.

#include <Arduino.h>

#include "config_validate.h"
#include "safety_monitor.h"
#include "safety_limits.h"
#include "version.h"

// ─── Chu kỳ ──────────────────────────────────────────────────────────────────
// PID 100 ms và vòng đo 20 s là hợp đồng timing của thiết bị (PRD NFR-07).
// KHÔNG có việc gì được block hai vòng này — kể cả WiFi, cloud, hay log.
static constexpr uint32_t kPidPeriodMs = 100;

static uint32_t s_lastPidMs = 0;

// ─── Cầu nối phần cứng ───────────────────────────────────────────────────────
// TODO(hw): nối vào driver sensor thật (DallasTemperature / VEML6035 / MCP23017
// theo bản instrument-wroom32). Scaffold trả giá trị vô lý CÓ CHỦ ĐÍCH: nếu ai
// đó nạp bản này lên máy thật, hard-limit sẽ cắt gia nhiệt ngay thay vì chạy với
// số đọc giả trông có vẻ hợp lệ.
static safety::Reading readTemperatures() {
    safety::Reading r{};
    r.bottomC      = NAN;
    r.hotlidC      = NAN;
    r.lastUpdateMs = millis();
    return r;
}

// TODO(hw): nối vào driver heater thật.
static void stopAllHeating() {
    // Fail-safe là TẮT, không phải "giữ nguyên duty". Xem docs/SAFETY.md.
    // TODO(hw): hạ tất cả chân điều khiển heater + hot-lid về mức tắt.
}

// TODO: ghi vào log bền (LittleFS / coredump partition). Mỗi lần hard-limit
// kích hoạt phải truy vết được sau sự cố — IEC 62304 cần dấu vết này.
static void logSafetyEvent(safety::Reason reason, const safety::Reading &r) {
    Serial.printf("[SAFETY] stop reason=%s bottom=%.1f hotlid=%.1f\n",
                  safety::reasonToString(reason), r.bottomC, r.hotlidC);
}

// ─── Vòng an toàn ────────────────────────────────────────────────────────────

/// Chạy MỖI chu kỳ PID, TRƯỚC khi tính PID.
/// Trả false nếu không được phép gia nhiệt trong chu kỳ này.
static bool safetyGate() {
    const safety::Reading r = readTemperatures();
    const safety::Verdict v = safety::evaluate(r, millis());

    if (v.mustStop()) {
        stopAllHeating();
        logSafetyEvent(v.reason, r);
        return false;
    }
    if (v.action == safety::Action::Warn) {
        Serial.printf("[SAFETY] warn reason=%s bottom=%.1f hotlid=%.1f\n",
                      safety::reasonToString(v.reason), r.bottomC, r.hotlidC);
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.printf("FBT %s fw=%s pcb=%s\n",
                  FBT_DEVICE_MODEL, FBT_FW_VERSION, FBT_PCB_VERSION);
    Serial.printf("hard-limit bottom=%.1fC hotlid=%.1fC\n",
                  safety::kBottomHardLimitC, safety::kHotlidHardLimitC);

    // Trạng thái khởi động an toàn: heater TẮT cho tới khi đọc được nhiệt độ
    // hợp lệ. Không bao giờ boot vào trạng thái đang gia nhiệt.
    stopAllHeating();

    // TODO: init sensor, heater, display, storage.
    // TODO: sau khi boot thành công phải đánh dấu app hợp lệ, nếu không
    // bootloader sẽ rollback (xem docs/OTA.md):
    //   esp_ota_mark_app_valid_cancel_rollback();

    Serial.printf("free heap sau setup: %u\n", (unsigned)ESP.getFreeHeap());
    s_lastPidMs = millis();
}

void loop() {
    const uint32_t now = millis();

    // Trừ theo unsigned → đúng kể cả khi millis() tràn (~49.7 ngày). Máy PCR
    // chạy liên tục nhiều tuần là chuyện bình thường.
    if (now - s_lastPidMs >= kPidPeriodMs) {
        s_lastPidMs = now;

        if (safetyGate()) {
            // TODO: chạy một bước PID. Nếu safetyGate() trả false thì KHÔNG
            // chạy PID — không chỉ là bỏ qua output, mà không tính luôn, để
            // integral không tích luỹ trong lúc đang lỗi.
        }
    }

    // TODO: vòng đo 20 s, UI, cloud.
    // CloudTask phải nằm ở core riêng và đọc telemetry qua snapshot có khoá
    // (portMUX) — KHÔNG đọc trực tiếp state của vòng đo (PRD D2-02).
}
