/**
 * ui_strings.h — bảng chuỗi đa ngôn ngữ (VI/EN/ZH/TW) cho màn hình Rapid4P.
 *
 * Nguồn: FBT-ReaderPlus-1.0/src/displayresources.h (language_vietnamese/english/chinese;
 * bảng TW ở bản gốc chép y bảng ZH — giữ vậy). Chuỗi hướng dẫn "Nút Đỏ/Xanh/Trắng"
 * của bản 3 nút bỏ đi; thay bằng nhãn nút chạm (Back/Next/Đo...).
 *
 * FONT: lv_font_vimate_* chỉ có Latin + tiếng Việt. Khi chưa sinh font CJK
 * (R4P_HAVE_CJK_FONT 0, khai ở rapid4p.h) thì ZH/TW rơi về EN để không hiện ô vuông — MAPPING §4.4:
 * sinh font theo đúng 107 chữ Hán đang dùng, rồi bật cờ + khai font trong ui_reader.c.
 */
#pragma once
#include "rapid4p.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STR_START_HINT = 0,     /* "Nhấn ĐO để bắt đầu" */
    STR_CHOOSE_TUBE,        /* "Chọn Ống Để Đo" */
    STR_CHOOSE_SAMPLE,      /* "Chọn Mẫu Để Đo" */
    STR_MEASURING,          /* "Đang Đo" */
    STR_PLEASE_WAIT,        /* "Vui Lòng Chờ Trong Giây Lát..." */
    STR_PREPARE_PUT_TUBE,   /* có %d = số khe */   /* "Đặt Ống Vào Máy Và Đậy Nắp" */
    STR_PREPARE_PRESS,      /* "Nhấn ĐO để đo!" */
    STR_RESULT_TUBE,        /* "Ống: " */
    STR_RESULT,             /* "Kết Quả" */
    STR_REDO,               /* "Đo lại" */
    STR_FINISH,             /* "Kết thúc" */
    STR_CALIB_MODE,         /* "CHẾ ĐỘ CÂN CHỈNH" */
    STR_CALIB_SAMPLE,       /* "Mẫu Đo: " */
    STR_CALIB_MAX,          /* "Cao Nhất" */
    STR_CALIB_MIN,          /* "Thấp Nhất" */
    STR_CALIB_CLEAR,        /* "Xoá Cân Chỉnh" */
    STR_CALIBRATING,        /* "ĐANG CÂN CHỈNH" */
    STR_SUCCESS,            /* "Thành Công!" */
    STR_SETTINGS,           /* "THIẾT LẬP RAPID" */
    STR_LANGUAGE,           /* "Ngôn Ngữ" */
    STR_WIFI,               /* "Wifi" */
    STR_UPDATE,             /* "Cập Nhật" */
    STR_THRESHOLD,          /* "Threshold" */
    STR_THRESHOLD_SETTING,  /* "CÀI ĐẶT THRESHOLD" */
    STR_VALUE_SETTING,      /* "CÀI ĐẶT GIÁ TRỊ" */
    STR_SAVE,               /* "Lưu" */
    STR_LANGUAGE_SETTING,   /* "CÀI ĐẶT NGÔN NGỮ" */
    STR_WIFI_SETTING,       /* "CÀI ĐẶT WIFI" */
    STR_UPDATING,           /* "Đang Cập Nhật ...." */
    STR_BACK,               /* "Quay lại" */
    STR_NEXT,               /* "Tiếp" */
    STR_CANCEL,             /* "Huỷ" */
    STR_MEASURE,            /* "ĐO" */
    STR_CALIBRATE,          /* "Cân chỉnh" */
    STR_POSITIVE,           /* "Dương tính" */
    STR_NEGATIVE,           /* "Âm tính" */
    STR_SLOT,               /* "Khe" */
    STR_ROUND,              /* "Vòng" */
    STR_STOP,               /* "Dừng" */
    STR_NOT_CALIBRATED,     /* "Chưa cân chỉnh" */
    STR_SENSOR_ERROR,       /* "Lỗi cảm biến" */
    STR_UPLOAD_PENDING,     /* "Chờ gửi" */
    STR_UPLOADED,           /* "Đã gửi" */
    STR_NO_UPDATE,          /* "Không có bản mới" */
    STR_DEVICE_ID,          /* "Mã máy" */
    STR_NO_WIFI,            /* "Chưa có WiFi - vào Thiết lập > Wifi" */
    STR_NO_TOKEN,           /* "Chưa có token máy chủ - nhập qua trang WiFi" */
    STR_CONFIRM,            /* "Xác nhận" */
    STR_CLEAR_CALIB_ASK,    /* "Xoá toàn bộ dữ liệu cân chỉnh 4 khe?" */
    STR_STEP,               /* "Bước" (Bước 1/3) */
    STR_SENSORS,            /* "Cảm biến" (chip "Cảm biến 4/4") */
    STR_SENSOR_NONE_HINT,   /* "Không có cảm biến - kiểm tra bo cảm biến" */
    STR_START,              /* "Bắt đầu" (softkey XANH màn chính) */
    STR_SELECT,             /* "Chọn" (softkey XANH trong danh sách) */
    STR_HOLD_HINT_THR,      /* "Giữ: ±50 · Giữ TRẮNG: huỷ" (màn sửa ngưỡng 2.8") */
    STR_TAP_HOLD_HINT_THR,  /* "Chạm: ±10 · Giữ: ±50" (màn sửa ngưỡng 4.3", dưới hàng −/+) */
    STR_CLEAR,              /* "Xoá" (softkey TRẮNG màn cân chỉnh) */
    /* 2026-09-20 — tối ưu cho nông dân (3 nút cơ, găng tay): */
    STR_REDO_LAST,          /* "Đo lại" (softkey ĐỎ màn chính: mẫu + ống lần trước) */
    STR_LAST_RUN,           /* "Lần trước" (dòng phụ màn chính: "Lần trước: PC · Tôm Thẻ") */
    STR_DONE,               /* "Xong" (softkey ĐỎ màn kết quả → màn chính) */
    STR_HOLD,               /* "giữ" (chữ nhỏ trên nhãn softkey cần nhấn giữ) */
    STR_RETRY,              /* "Thử lại" (màn đo lỗi) */
    STR_TOGGLE_CARD,        /* "Đổi mã" (màn WiFi 2.8": lật thẻ WiFi/mã máy) */
    STR_REMAINING,          /* "còn ~%d s" (font không có ≈) (đếm ngược khi đang đo) */
    STR_POSITIVE_COUNT,     /* "%d/%d DƯƠNG TÍNH" (tổng kết kết quả) */
    STR_ALL_NEGATIVE,       /* "ÂM TÍNH %d/%d" */
    STR_DONT_OPEN,          /* "Đừng mở nắp" (đang đo) */
    STR_MEASURE_ERROR_HINT, /* "Kiểm tra bo cảm biến rồi thử lại" (màn đo lỗi) */
    STR_SETTINGS_SHORT,     /* "Cài đặt" (nhãn softkey; STR_SETTINGS "THIẾT LẬP RAPID" là tiêu đề) */
    STR_START_HINT_KEYS,    /* "Nhấn nút XANH để bắt đầu" (2.8": gợi ý theo màu nút cơ) */
    STR_PREPARE_KEYS,       /* "Đặt ống vào %d khe, đậy nắp rồi nhấn nút ĐỎ" (2.8") */
    STR_IP,                 /* "IP" (màn Cập nhật 2.8": "IP: 192.168.1.5") */
    STR_COUNT
} r4p_str_t;

const char *r4p_str(r4p_str_t id);                       /* theo ngôn ngữ hiện tại */
const char *r4p_str_lang(r4p_str_t id, r4p_lang_t lang);
const char *r4p_lang_display_name(r4p_lang_t which);     /* tên ngôn ngữ `which` viết bằng ngôn ngữ hiện tại */
const char *r4p_sick_label(r4p_sick_t s);                /* PC → "Chứng Dương"/"Positive"/"阳性" */
const char *r4p_sample_label(r4p_sample_t s);            /* "Tôm Thẻ"/"P.Vannamei" */
void r4p_str_set_lang(r4p_lang_t lang);
r4p_lang_t r4p_str_get_lang(void);

#ifdef __cplusplus
}
#endif
