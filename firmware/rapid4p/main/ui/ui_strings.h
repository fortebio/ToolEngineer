/**
 * ui_strings.h — bảng chuỗi đa ngôn ngữ (VI/EN/ZH/TW) cho màn hình Rapid4P.
 *
 * Nguồn: FBT-ReaderPlus-1.0/src/displayresources.h (language_vietnamese/english/chinese;
 * bảng TW ở bản gốc chép y bảng ZH — giữ vậy). Chuỗi hướng dẫn "Nút Đỏ/Xanh/Trắng"
 * của bản 3 nút bỏ đi; thay bằng nhãn nút chạm (Back/Next/Đo...).
 *
 * FONT: lv_font_vimate_* chỉ có Latin + tiếng Việt. Khi chưa sinh font CJK
 * (R4P_HAVE_CJK_FONT 0) thì ZH/TW rơi về EN để không hiện ô vuông — MAPPING §4.4:
 * sinh font theo đúng 107 chữ Hán đang dùng, rồi bật cờ + khai font trong ui_reader.c.
 */
#pragma once
#include "rapid4p.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef R4P_HAVE_CJK_FONT
#define R4P_HAVE_CJK_FONT 0
#endif

typedef enum {
    STR_START_HINT = 0,     /* "Nhấn ĐO để bắt đầu" */
    STR_CHOOSE_TUBE,        /* "Chọn Ống Để Đo" */
    STR_CHOOSE_SAMPLE,      /* "Chọn Mẫu Để Đo" */
    STR_MEASURING,          /* "Đang Đo" */
    STR_PLEASE_WAIT,        /* "Vui Lòng Chờ Trong Giây Lát..." */
    STR_PREPARE_PUT_TUBE,   /* "Đặt Ống Vào Máy Và Đậy Nắp" */
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
