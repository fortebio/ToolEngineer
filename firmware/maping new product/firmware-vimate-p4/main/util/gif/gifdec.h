#ifndef GIFDEC_H
#define GIFDEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

#include <stdint.h>

typedef struct _gd_Palette {
    int size;
    uint8_t colors[0x100 * 3];
} gd_Palette;

typedef struct _gd_GCE {
    uint16_t delay;
    uint8_t tindex;
    uint8_t disposal;
    int input;
    int transparency;
} gd_GCE;



typedef struct _gd_GIF {
    lv_fs_file_t fd;
    const char * data;
    uint8_t is_file;
    uint32_t f_rw_p;
    int32_t anim_start;
    uint16_t width, height;
    uint16_t depth;
    int32_t loop_count;
    gd_GCE gce;
    gd_Palette * palette;
    gd_Palette lct, gct;
    void (*plain_text)(
        struct _gd_GIF * gif, uint16_t tx, uint16_t ty,
        uint16_t tw, uint16_t th, uint8_t cw, uint8_t ch,
        uint8_t fg, uint8_t bg
    );
    void (*comment)(struct _gd_GIF * gif);
    void (*application)(struct _gd_GIF * gif, char id[8], char auth[3]);
    uint16_t fx, fy, fw, fh;
    uint8_t bgindex;
    uint8_t first_frame;
    uint8_t * canvas, * frame;
#if LV_GIF_CACHE_DECODE_DATA
    uint8_t *lzw_cache;
#endif
    /* VIMATE 15/09/2026: hook vẽ thẳng (lvgl_gif opaque). Khi khác NULL, gd_render_frame
     * và dispose() KHÔNG động vào canvas ARGB mà gọi hook với rect khung hiện tại:
     *   fill_index < 0  → tô index từ gif->frame (bỏ pixel trong suốt, disposal 0/1)
     *   fill_index >= 0 → tô cả rect bằng màu palette[fill_index] (disposal 2 về nền)
     * Hook đọc gif->palette (GCT/LCT của khung), gif->frame, gif->width. */
    void (*render_hook)(struct _gd_GIF * gif, void * user, int fx, int fy, int fw, int fh,
                        int fill_index);
    void * render_user;
} gd_GIF;

gd_GIF * gd_open_gif_file(const char * fname);

gd_GIF * gd_open_gif_data(const void * data);

void gd_render_frame(gd_GIF * gif, uint8_t * buffer);

int gd_get_frame(gd_GIF * gif);
void gd_rewind(gd_GIF * gif);
void gd_close_gif(gd_GIF * gif);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GIFDEC_H */
