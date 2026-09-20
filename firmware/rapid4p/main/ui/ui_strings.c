#include "ui_strings.h"

static r4p_lang_t s_lang = R4P_LANG_VI;   /* calib_store_load ghi đè theo NVS; mặc định VI */

static const char *const s_vi[STR_COUNT] = {
    [STR_START_HINT]        = "Nhấn ĐO để bắt đầu",
    [STR_CHOOSE_TUBE]       = "Chọn Ống Để Đo",
    [STR_CHOOSE_SAMPLE]     = "Chọn Mẫu Để Đo",
    [STR_MEASURING]         = "Đang Đo",
    [STR_PLEASE_WAIT]       = "Vui lòng chờ trong giây lát...",
    [STR_PREPARE_PUT_TUBE]  = "Đặt ống vào %d khe và đậy nắp",
    [STR_PREPARE_PRESS]     = "Nhấn ĐO để đo!",
    [STR_RESULT_TUBE]       = "Ống: ",
    [STR_RESULT]            = "Kết Quả",
    [STR_REDO]              = "Đo lại",
    [STR_FINISH]            = "Kết thúc",
    [STR_CALIB_MODE]        = "CHẾ ĐỘ CÂN CHỈNH",
    [STR_CALIB_SAMPLE]      = "Mẫu đo: ",
    [STR_CALIB_MAX]         = "Cao Nhất",
    [STR_CALIB_MIN]         = "Thấp Nhất",
    [STR_CALIB_CLEAR]       = "Xoá cân chỉnh",
    [STR_CALIBRATING]       = "ĐANG CÂN CHỈNH",
    [STR_SUCCESS]           = "Thành công!",
    [STR_SETTINGS]          = "THIẾT LẬP RAPID",
    [STR_LANGUAGE]          = "Ngôn ngữ",
    [STR_WIFI]              = "Wifi",
    [STR_UPDATE]            = "Cập nhật",
    [STR_THRESHOLD]         = "Threshold",
    [STR_THRESHOLD_SETTING] = "CÀI ĐẶT THRESHOLD",
    [STR_VALUE_SETTING]     = "CÀI ĐẶT GIÁ TRỊ",
    [STR_SAVE]              = "Lưu",
    [STR_LANGUAGE_SETTING]  = "CÀI ĐẶT NGÔN NGỮ",
    [STR_WIFI_SETTING]      = "CÀI ĐẶT WIFI",
    [STR_UPDATING]          = "Đang cập nhật...",
    [STR_BACK]              = "Quay lại",
    [STR_NEXT]              = "Tiếp",
    [STR_CANCEL]            = "Huỷ",
    [STR_MEASURE]           = "ĐO",
    [STR_CALIBRATE]         = "Cân chỉnh",
    [STR_POSITIVE]          = "Dương tính",
    [STR_NEGATIVE]          = "Âm tính",
    [STR_SLOT]              = "Khe",
    [STR_ROUND]             = "Vòng",
    [STR_STOP]              = "Dừng",
    [STR_NOT_CALIBRATED]    = "Chưa cân chỉnh",
    [STR_SENSOR_ERROR]      = "Lỗi cảm biến",
    [STR_UPLOAD_PENDING]    = "Chờ gửi",
    [STR_UPLOADED]          = "Đã gửi",
    [STR_NO_UPDATE]         = "Không có bản mới",
    [STR_DEVICE_ID]         = "Mã máy",
    [STR_NO_WIFI]           = "Chưa có WiFi - vào Thiết lập > Wifi",
    [STR_NO_TOKEN]          = "Chưa có token máy chủ - nhập ở trang WiFi",
    [STR_CONFIRM]           = "Xác nhận",
    [STR_CLEAR_CALIB_ASK]   = "Xoá toàn bộ dữ liệu cân chỉnh %d khe?",
    [STR_STEP]              = "Bước",
    [STR_SENSORS]           = "Cảm biến",
    [STR_SENSOR_NONE_HINT]  = "Không có cảm biến - kiểm tra bo cảm biến rồi khởi động lại",
    [STR_START]             = "Bắt đầu",
    [STR_SELECT]            = "Chọn",
    [STR_HOLD_HINT_THR]     = "Giữ: ±50  ·  Giữ TRẮNG: huỷ",
    [STR_CLEAR]             = "Xoá",
    [STR_REDO_LAST]         = "Đo lại",
    [STR_LAST_RUN]          = "Lần trước",
    [STR_DONE]              = "Xong",
    [STR_HOLD]              = "giữ",
    [STR_RETRY]             = "Thử lại",
    [STR_TOGGLE_CARD]       = "Đổi mã",
    [STR_REMAINING]         = "còn ~%d s",
    [STR_POSITIVE_COUNT]    = "%d/%d DƯƠNG TÍNH",
    [STR_ALL_NEGATIVE]      = "ÂM TÍNH %d/%d",
    [STR_DONT_OPEN]         = "Đừng mở nắp",
    [STR_MEASURE_ERROR_HINT]= "Kiểm tra bo cảm biến rồi thử lại",
    [STR_SETTINGS_SHORT]    = "Cài đặt",
};

static const char *const s_en[STR_COUNT] = {
    [STR_START_HINT]        = "Press MEASURE to start",
    [STR_CHOOSE_TUBE]       = "SELECT TUBE",
    [STR_CHOOSE_SAMPLE]     = "SELECT SAMPLE",
    [STR_MEASURING]         = "In process",
    [STR_PLEASE_WAIT]       = "Waiting...",
    [STR_PREPARE_PUT_TUBE]  = "Put tubes in %d slots and close the lid",
    [STR_PREPARE_PRESS]     = "Press MEASURE!",
    [STR_RESULT_TUBE]       = "Tube: ",
    [STR_RESULT]            = "Result",
    [STR_REDO]              = "Redo",
    [STR_FINISH]            = "Finish",
    [STR_CALIB_MODE]        = "CALIBRATION MODE",
    [STR_CALIB_SAMPLE]      = "Tube: ",
    [STR_CALIB_MAX]         = "Highest",
    [STR_CALIB_MIN]         = "Lowest",
    [STR_CALIB_CLEAR]       = "Clear calibration",
    [STR_CALIBRATING]       = "Calibrating...",
    [STR_SUCCESS]           = "Success!",
    [STR_SETTINGS]          = "RAPID SETTING",
    [STR_LANGUAGE]          = "Language",
    [STR_WIFI]              = "Wifi",
    [STR_UPDATE]            = "Update",
    [STR_THRESHOLD]         = "Threshold",
    [STR_THRESHOLD_SETTING] = "THRESHOLD SETTING",
    [STR_VALUE_SETTING]     = "VALUE SETTING",
    [STR_SAVE]              = "Save",
    [STR_LANGUAGE_SETTING]  = "LANGUAGE SETTING",
    [STR_WIFI_SETTING]      = "WIFI SETTING",
    [STR_UPDATING]          = "Updating...",
    [STR_BACK]              = "Back",
    [STR_NEXT]              = "Next",
    [STR_CANCEL]            = "Cancel",
    [STR_MEASURE]           = "MEASURE",
    [STR_CALIBRATE]         = "Calibrate",
    [STR_POSITIVE]          = "Positive",
    [STR_NEGATIVE]          = "Negative",
    [STR_SLOT]              = "Slot",
    [STR_ROUND]             = "Round",
    [STR_STOP]              = "Stop",
    [STR_NOT_CALIBRATED]    = "Not calibrated",
    [STR_SENSOR_ERROR]      = "Sensor error",
    [STR_UPLOAD_PENDING]    = "Upload pending",
    [STR_UPLOADED]          = "Uploaded",
    [STR_NO_UPDATE]         = "No update available",
    [STR_DEVICE_ID]         = "Device ID",
    [STR_NO_WIFI]           = "No WiFi - open Settings > Wifi",
    [STR_NO_TOKEN]          = "No server token - enter it on the WiFi page",
    [STR_CONFIRM]           = "Confirm",
    [STR_CLEAR_CALIB_ASK]   = "Clear calibration data of all %d slots?",
    [STR_STEP]              = "Step",
    [STR_SENSORS]           = "Sensors",
    [STR_SENSOR_NONE_HINT]  = "No sensors found - check the sensor board and restart",
    [STR_START]             = "Start",
    [STR_SELECT]            = "Select",
    [STR_HOLD_HINT_THR]     = "Hold: +/-50  ·  Hold WHITE: cancel",
    [STR_CLEAR]             = "Clear",
    [STR_REDO_LAST]         = "Redo",
    [STR_LAST_RUN]          = "Last run",
    [STR_DONE]              = "Done",
    [STR_HOLD]              = "hold",
    [STR_RETRY]             = "Retry",
    [STR_TOGGLE_CARD]       = "Flip",
    [STR_REMAINING]         = "~%d s left",
    [STR_POSITIVE_COUNT]    = "%d/%d POSITIVE",
    [STR_ALL_NEGATIVE]      = "NEGATIVE %d/%d",
    [STR_DONT_OPEN]         = "Keep the lid closed",
    [STR_MEASURE_ERROR_HINT]= "Check the sensor board and retry",
    [STR_SETTINGS_SHORT]    = "Settings",
};

/* Giữ nguyên chuỗi ZH của bản gốc; chuỗi mới (nút chạm) dịch ngắn gọn. */
static const char *const s_zh[STR_COUNT] = {
    [STR_START_HINT]        = "按测量键开始",
    [STR_CHOOSE_TUBE]       = "选择试管",
    [STR_CHOOSE_SAMPLE]     = "选择样本",
    [STR_MEASURING]         = "进行中",
    [STR_PLEASE_WAIT]       = "请稍等...",
    [STR_PREPARE_PUT_TUBE]  = "将试管放入%d个槽位并关闭盖子",
    [STR_PREPARE_PRESS]     = "按测量键!",
    [STR_RESULT_TUBE]       = "试管: ",
    [STR_RESULT]            = "平均值",
    [STR_REDO]              = "重做",
    [STR_FINISH]            = "完成",
    [STR_CALIB_MODE]        = "校准模式",
    [STR_CALIB_SAMPLE]      = "试管类型: ",
    [STR_CALIB_MAX]         = "最高值",
    [STR_CALIB_MIN]         = "最低值",
    [STR_CALIB_CLEAR]       = "删除校准",
    [STR_CALIBRATING]       = "校准中...",
    [STR_SUCCESS]           = "成功!",
    [STR_SETTINGS]          = "设置",
    [STR_LANGUAGE]          = "语言",
    [STR_WIFI]              = "Wifi",
    [STR_UPDATE]            = "更新",
    [STR_THRESHOLD]         = "阈值",
    [STR_THRESHOLD_SETTING] = "设置阈值",
    [STR_VALUE_SETTING]     = "数值设置",
    [STR_SAVE]              = "保存",
    [STR_LANGUAGE_SETTING]  = "语言",
    [STR_WIFI_SETTING]      = "WiFi 设置",
    [STR_UPDATING]          = "更新中...",
    [STR_BACK]              = "返回",
    [STR_NEXT]              = "下一个",
    [STR_CANCEL]            = "取消",
    [STR_MEASURE]           = "测量",
    [STR_CALIBRATE]         = "校准",
    [STR_POSITIVE]          = "阳性",
    [STR_NEGATIVE]          = "阴性",
    [STR_SLOT]              = "槽",
    [STR_ROUND]             = "轮",
    [STR_STOP]              = "停止",
    [STR_NOT_CALIBRATED]    = "未校准",
    [STR_SENSOR_ERROR]      = "传感器错误",
    [STR_UPLOAD_PENDING]    = "等待上传",
    [STR_UPLOADED]          = "已上传",
    [STR_NO_UPDATE]         = "没有更新",
    [STR_DEVICE_ID]         = "设备 ID",
    [STR_NO_WIFI]           = "未连接WiFi - 请到设置 > Wifi",
    [STR_NO_TOKEN]          = "缺少服务器令牌 - 请在WiFi页面输入",
    [STR_CONFIRM]           = "确认",
    [STR_CLEAR_CALIB_ASK]   = "删除全部%d个槽位的校准数据?",
    [STR_STEP]              = "步骤",
    [STR_SENSORS]           = "传感器",
    [STR_SENSOR_NONE_HINT]  = "未检测到传感器 - 请检查传感器板后重启",
    [STR_START]             = "开始",
    [STR_SELECT]            = "选择",
    [STR_HOLD_HINT_THR]     = "长按: ±50  ·  长按白键: 取消",
    [STR_CLEAR]             = "清除",
    [STR_REDO_LAST]         = "重测",
    [STR_LAST_RUN]          = "上次",
    [STR_DONE]              = "完成",
    [STR_HOLD]              = "长按",
    [STR_RETRY]             = "重试",
    [STR_TOGGLE_CARD]       = "切换",
    [STR_REMAINING]         = "还剩约 %d 秒",
    [STR_POSITIVE_COUNT]    = "%d/%d 阳性",
    [STR_ALL_NEGATIVE]      = "阴性 %d/%d",
    [STR_DONT_OPEN]         = "请勿打开盖子",
    [STR_MEASURE_ERROR_HINT]= "请检查传感器板后重试",
    [STR_SETTINGS_SHORT]    = "设置",
};

static const char *const *table_for(r4p_lang_t lang)
{
    switch (lang) {
        case R4P_LANG_VI: return s_vi;
        case R4P_LANG_ZH:
        case R4P_LANG_TW:
#if R4P_HAVE_CJK_FONT
            return s_zh;
#else
            return s_en;   /* chưa có font CJK → EN, tránh ô vuông */
#endif
        default: return s_en;
    }
}

const char *r4p_str_lang(r4p_str_t id, r4p_lang_t lang)
{
    if (id >= STR_COUNT) return "";
    const char *s = table_for(lang)[id];
    return s ? s : s_en[id];
}

const char *r4p_str(r4p_str_t id) { return r4p_str_lang(id, s_lang); }

void r4p_str_set_lang(r4p_lang_t lang) { if (lang < R4P_LANG_COUNT) s_lang = lang; }
r4p_lang_t r4p_str_get_lang(void) { return s_lang; }

/* Tên ngôn ngữ (language_convert) — luôn hiện được vì tên VI/EN là Latin;
 * tên bằng ZH chỉ khi có font. */
static const char *const s_lang_names_vi[R4P_LANG_COUNT] = { "Tiếng Việt", "Tiếng Anh", "Tiếng Trung", "Tiếng Đài" };
static const char *const s_lang_names_en[R4P_LANG_COUNT] = { "Vietnamese", "English", "Chinese", "Taiwanese" };
static const char *const s_lang_names_zh[R4P_LANG_COUNT] = { "越南语", "英语", "中文", "台湾语" };

const char *r4p_lang_display_name(r4p_lang_t which)
{
    if (which >= R4P_LANG_COUNT) return "";
    switch (s_lang) {
        case R4P_LANG_VI: return s_lang_names_vi[which];
        case R4P_LANG_ZH: case R4P_LANG_TW:
#if R4P_HAVE_CJK_FONT
            return s_lang_names_zh[which];
#endif
        default: (void)s_lang_names_zh; return s_lang_names_en[which];
    }
}

const char *r4p_sick_label(r4p_sick_t s)
{
    switch (s) {
        case R4P_SICK_PC:
            return s_lang == R4P_LANG_VI ? "Chứng Dương" : (table_for(s_lang) == s_zh ? "阳性" : "Positive");
        case R4P_SICK_WSSV:
            return s_lang == R4P_LANG_VI ? "Đốm Trắng" : "WSSV";
        default:
            return r4p_sick_name(s);
    }
}

const char *r4p_sample_label(r4p_sample_t s)
{
    const bool vi = (s_lang == R4P_LANG_VI);
    switch (s) {
        case R4P_SAMPLE_VANNAMEI: return vi ? "Tôm Thẻ"   : "P.Vannamei";
        case R4P_SAMPLE_MONODON:  return vi ? "Tôm Sú"    : "P.Monodon";
        case R4P_SAMPLE_TILAPIA:  return vi ? "Cá Rô Phi" : "Tilapia";
        case R4P_SAMPLE_PIG:      return vi ? "Heo"       : "Pig";
        case R4P_SAMPLE_WATER:    return vi ? "Nước"      : "Water";
        default: return "";
    }
}

/* Khoá payload/NVS — KHÔNG dịch, KHÔNG đổi (server + legacy Sheet đọc chuỗi này). */
const char *r4p_sick_name(r4p_sick_t s)
{
    switch (s) {
        case R4P_SICK_PC: return "PC";
        case R4P_SICK_EHP: return "EHP";
        case R4P_SICK_EMS: return "EMS";
        case R4P_SICK_WSSV: return "WSSV";
        case R4P_SICK_TPD: return "TPD";
        default: return "?";
    }
}

const char *r4p_sample_name(r4p_sample_t s)
{
    switch (s) {
        case R4P_SAMPLE_VANNAMEI: return "PRAWN Vannamei";
        case R4P_SAMPLE_MONODON: return "PRAWN Monodon";
        case R4P_SAMPLE_TILAPIA: return "FISH Tilapia";
        case R4P_SAMPLE_PIG: return "PIG";
        case R4P_SAMPLE_WATER: return "WATER";
        default: return "?";
    }
}
