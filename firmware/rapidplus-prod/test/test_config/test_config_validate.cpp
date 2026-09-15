// Test validate + clamp config và verify OTA — chạy trên HOST.
//   pio test -e native -f test_config
//
// Hai module này quyết định thứ gì được phép tác động lên phần cứng và thứ gì
// được phép ghi vào flash. Test đỏ = ĐỪNG PHÁT HÀNH.

#include <unity.h>

#include <cstring>
#include <limits>

#include "config_validate.h"
#include "ota_verify.h"

namespace {

int asInt(config::Status s) { return static_cast<int>(s); }
int asInt(ota::Decision d) { return static_cast<int>(d); }

const float kNaN = std::numeric_limits<float>::quiet_NaN();

config::RunConfig goodConfig() {
    config::RunConfig c{};
    c.amplificationTime = 60;
    c.ki                = 1.0f;
    c.slope             = 2.5f;
    c.label[0]          = 'A';
    c.label[1]          = '\0';
    return c;
}

const char *kGoodSha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

ota::Manifest goodManifest() {
    ota::Manifest m{};
    m.version    = "2.4.3";
    m.sha256Hex  = kGoodSha;
    m.pcbVersion = "1.3";
    m.sizeBytes  = 1024 * 1024;
    m.certPinned = true;
    return m;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// ═══ config ══════════════════════════════════════════════════════════════════

void test_config_hop_le_thi_khong_doi(void) {
    config::RunConfig c = goodConfig();
    config::Result r = config::validateAndClamp(c);
    TEST_ASSERT_EQUAL_INT(asInt(config::Status::Ok), asInt(r.status));
    TEST_ASSERT_FALSE(r.wasModified());
    TEST_ASSERT_EQUAL_INT(60, c.amplificationTime);
}

void test_amplification_time_vuot_thi_clamp(void) {
    config::RunConfig c = goodConfig();
    c.amplificationTime = 9999;
    config::Result r = config::validateAndClamp(c);
    TEST_ASSERT_EQUAL_INT(asInt(config::Status::Clamped), asInt(r.status));
    TEST_ASSERT_EQUAL_INT(config::kAmplificationTimeMax, c.amplificationTime);
    TEST_ASSERT_TRUE(r.flags & config::kFlagAmplificationTime);
    // Vẫn được áp — clamp là an toàn vì hard-limit nhiệt là lưới phía sau.
    TEST_ASSERT_TRUE(r.shouldApply());
}

void test_ki_bang_0_thi_clamp_len_toi_thieu(void) {
    config::RunConfig c = goodConfig();
    c.ki = 0.0f;
    config::Result r = config::validateAndClamp(c);
    TEST_ASSERT_EQUAL_INT(asInt(config::Status::Clamped), asInt(r.status));
    TEST_ASSERT_TRUE(c.ki > 0.0f);
}

void test_slope_bang_0_thi_TU_CHOI_chu_khong_clamp(void) {
    // Đây là ranh giới thiết kế quan trọng nhất của module này.
    // slope là mẫu số trong thuật toán. Clamp về 0.0001 sẽ cho ra kết quả đo
    // TRÔNG hợp lệ nhưng vô nghĩa — với thiết bị chẩn đoán, kết quả sai âm thầm
    // nguy hiểm hơn là từ chối chạy.
    config::RunConfig c = goodConfig();
    c.slope = 0.0f;
    config::Result r = config::validateAndClamp(c);
    TEST_ASSERT_EQUAL_INT(asInt(config::Status::Rejected), asInt(r.status));
    TEST_ASSERT_FALSE(r.shouldApply());
}

void test_slope_NaN_thi_tu_choi(void) {
    config::RunConfig c = goodConfig();
    c.slope = kNaN;
    config::Result r = config::validateAndClamp(c);
    TEST_ASSERT_EQUAL_INT(asInt(config::Status::Rejected), asInt(r.status));
}

void test_label_khong_null_terminate_thi_cat(void) {
    // Đây chính là finding D1-02: gói từ cloud không có '\0' làm strlen đọc
    // tràn buffer. Điền đầy buffer để tái hiện.
    config::RunConfig c = goodConfig();
    for (unsigned i = 0; i < sizeof(c.label); i++) {
        c.label[i] = 'X';
    }
    config::Result r = config::validateAndClamp(c);
    TEST_ASSERT_EQUAL_INT(asInt(config::Status::Clamped), asInt(r.status));
    TEST_ASSERT_TRUE(r.flags & config::kFlagLabelTruncated);
    TEST_ASSERT_EQUAL_CHAR('\0', c.label[config::kLabelMaxLen]);
}

void test_clampLabel_cat_chuoi_dai(void) {
    char dst[config::kLabelMaxLen + 1];
    bool truncated = config::clampLabel(dst, sizeof(dst), "CHUOI_RAT_DAI_QUA_NGUONG");
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_EQUAL_UINT(config::kLabelMaxLen, (unsigned)std::strlen(dst));
}

void test_clampLabel_nullptr_khong_crash(void) {
    char dst[config::kLabelMaxLen + 1];
    config::clampLabel(dst, sizeof(dst), nullptr);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

// ═══ OTA ═════════════════════════════════════════════════════════════════════

void test_ota_manifest_hop_le_thi_nhan(void) {
    ota::Manifest m = goodManifest();
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_TRUE(o.accepted());
}

void test_ota_khong_co_cert_ghim_thi_tu_choi(void) {
    // Xét đầu tiên: kênh không xác thực thì mọi trường khác đều có thể do kẻ
    // tấn công viết ra, kể cả hash "hợp lệ".
    ota::Manifest m = goodManifest();
    m.certPinned = false;
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_FALSE(o.accepted());
    TEST_ASSERT_EQUAL_INT(asInt(ota::Decision::RejectNoPinnedCert), asInt(o.decision));
}

void test_ota_pcb_khong_khop_thi_tu_choi(void) {
    // Nạp firmware của PCB khác = sai pin map = hỏng phần cứng.
    ota::Manifest m = goodManifest();
    m.pcbVersion = "1.2";
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_EQUAL_INT(asInt(ota::Decision::RejectPcbMismatch), asInt(o.decision));
}

void test_ota_hash_sai_dinh_dang_thi_tu_choi(void) {
    ota::Manifest m = goodManifest();
    m.sha256Hex = "abc";  // quá ngắn
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_EQUAL_INT(asInt(ota::Decision::RejectBadHash), asInt(o.decision));
}

void test_ota_hash_hoa_thuong_lan_lon_thi_tu_choi(void) {
    ota::Manifest m = goodManifest();
    m.sha256Hex = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef";
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_EQUAL_INT(asInt(ota::Decision::RejectBadHash), asInt(o.decision));
}

void test_ota_anh_qua_lon_thi_tu_choi(void) {
    // Ảnh lớn hơn app slot sẽ ghi tràn sang partition kế bên. Phải chặn TRƯỚC
    // khi ghi byte đầu tiên.
    ota::Manifest m = goodManifest();
    m.sizeBytes = ota::kMaxImageBytes + 1;
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_EQUAL_INT(asInt(ota::Decision::RejectBadSize), asInt(o.decision));
}

void test_ota_cung_phien_ban_thi_bo_qua(void) {
    ota::Manifest m = goodManifest();
    ota::Outcome o = ota::evaluate(m, "2.4.3", "1.3");
    TEST_ASSERT_EQUAL_INT(asInt(ota::Decision::RejectSameVersion), asInt(o.decision));
}

void test_ota_manifest_nullptr_khong_crash(void) {
    ota::Manifest m = goodManifest();
    m.version = nullptr;
    ota::Outcome o = ota::evaluate(m, "2.4.2", "1.3");
    TEST_ASSERT_FALSE(o.accepted());
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_config_hop_le_thi_khong_doi);
    RUN_TEST(test_amplification_time_vuot_thi_clamp);
    RUN_TEST(test_ki_bang_0_thi_clamp_len_toi_thieu);
    RUN_TEST(test_slope_bang_0_thi_TU_CHOI_chu_khong_clamp);
    RUN_TEST(test_slope_NaN_thi_tu_choi);
    RUN_TEST(test_label_khong_null_terminate_thi_cat);
    RUN_TEST(test_clampLabel_cat_chuoi_dai);
    RUN_TEST(test_clampLabel_nullptr_khong_crash);

    RUN_TEST(test_ota_manifest_hop_le_thi_nhan);
    RUN_TEST(test_ota_khong_co_cert_ghim_thi_tu_choi);
    RUN_TEST(test_ota_pcb_khong_khop_thi_tu_choi);
    RUN_TEST(test_ota_hash_sai_dinh_dang_thi_tu_choi);
    RUN_TEST(test_ota_hash_hoa_thuong_lan_lon_thi_tu_choi);
    RUN_TEST(test_ota_anh_qua_lon_thi_tu_choi);
    RUN_TEST(test_ota_cung_phien_ban_thi_bo_qua);
    RUN_TEST(test_ota_manifest_nullptr_khong_crash);

    return UNITY_END();
}
