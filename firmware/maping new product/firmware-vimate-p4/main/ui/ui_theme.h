/**
 * ui_theme.h — token màu / font / bo góc / khoảng cách / kích thước chạm cho UI thiết bị.
 *
 * Nguồn sự thật: design-system/vimate-device-ui/MASTER.md (§2–§5). Sửa token ở đây
 * thì cập nhật MASTER.md (và ngược lại). Chỉ #define — không tốn RAM/flash.
 *
 * Dùng: lv_obj_set_style_bg_color(obj, lv_color_hex(UI_CLR_PRIMARY), 0);
 *       lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, 0);
 *
 * Tương phản WCAG (chữ/nền) ghi ở cột phải — mọi cặp dùng cho chữ phải ≥ 4,5:1.
 * Code mới KHÔNG viết lv_color_hex(0x...) trực tiếp; thiếu màu → thêm token ở đây.
 */
#pragma once
#include "lvgl.h"
#include "boards/board.h"
#include "fonts/lv_font_vimate.h"

/* ================= Màu (0xRRGGBB) ================= */
/* Nền / chữ */
#define UI_CLR_BG               0xEFF6FF /* nền màn Home / tiện ích                     */
#define UI_CLR_CARD             0xFFFFFF /* thẻ, sheet, bottom bar                       */
#define UI_CLR_FG               0x0F172A /* chữ chính, tiêu đề        — 17,9:1 trên CARD */
#define UI_CLR_FG_MUTED         0x475569 /* phụ đề, hint, nhãn phụ    —  7,6:1 trên CARD */
#define UI_CLR_BORDER           0xE4ECFC /* viền thẻ 2–3 px, đường kẻ                    */
#define UI_CLR_OVERLAY          0x000000 /* nền mờ 70 % dưới reward                      */

/* Primary — xanh học tập */
#define UI_CLR_PRIMARY          0x2563EB /* nút chính, icon bar       — trắng 5,2:1      */
#define UI_CLR_PRIMARY_PRESSED  0x1D4ED8 /* nền nút chính khi nhấn    — trắng 6,7:1      */
#define UI_CLR_PRIMARY_SOFT     0xDBEAFE /* nền PRESSED của ô trong suốt                 */
#define UI_CLR_PRIMARY_DARK     0x1E3A8A /* nhãn bar, chữ trên PRIMARY_SOFT — 8,5:1      */
#define UI_CLR_ON_PRIMARY       0xFFFFFF

/* Secondary — vàng sao / tiến độ */
#define UI_CLR_SECONDARY        0xF59E0B /* sao, huy hiệu, progress   — chữ FG 8,3:1;
                                            KHÔNG đặt chữ trắng lên (2,2:1)             */
#define UI_CLR_ON_SECONDARY     0x0F172A

/* Accent — hồng khen thưởng */
#define UI_CLR_ACCENT           0xEC4899 /* "Chính xác!", confetti    — chữ đen 6,0:1;
                                            trắng chỉ 3,5:1 → chỉ chữ ≥ 24 px đậm       */
#define UI_CLR_ACCENT_DARK      0xBE185D /* chữ accent trên trắng     — 6,0:1            */

/* Success — xanh lá */
#define UI_CLR_SUCCESS          0x15803D /* FAB Home, "Đã uống", đáp án đúng — trắng 5,0:1 */
#define UI_CLR_SUCCESS_PRESSED  0x166534 /* khi nhấn                  — trắng 7,1:1      */
#define UI_CLR_SUCCESS_DEEP     0x14532D /* đầu dưới gradient khi nhấn                    */
#define UI_CLR_SUCCESS_SOFT     0xDCFCE7 /* nền ô đáp án đúng         — chữ SUCCESS 4,6:1 */

/* Destructive — đỏ */
#define UI_CLR_DESTRUCTIVE      0xDC2626 /* sai, mất tim, lỗi         — trắng 4,8:1      */
#define UI_CLR_DESTRUCTIVE_DARK 0xB91C1C /* chữ trên DESTRUCTIVE_SOFT — 5,3:1            */
#define UI_CLR_DESTRUCTIVE_SOFT 0xFEE2E2 /* nền ô đáp án sai (DC2626 trên nền này chỉ 4,0) */

/* Màn Nhắc (alarm) — giữ nguyên bảng màu ấm đang dùng */
#define UI_CLR_WARN_BG          0xFFE9C7 /* gradient trên                                */
#define UI_CLR_WARN_BG2         0xFFF7ED /* gradient dưới                                */
#define UI_CLR_WARN_FG          0x7C2D12 /* chữ                       — 9,4:1 trên trắng */
#define UI_CLR_WARN_SHADOW      0xB45309

/* Màn Lịch học */
#define UI_CLR_SKY_BG           0x89CBE3 /* nền — chữ chỉ đặt trên sheet trắng           */

/* Sao khen thưởng */
#define UI_CLR_STAR_FULL        UI_CLR_SECONDARY
#define UI_CLR_STAR_EMPTY       0x64748B /* sao rỗng trên overlay tối                    */

/* ================= Font theo vai trò ================= */
#define UI_FONT_COUNTDOWN  (&lv_font_countdown_72)   /* số đếm ngược                      */
#define UI_FONT_TITLE      (&lv_font_vimate_48)      /* tiêu đề màn, lời chào             */
#define UI_FONT_BODY       (&lv_font_vimate_24)      /* nội dung, câu hỏi, đáp án, nút    */
#define UI_FONT_LABEL      (&lv_font_vimate_18)      /* TỐI THIỂU cho chữ có thể chạm/hint */
#define UI_FONT_META       (&lv_font_vimate_14)      /* metadata không cần đọc kỹ         */
#define UI_FONT_SYMBOL     (&lv_font_montserrat_18)  /* chỉ LV_SYMBOL_* (vimate không có) */

/* ================= Bo góc (px) ================= */
#define UI_R_CHIP   12
#define UI_R_BTN    16
#define UI_R_TILE   20
#define UI_R_BAR    22
#define UI_R_CARD   24
#define UI_R_SHEET  32

/* ================= Khoảng cách (px, cơ số 4) ================= */
#define UI_SP_1   4
#define UI_SP_2   8
#define UI_SP_3   12
#define UI_SP_4   16
#define UI_SP_6   24
#define UI_SP_8   32
#define UI_SP_12  48

/* ================= Kích thước chạm (≈ 7,5 mm cạnh ngắn, theo mật độ board) ========
 * P4 4.3" 800×480 ≈ 8,5 px/mm → 64 px; S3 3.5" 480×320 ≈ 6,5 px/mm → 48 px;
 * S3 2.8" 320×240 ≈ 5,6 px/mm → 42 px. 44 px "chuẩn web" trên P4 chỉ ≈ 5,2 mm. */
#if BOARD_LCD_H_RES >= 800
#define UI_TOUCH_MIN  64
#define UI_TOUCH_GAP  12
#define UI_BTN_W      160
#define UI_BTN_H      64
#elif BOARD_LCD_H_RES >= 480
#define UI_TOUCH_MIN  48
#define UI_TOUCH_GAP  8
#define UI_BTN_W      120
#define UI_BTN_H      48
#else
#define UI_TOUCH_MIN  42
#define UI_TOUCH_GAP  8
#define UI_BTN_W      104
#define UI_BTN_H      42
#endif
