#ifndef DISPLAY_RESOURCES_H
#define DISPLAY_RESOURCES_H
#include "Arduino.h"
#include "Icon/iconSample.h"
/* lock giá trị calib */

static const unsigned char logoFBT[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
    0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x01, 0xf8, 0x00, 0x00, 0x00, 0x1f, 0xf0, 0x00, 0x00, 0x00,
    0xff, 0xc0, 0x00, 0x00, 0x03, 0xff, 0x80, 0x00, 0x00, 0x1b, 0xfe, 0x00, 0x00, 0x00, 0x7b, 0xfc,
    0x00, 0x00, 0x01, 0xfb, 0xf0, 0x00, 0x00, 0x07, 0xfb, 0xe0, 0x00, 0x00, 0x1f, 0xfb, 0x80, 0x00,
    0x00, 0x1f, 0xfb, 0x00, 0x00, 0x00, 0x07, 0xfa, 0x00, 0x00, 0x00, 0x31, 0xf8, 0x00, 0x00, 0x00,
    0x3c, 0x78, 0x00, 0x00, 0x00, 0x3f, 0x18, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x3f,
    0xf0, 0x00, 0x00, 0x00, 0x3f, 0xf8, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x3f, 0x98,
    0x00, 0x00, 0x00, 0x3e, 0x78, 0x00, 0x00, 0x00, 0x39, 0xf8, 0x00, 0x00, 0x00, 0x07, 0xf8, 0x30,
    0x00, 0x00, 0x1f, 0xf9, 0xe0, 0x00, 0x00, 0x1f, 0xff, 0xc0, 0x00, 0x00, 0x07, 0xff, 0x80, 0x00,
    0x00, 0x01, 0xff, 0x80, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const unsigned char shrimp[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x0f, 0xc0, 0x00, 0x40, 0x01, 0x3f, 0xf0, 0x00, 0x00, 0x1f, 0xff, 0xf8, 0x00, 0x00, 0x7f, 0xfe,
    0xfc, 0x00, 0x03, 0xff, 0xff, 0xfe, 0x00, 0x21, 0xff, 0xff, 0xfe, 0x00, 0x18, 0x1f, 0xff, 0x3f,
    0x00, 0x00, 0x33, 0xfb, 0xe3, 0x00, 0x00, 0xff, 0x83, 0xfe, 0x00, 0x01, 0x48, 0x07, 0xbf, 0x00,
    0x00, 0x10, 0x05, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00,
    0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x07, 0xd8, 0x00, 0x00, 0x00,
    0x0f, 0xf0, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x0f, 0xc0, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00};

static const unsigned char PROGMEM image_Layer_1_bits[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xf0, 0x00, 0x18, 0x1f, 0xfc, 0x00, 0x1c, 0x7f, 0xff, 0x00, 0x1e, 0xff, 0xff, 0x80, 0x1f, 0xff, 0xff, 0xc0, 0x1f, 0xff, 0xff, 0xe0, 0x0f, 0xff, 0xf7, 0xe0, 0x0f, 0xff, 0xff, 0xe0, 0x1f, 0xff, 0xff, 0xc0, 0x1f, 0xff, 0xff, 0xc0, 0x1e, 0x7f, 0xff, 0x80, 0x1c, 0x3f, 0xfe, 0x00, 0x10, 0x1f, 0xfc, 0x00, 0x00, 0x03, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* ICon language*/
static const unsigned char PROGMEM VIE[] = {0xc0, 0xc3, 0xf0, 0xff, 0xc0, 0xc0, 0xc3, 0xf0, 0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0xc0, 0xc0, 0xc0, 0xff, 0x00, 0xc0, 0xc0, 0xc0, 0xff, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0x33, 0x00, 0xc0, 0xc0, 0x00, 0x33, 0x00, 0xc0, 0xc0, 0x00, 0x0c, 0x03, 0xf0, 0xff, 0xc0, 0x0c, 0x03, 0xf0, 0xff, 0xc0};
static const unsigned char PROGMEM ENG[] = {0xff, 0xcc, 0x0c, 0x3f, 0xc0, 0xff, 0xcc, 0x0c, 0x3f, 0xc0, 0xc0, 0x0c, 0x0c, 0xc0, 0xc0, 0xc0, 0x0c, 0x0c, 0xc0, 0xc0, 0xc0, 0x0f, 0x0c, 0xc0, 0x00, 0xc0, 0x0f, 0x0c, 0xc0, 0x00, 0xff, 0x0c, 0xcc, 0xc0, 0x00, 0xff, 0x0c, 0xcc, 0xc0, 0x00, 0xc0, 0x0c, 0x3c, 0xc3, 0xc0, 0xc0, 0x0c, 0x3c, 0xc3, 0xc0, 0xc0, 0x0c, 0x0c, 0xc0, 0xc0, 0xc0, 0x0c, 0x0c, 0xc0, 0xc0, 0xff, 0xcc, 0x0c, 0x3f, 0xc0, 0xff, 0xcc, 0x0c, 0x3f, 0xc0};
static const unsigned char PROGMEM CHI[] = {0x3f, 0x0c, 0x0c, 0x3f, 0x3f, 0x0c, 0x0c, 0x3f, 0xc0, 0xcc, 0x0c, 0x0c, 0xc0, 0xcc, 0x0c, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0x0f, 0xfc, 0x0c, 0xc0, 0x0f, 0xfc, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0xcc, 0x0c, 0x0c, 0xc0, 0xcc, 0x0c, 0x0c, 0x3f, 0x0c, 0x0c, 0x3f, 0x3f, 0x0c, 0x0c, 0x3f};
static const unsigned char PROGMEM TAW[] = {0x3f, 0x0c, 0x0c, 0x3f, 0x3f, 0x0c, 0x0c, 0x3f, 0xc0, 0xcc, 0x0c, 0x0c, 0xc0, 0xcc, 0x0c, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0x0f, 0xfc, 0x0c, 0xc0, 0x0f, 0xfc, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0x0c, 0x0c, 0x0c, 0xc0, 0xcc, 0x0c, 0x0c, 0xc0, 0xcc, 0x0c, 0x0c, 0x3f, 0x0c, 0x0c, 0x3f, 0x3f, 0x0c, 0x0c, 0x3f};

static const unsigned char PROGMEM ThresholdIcon[] = {0x00, 0x00, 0x80, 0x00, 0x83, 0x80, 0x82, 0x80, 0x82, 0x80, 0x82, 0xb8, 0x82, 0xa8, 0x82, 0xa8, 0xba, 0xa8, 0xaa, 0xa8, 0xaa, 0xa8, 0xaa, 0xa8, 0xbb, 0xb8, 0x80, 0x00, 0xff, 0xfc, 0x00, 0x00};
static const unsigned char PROGMEM UpdateIcon[] = {0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x09, 0x20, 0x07, 0xc0, 0xe3, 0x8e, 0xa1, 0x0a, 0xbf, 0xfa, 0x80, 0x02, 0x80, 0x02, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief  Infrared arrow down and up icons, 4x4 pixels each.
 *
 */
static const unsigned char PROGMEM image_InfraredArrowDown_bits[] = {0xff, 0x7e, 0x3c, 0x18};
static const unsigned char PROGMEM image_InfraredArrowUp_bits[] = {0x18, 0x3c, 0x7e, 0xff};

/* Icon Wifi Connect */
static const unsigned char PROGMEM image_wifi_not_connected_bits[] = {0x21, 0xf0, 0x00, 0x16, 0x0c, 0x00, 0x08, 0x03, 0x00, 0x25, 0xf0, 0x80, 0x42, 0x0c, 0x40, 0x89, 0x02, 0x20, 0x10, 0xa1, 0x00, 0x23, 0x58, 0x80, 0x04, 0x24, 0x00, 0x08, 0x52, 0x00, 0x01, 0xa8, 0x00, 0x02, 0x04, 0x00, 0x00, 0x42, 0x00, 0x00, 0xa1, 0x00, 0x00, 0x40, 0x80, 0x00, 0x00, 0x00};
static const unsigned char PROGMEM image_wifi_connected_bits[] = {0x01, 0xf0, 0x00, 0x07, 0xfc, 0x00, 0x1e, 0x0f, 0x00, 0x39, 0xf3, 0x80, 0x77, 0xfd, 0xc0, 0xef, 0x1e, 0xe0, 0x5c, 0xe7, 0x40, 0x3b, 0xfb, 0x80, 0x17, 0x1d, 0x00, 0x0e, 0xee, 0x00, 0x05, 0xf4, 0x00, 0x03, 0xb8, 0x00, 0x01, 0x50, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00};

typedef enum
{
    VietNamese,
    English,
    Chinese,
    NUMBER_LANGUAGE,
    Taiwanese
} language_pointer;

typedef enum
{
    SCREEN_START,
    CHOOSE_TUBE_MODE,
    CHOOSE_SAMPLE_MODE,
    CHOOSE_ANIMAL_MODE,
    CHOOSE_ANIMAL_PRAWN,
    CHOOSE_ANIMAL_FISH,
    CHOOSE_ANIMAL_PIG,
    WAITING_READ_SENSOR,
    WAITING_PROCESS,
    PREPARE_PUT_TUBE,
    PREPARE_NOTIFICATION,
    RESULT_SICK,
    RESULT_AVERAGE,
    RESULT_FEATURE_GREEN,
    RESULT_FEATURE_RED,
    RESULT_ERROR,
    RESULT_NOISE_CONTENT,
    CALIB_MODE,
    CALIB_SAMPLE,
    CALIB_SAMPLE_MAX,
    CALIB_SAMPLE_MIN,
    CALIB_FEATURE_GREEN,
    CALIB_FEATURE_RED,
    CALIB_FEATURE_WHITE,
    CALIB_WAITING_TITILE,
    CALIB_WAITING_PLEASE,
    CALIB_COMPLETE,
    CALIB_LOGIN_MODE,
    CALIB_LOGIN_UPDATE_GREEN,
    CALIB_LOGIN_UPDATE_RED,
    CALIB_LOGIN_UPDATE_WHITE,
    CALIB_LOGIN_UPDATE_HOLDGREEN,
    SETTING_MODE,
    SETTING_LANGUAGE,
    SETTING_SAMPLE,
    SETTING_WIFI,
    SETTING_UPDATE,
    SETTING_THRESHOLD,
    THRESHOLD_MODE,
    THRESHOLD_MANUAL,
    THRESHOLD_WIRELESS,
    THRESHOLD_FEATURE_RED,
    THRESHOLD_FEATURE_GREEN,
    THRESHOLD_FEATURE_WHITE,
    THRESHOLD_UPDATE_MODE,
    THRESHOLD_UPDATE_GREEN,
    THRESHOLD_UPDATE_RED,
    THRESHOLD_UPDATE_WHITE,
    THRESHOLD_UPDATE_PRESSGREEN,
    THRESHOLD_WIRELESS_MODE,
    THRESHOLD_WIRELESS_WAITING,
    THRESHOLD_WIRELESS_FINSHED,
    LANGUAGE_MODE,
    WIFI_SETTING_MODE,
    WIFI_SETTING_ID,
    WIFI_SETTING_PASSWORD,
    WIFI_SETTING_ID_DEVICE,
    WIFI_SETTING_FINISHED,
    UPDATE_MODE,
    UPDATE_LOADING,
    UPDATE_FINISHED,
    NUMBERSCREEN
} screen_type;

static const char *language_vietnamese[] = {
    "Nhấn Nút Xanh Để Bắt Đầu",                                                  /*SCREEN_START*/
    "Chọn Ống Để Đo",                                                            /*CHOOSE_TUBE_MODE*/
    "Chọn Mẫu Để Đo",                                                            /*CHOOSE_SAMPLE_MODE*/
    "Chọn Con Vật",                                                              /*CHOOSE_ANIMAL_MODE*/
    "Con Tôm",                                                                   /*CHOOSE_ANIMAL_PRAWN*/
    "Con Cá",                                                                    /*CHOOSE_ANIMAL_FISH*/
    "Con Heo",                                                                   /*CHOOSE_ANIMAL_PIG*/
    "Đang Đo",                                                                   /*WAITING_READ_SENSOR*/
    "Vui Lòng Chờ Trong Giây Lát...",                                            /*WAITING_PROCESS*/
    "Đặt Ống Vào Máy Và Đậy Nắp",                                                /*PREPARE_PUT_TUBE*/
    "Nhấn Nút Đỏ Để Đo!",                                                        /*PREPARE_NOTIFICATION*/
    "Ống: ",                                                                     /*RESULT_SICK*/
    "Kết Quả",                                                                   /*RESULT_AVERAGE*/
    "Nút Xanh: Đo lại",                                                          /*RESULT_FEATURE_GREEN*/
    "Nút Đỏ: Kết thúc",                                                          /*RESULT_FEATURE_RED*/
    "Nhấn Nút Đỏ Khởi Động Lại ",                                                /*RESULT_ERROR*/
    "Xin lỗi vì máy gặp nhiễu cảm biến, cần khởi động lại để hoạt động ổn định", /*RESULT_NOISE_CONTENT*/
    "CHẾ ĐỘ CÂN CHỈNH",                                                          /*CALIB_MODE*/
    "Mẫu Đo: ",                                                                  /*CALIB_SAMPLE*/
    "Cao Nhất",                                                                  /*CALIB_SAMPLE_MAX*/
    "Thấp Nhất",                                                                 /*CALIB_SAMPLE_MIN*/
    "Nút Xanh: Thoát",                                                           /*CALIB_FEATURE_GREEN*/
    "Nút Đỏ: Cân Chỉnh",                                                         /*CALIB_FEATURE_RED*/
    "Nút Trắng: Xoá Cân Chỉnh",                                                  /*CALIB_FEATURE_WHITE*/
    "ĐANG CÂN CHỈNH",                                                            /*CALIB_WAITING_TITILE*/
    "Vui lòng chờ trong giây lát...",                                            /*CALIB_WAITING_PLEASE*/
    "Thành Công!!!",                                                             /*CALIB_COMPLETE*/
    "ĐĂNG NHẬP CALIB",                                                           /*CALIB_LOGIN_MODE*/
    "Nút Xanh: Tăng",                                                            /*CALIB_LOGIN_UPDATE_GREEN*/
    "Nút Đỏ: Giảm",                                                              /*CALIB_LOGIN_UPDATE_RED*/
    "Nút Trắng: Kế Tiếp",                                                        /*CALIB_LOGIN_UPDATE_WHITE*/
    "Giữ Nút Xanh: Lưu",                                                         /*CALIB_LOGIN_UPDATE_HOLDGREEN*/
    "THIẾT LẬP RAPID",                                                           /*SETTING_MODE*/
    "Ngôn Ngữ",                                                                  /*SETTING_LANGUAGE*/
    "Con Vật",                                                                   /*SETTING_SAMPLE*/
    "Wifi",                                                                      /*SETTING_WIFI*/
    "Cập Nhật",                                                                  /*SETTING_UPDATE*/
    "Ngưỡng",                                                                    /*SETTING_THRESHOLD*/
    "CÀI ĐẶT NGƯỠNG",                                                            /*THRESHOLD_MODE*/
    "Thủ Công",                                                                  /*THRESHOLD_MANUAL*/
    "Không Dây",                                                                 /*THRESHOLD_WIRELESS*/
    "Nút Đỏ: Không Dây",                                                         /*THRESHOLD_FEATURE_RED*/
    "Nút Xanh: Thủ Công",                                                        /*THRESHOLD_FEATURE_GREEN*/
    "Nút Trắng: Thoát",                                                          /*THRESHOLD_FEATURE_WHITE*/
    "CÀI ĐẶT GIÁ TRỊ",                                                           /*THRESHOLD_UPDATE_MODE*/
    "Nút Xanh: Tăng",                                                            /*THRESHOLD_UPDATE_GREEN*/
    "Nút Đỏ: Giảm",                                                              /*THRESHOLD_UPDATE_RED*/
    "Nút Trắng: Kế Tiếp",                                                        /*THRESHOLD_UPDATE_WHITE*/
    "Giữ Nút Xanh: Lưu",                                                         /*THRESHOLD_UPDATE_PRESSGREEN*/
    "CÀI ĐẶT NGƯỠNG",                                                            /*THRESHOLD_WIRELESS_MODE*/
    "Đang Cài Đặt....",                                                          /*THRESHOLD_WIRELESS_WAITING*/
    "Cài Đặt Thành Công",                                                        /*THRESHOLD_WIRELESS_FINSHED*/
    "CÀI ĐẶT NGÔN NGỮ",                                                          /*LANGUAGE_MODE*/
    "CÀI ĐẶT WIFI",                                                              /*WIFI_SETTING_MODE*/
    "Tên Wifi: ",                                                                /*WIFI_SETTING_ID*/
    "Mật khẩu: ",                                                                /*WIFI_SETTING_PASSWORD*/
    "ID RAPID: ",                                                                /*WIFI_SETTING_ID_DEVICE*/
    "Cài Đặt Thành Công!",                                                       /*WIFI_SETTING_FINISHED*/
    "CẬP NHẬT RAPID",                                                            /*UPDATE_MODE*/
    "Đang Cập Nhật ....",                                                        /*UPDATE_LOADING*/
    "Thành Công!",                                                               /*UPDATE_FINISHED*/
};

static const char *language_english[] = {
    "Press Green to Start",                                                    /*SCREEN_START*/
    "SELECT TUBE",                                                             /*CHOOSE_TUBE_MODE*/
    "SELECT SAMPLE",                                                           /*CHOOSE_SAMPLE_MODE*/
    "SELECT ANIMAL",                                                           /*CHOOSE_ANIMAL_MODE*/
    "Prawn",                                                                   /*CHOOSE_ANIMAL_PRAWN*/
    "Fish",                                                                    /*CHOOSE_ANIMAL_FISH*/
    "Pig",                                                                     /*CHOOSE_ANIMAL_PIG*/
    "In process",                                                              /*WAITING_READ_SENSOR*/
    "Waiting...",                                                              /*WAITING_PROCESS*/
    "Put tube inside and close the lid",                                       /*PREPARE_PUT_TUBE*/
    "Press Red to Measure!",                                                   /*PREPARE_NOTIFICATION*/
    "Tube: ",                                                                  /*RESULT_SICK*/
    "Result",                                                                  /*RESULT_AVERAGE*/
    "Green: Redo",                                                             /*RESULT_FEATURE_GREEN*/
    "Red: Finish",                                                             /*RESULT_FEATURE_RED*/
    "Press the Red to Restart",                                             /*RESULT_ERROR*/
    "Sorry, the machine has sensor noise, needs to restart to operate stably", /*RESULT_NOISE_CONTENT*/
    "CALIBRATION MODE",                                                        /*CALIB_MODE*/
    "Tube:   ",                                                                /*CALIB_SAMPLE*/
    " Highest ",                                                               /*CALIB_SAMPLE_MAX*/
    " Lowest ",                                                                /*CALIB_SAMPLE_MIN*/
    "Green: Exit",                                                             /*CALIB_FEATURE_GREEN*/
    "Red: Calibration",                                                        /*CALIB_FEATURE_RED*/
    "White: Clear Calibration",                                                /*CALIB_FEATURE_WHITE*/
    "Calibrating!!!",                                                          /*CALIB_WAITING_TITILE*/
    "Waiting...",                                                              /*CALIB_WAITING_PLEASE*/
    "Success!!!",                                                              /*CALIB_COMPLETE*/
    "LOGIN CALIB",                                                             /*CALIB_LOGIN_MODE*/
    "Green: Up",                                                               /*CALIB_LOGIN_UPDATE_HOLDGREEN*/
    "Red: Down",                                                               /*CALIB_LOGIN_UPDATE_WHITE*/
    "White: Next",                                                             /*CALIB_LOGIN_UPDATE_RED*/
    "Hold Green: Save",                                                        /*CALIB_LOGIN_UPDATE_GREEN*/
    "RAPID SETTING",                                                           /*SETTING_MODE*/
    "Language",                                                                /*SETTING_LANGUAGE*/
    "Animals",                                                                 /*SETTING_SAMPLE*/
    "Wifi",                                                                    /*SETTING_WIFI*/
    "Update",                                                                  /*SETTING_UPDATE*/
    "Threshold",                                                               /*SETTING_THRESHOLD*/
    "THRESHOLD SETTING",                                                       /*THRESHOLD_MODE*/
    "Manual",                                                                  /*THRESHOLD_MANUAL*/
    "Wireless",                                                                /*THRESHOLD_WIRELESS*/
    "Red: Wireless",                                                           /*THRESHOLD_FEATURE_RED*/
    "Green: Manual",                                                           /*THRESHOLD_FEATURE_GREEN*/
    "White: Exit",                                                             /*THRESHOLD_FEATURE_WHITE*/
    "VALUE SETTING",                                                           /*THRESHOLD_UPDATE_MODE*/
    "Green: Up",                                                               /*THRESHOLD_UPDATE_GREEN*/
    "Red: Down",                                                               /*THRESHOLD_UPDATE_RED*/
    "White: Next",                                                             /*THRESHOLD_UPDATE_WHITE*/
    "Hold Green: Save",                                                        /*THRESHOLD_UPDATE_PRESSGREEN*/
    "THRESHOLD SETTING",                                                       /*THRESHOLD_WIRELESS_MODE*/
    "Waiting...",                                                              /*THRESHOLD_WIRELESS_WAITING*/
    "Successful...!",                                                          /*THRESHOLD_WIRELESS_FINSHED*/
    "LANGUAGE SETTING",                                                        /*LANGUAGE_MODE*/
    "WIFI SETTING",                                                            /*WIFI_SETTING_MODE*/
    "Wifi Name: ",                                                             /*WIFI_SETTING_ID*/
    "Password: ",                                                              /*WIFI_SETTING_PASSWORD*/
    "RAPID ID: ",                                                              /*WIFI_SETTING_ID_DEVICE*/
    "Successful!",                                                             /*WIFI_SETTING_FINISHED*/
    "UPDATE RAPID",                                                            /*UPDATE_MODE*/
    "Updating ....",                                                           /*UPDATE_LOADING*/
    "Success!",                                                                /*UPDATE_FINISHED*/
};

static const char *language_chinese[] = {
    "按绿色键启动",                                 /*SCREEN_START*/
    "选择试管",                                     /*CHOOSE_TUBE_MODE*/
    "选择样品",                                     /*CHOOSE_SAMPLE_MODE*/
    "SELECT ANIMAL",                                /* CHOOSE_ANIMAL_MODE*/
    "Prawn",                                        /* CHOOSE_ANIMAL_PRAWN*/
    "Fish",                                         /* CHOOSE_ANIMAL_FISH*/
    "Pig",                                          /* CHOOSE_ANIMAL_PIG*/
    "进行中",                                       /*WAITING_READ_SENSOR*/
    "请稍等 ...",                                   /*WAITING_PROCESS*/
    "将试管放入并盖上 盖子",                        /*PREPARE_PUT_TUBE*/
    "按红色键开 始检测!",                           /*PREPARE_NOTIFICATION*/
    "试管:  ",                                      /*RESULT_SICK*/
    "平均值",                                       /*RESULT_AVERAGE*/
    "绿色: 重做 ",                                  /*RESULT_FEATURE_GREEN*/
    "红色: 完成",                                   /*RESULT_FEATURE_RED*/
    "按红色按钮重启",                               /*RESULT_ERROR*/
    "抱歉, 机器传感器有噪音, 需要重启 才能稳定运行", /*RESULT_NOISE_CONTENT*/
    "CALIBRATION MODE",                             /*CALIB_MODE*/
    "Tube:   ",                                     /*CALIB_SAMPLE*/
    " Highest ",                                    /*CALIB_SAMPLE_MAX*/
    " Lowest ",                                     /*CALIB_SAMPLE_MIN*/
    "Green: Exit",                                  /*CALIB_FEATURE_GREEN*/
    "Red: Calibration",                             /*CALIB_FEATURE_RED*/
    "White: Clear Calibration",                     /*CALIB_FEATURE_WHITE*/
    "Calibrating!!!",                               /*CALIB_WAITING_TITILE*/
    "Waiting...",                                   /*CALIB_WAITING_PLEASE*/
    "Success!!!",                                   /*CALIB_COMPLETE*/
    "LOGIN CALIB",                                  /*CALIB_LOGIN_MODE*/
    "Green: Up",                                    /*CALIB_LOGIN_UPDATE_HOLDGREEN*/
    "Red: Down",                                    /*CALIB_LOGIN_UPDATE_WHITE*/
    "White: Next",                                  /*CALIB_LOGIN_UPDATE_RED*/
    "Hold Green: Save",                             /*CALIB_LOGIN_UPDATE_GREEN*/
    "设置",                                         /*SETTING_MODE*/
    "语言",                                         /*SETTING_LANGUAGE*/
    "动物",                                         /*SETTING_SAMPLE*/
    "WIFI",                                         /*SETTING_WIFI*/
    "更新",                                         /*SETTING_UPDATE*/
    "阈值",                                         /*SETTING_THRESHOLD*/
    "THRESHOLD SETTING",                            /*THRESHOLD_MODE*/
    "Manual",                                       /*THRESHOLD_MANUAL*/
    "Wireless",                                     /*THRESHOLD_WIRELESS*/
    "Red: Wireless",                                /*THRESHOLD_FEATURE_RED*/
    "Green: Manual",                                /*THRESHOLD_FEATURE_GREEN*/
    "White: Exit",                                  /*THRESHOLD_FEATURE_WHITE*/
    "VALUE SETTING",                                /*THRESHOLD_UPDATE_MODE*/
    "Green: Up",                                    /*THRESHOLD_UPDATE_GREEN*/
    "Red: Down",                                    /*THRESHOLD_UPDATE_RED*/
    "White: Next",                                  /*THRESHOLD_UPDATE_WHITE*/
    "Hold Green: Save",                             /*THRESHOLD_UPDATE_PRESSGREEN*/
    "THRESHOLD SETTING",                            /*THRESHOLD_WIRELESS_MODE*/
    "Waiting...",                                   /*THRESHOLD_WIRELESS_WAITING*/
    "Successful...!",                               /*THRESHOLD_WIRELESS_FINSHED*/
    "语言设置",                                     /*LANGUAGE_MODE*/
    "WiFi SETTINGS",                                /*WIFI_SETTING_MODE*/
    "WiFi: ",                                       /*WIFI_SETTING_ID*/
    "Password: ",                                   /*WIFI_SETTING_PASSWORD*/
    "ID: ",                                         /*WIFI_SETTING_ID_DEVICE*/
    "Installation Successful!",                     /*WIFI_SETTING_FINISHED*/
    "更新 RAPID",                                   /*UPDATE_MODE*/
    "Updating ....",                                /*UPDATE_LOADING*/
    "Success!",                                     /*UPDATE_FINISHED*/
};

static const char **language_RD[] = {language_vietnamese,
                                     language_english,
                                     language_chinese,
                                     language_chinese};

/**
 * @brief Language convert array/ Language Setting
 * Thay đổi ngôn ngữ trong giao diện cài đặt ngôn phù hợp
 * với ngôn ngữ đã chọn
 */
static const char *Vietnamese_Convert[] = {"Tiếng Việt", "Tiếng Anh", "Tiếng Trung", "Tiếng Đài"};
static const char *English_convert[] = {"Vietnamese", "English", "Chinese", "Taiwanese"};
// static const char *Chinese_convert[] = {"Vietnamese", "English", "Chinese", "Taiwanese"};
static const char *Chinese_convert[] = {"越南语", "英语", "中文", "台湾语"};
static const char *Taiwanese_convert[] = {"越南语", "英语", "中文", "台湾语"};
static const char **language_convert[] = {Vietnamese_Convert,
                                          English_convert,
                                          Chinese_convert,
                                          Taiwanese_convert};

/**
 * @brief  Feature button menu with multilingual
 * Chức năng nút trong menu với ngôn ngữ phù ngữ
 */
static const char *redMenu_feature[] = {"Nút Đỏ: Xuống", "Red: Down", "红色: 下一个", "红色: 下一个"};
static const char *greenMenu_feature[] = {"Nút Xanh: Chọn", "Green: Choose", "绿色: 选择", "绿色: 选择"};
static const char *whiteMenu_feature[] = {"Nút Trắng: Lên", "White: Up", "白色: 上一个", "白色: 上一个"};

/**/
static const char *greenHold_Group[] = {"Giữ Xanh: Nhóm", "Hold Green: Grouping", "按住绿色按钮：分组", "按住绿色按钮：分组"};
static const char *greenHold_Ugroup[] = {"Giữ Xanh: Bỏ Nhóm", "Hold Green: Ungrouping", "按住绿色按钮：取消分组", "按住绿色按钮：取消分组"};

static const char *language_sickName_positive[] = {"Chứng Dương", "Positive", "阳性", "阳性"};
static const char *language_sickName_WSSV[] = {"Đốm Trắng", "WSSV", "WSSV", "WSSV"};

/* Sample name with multilingual */
static const char *language_sampleVannamei[] = {"Tôm Thẻ", "P.Vannamei", "P.Vannamei", "P.Vannamei"};
static const char *language_sampleMonodon[] = {"Tôm Sú", "P.Monodon", "P.Monodon", "P.Monodon"};
static const char *language_sampleTilapia[] = {"Cá Rô Phi", "Tilapia", "Tilapia", "Tilapia"};
static const char *language_samplePig[] = {"Heo", "Pig", "Pig", "Pig"};
static const char *language_sampleWater[] = {"Nước", "Water", "Water", "Water"};

/* Icon Menu Setting */
static const unsigned char *iconLanguage[] = {VIE, ENG, CHI, TAW};
static const unsigned char *iconWifi[] = {image_wifi_connected_bits, image_wifi_not_connected_bits};
static const unsigned char *iconThreshold[] = {ThresholdIcon};
static const unsigned char *iconUpdate[] = {UpdateIcon};

/* Icon Empty 32x32 */
static const unsigned char *iconEmpty[] = {empty_icon};

/* Icon Samples scale 32x32
 * Muốn thêm icon mới thì thêm file ảnh vào thư mục image và scale về 32x32
 */
static const unsigned char *iconVannamei[] = {prawn32x32};
static const unsigned char *iconMonodon[] = {prawn32x32};
static const unsigned char *iconTilapia[] = {fish32x32};
static const unsigned char *iconPig[] = {pig32x32};
static const unsigned char *iconWater[] = {water32x32};
static const unsigned char *iconSample[] = {prawn32x32, fish32x32, pig32x32, water32x32};

/* Name of Animal with multilingual */
static const char *prawn_1[] = {"Tôm Thẻ", "P.Vannamei", "白对虾", "白对虾"};
static const char *prawn_2[] = {"Tôm Sú", "P.Monodon", "斑节对虾", "斑节对虾"};
static const char *water[] = {"Nước", "Water", "水", "水"};
static const char *fish[] = {"Cá Rô Phi", "Tilapia", "罗非鱼", "罗非鱼"};
static const char *pig[] = {"Heo", "Pig", "猪", "猪"};
#endif
