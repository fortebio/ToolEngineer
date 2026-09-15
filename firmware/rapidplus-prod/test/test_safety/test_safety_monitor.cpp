// Test lớp an toàn nhiệt — chạy trên HOST, không cần phần cứng.
//   pio test -e native -f test_safety
//
// Các test này là hợp đồng an toàn của thiết bị. Test nào đỏ = ĐỪNG PHÁT HÀNH.
// Sửa test cho xanh mà không sửa nguyên nhân là hành vi bị cấm (AGENTS.md §0).

#include <unity.h>

#include <limits>

#include "safety_monitor.h"

using namespace safety;

namespace {

constexpr unsigned long kNow   = 100000;
constexpr unsigned long kFresh = kNow - 100;  // số đọc mới, chưa timeout

// Unity so sánh số nguyên; enum class không tự chuyển sang int.
int asInt(Action a) { return static_cast<int>(a); }
int asInt(Reason r) { return static_cast<int>(r); }

const float kNaN = std::numeric_limits<float>::quiet_NaN();

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// ─── Đường bình thường ───────────────────────────────────────────────────────

void test_nhiet_do_binh_thuong_cho_phep(void) {
    Reading r{60.0f, 50.0f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_EQUAL_INT(asInt(Action::Allow), asInt(v.action));
    TEST_ASSERT_FALSE(v.mustStop());
}

// ─── Hard-limit ──────────────────────────────────────────────────────────────

void test_bottom_vuot_hard_limit_thi_cat(void) {
    Reading r{kBottomHardLimitC + 0.1f, 50.0f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_TRUE(v.mustStop());
    TEST_ASSERT_EQUAL_INT(asInt(Reason::BottomOverTemp), asInt(v.reason));
}

void test_hotlid_vuot_hard_limit_thi_cat(void) {
    Reading r{60.0f, kHotlidHardLimitC + 0.1f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_TRUE(v.mustStop());
    TEST_ASSERT_EQUAL_INT(asInt(Reason::HotlidOverTemp), asInt(v.reason));
}

void test_dung_bang_hard_limit_thi_chua_cat(void) {
    // Ngưỡng là "vượt quá", không phải "bằng". Khoá lại ranh giới để lần
    // refactor sau không vô tình đổi > thành >= hay ngược lại.
    Reading r{kBottomHardLimitC, 50.0f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_FALSE(v.mustStop());
}

// ─── Sensor hỏng ─────────────────────────────────────────────────────────────

void test_sensor_timeout_thi_cat(void) {
    Reading r{25.0f, 25.0f, kNow - kSensorTimeoutMs - 1};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_TRUE(v.mustStop());
    TEST_ASSERT_EQUAL_INT(asInt(Reason::SensorTimeout), asInt(v.reason));
}

void test_so_doc_ngoai_dai_thi_cat(void) {
    // Sensor hở mạch cho giá trị rác. Nhiệt độ "mát" nhưng vô lý vẫn phải cắt.
    Reading r{-273.0f, 50.0f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_TRUE(v.mustStop());
    TEST_ASSERT_EQUAL_INT(asInt(Reason::SensorOutOfRange), asInt(v.reason));
}

void test_nan_thi_cat(void) {
    // Đây là test dễ bị bỏ sót nhất: mọi so sánh với NaN đều false, nên một
    // kiểm tra ngưỡng viết ẩu sẽ CHO QUA NaN và máy cứ thế gia nhiệt.
    Reading r{kNaN, 50.0f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_TRUE(v.mustStop());
    TEST_ASSERT_EQUAL_INT(asInt(Reason::SensorOutOfRange), asInt(v.reason));
}

void test_sensor_hong_duoc_xet_truoc_nguong_nhiet(void) {
    // Số đọc cũ có thể "mát" trong khi máy đã nóng thật. Timeout phải thắng.
    Reading r{25.0f, 25.0f, kNow - kSensorTimeoutMs - 1};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_EQUAL_INT(asInt(Reason::SensorTimeout), asInt(v.reason));
}

// ─── Cảnh báo sớm ────────────────────────────────────────────────────────────

void test_gan_nguong_thi_canh_bao_chu_khong_cat(void) {
    Reading r{kBottomHardLimitC - 1.0f, 50.0f, kFresh};
    Verdict v = evaluate(r, kNow);
    TEST_ASSERT_EQUAL_INT(asInt(Action::Warn), asInt(v.action));
    TEST_ASSERT_FALSE(v.mustStop());
}

// ─── Tràn millis() ───────────────────────────────────────────────────────────

void test_millis_tran_khong_gay_cat_gia(void) {
    // millis() tràn sau ~49.7 ngày. Máy PCR chạy liên tục nhiều tuần là bình
    // thường, nên đây KHÔNG phải trường hợp lý thuyết. Trừ theo unsigned cho
    // kết quả đúng khi tràn; nếu ai đó đổi sang kiểu có dấu, test này sẽ đỏ.
    const unsigned long nowAfterWrap   = 50;
    const unsigned long lastBeforeWrap = 0xFFFFFFFFul - 50;
    Reading r{60.0f, 50.0f, lastBeforeWrap};
    Verdict v = evaluate(r, nowAfterWrap);
    TEST_ASSERT_FALSE(v.mustStop());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_nhiet_do_binh_thuong_cho_phep);
    RUN_TEST(test_bottom_vuot_hard_limit_thi_cat);
    RUN_TEST(test_hotlid_vuot_hard_limit_thi_cat);
    RUN_TEST(test_dung_bang_hard_limit_thi_chua_cat);
    RUN_TEST(test_sensor_timeout_thi_cat);
    RUN_TEST(test_so_doc_ngoai_dai_thi_cat);
    RUN_TEST(test_nan_thi_cat);
    RUN_TEST(test_sensor_hong_duoc_xet_truoc_nguong_nhiet);
    RUN_TEST(test_gan_nguong_thi_canh_bao_chu_khong_cat);
    RUN_TEST(test_millis_tran_khong_gay_cat_gia);
    return UNITY_END();
}
