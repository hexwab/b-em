/*B-em v2.2 by Tom Walker
  Allegro video code*/
#include <allegro5/allegro_primitives.h>
#include "b-em.h"
#include "pal.h"
#include "serial.h"
#include "tape.h"
#include "video.h"
#include "video_render.h"

enum vid_disptype vid_dtype_user, vid_dtype_intern;
bool vid_pal;
int vid_fskipmax = 1;
int vid_fullborders = 1;

static int fskipcount;

int vid_savescrshot = 0;
char vid_scrshotname[260];

int winsizex, winsizey;
int scr_x_start, scr_x_size, scr_y_start, scr_y_size;

bool vid_print_mode = false;

void video_close()
{
    al_destroy_bitmap(b32);
    al_destroy_bitmap(b16);
    al_destroy_bitmap(b);
}

#ifdef WIN32
static const int y_fudge = 0;
#else
static const int y_fudge = 28;
#endif

void video_enterfullscreen()
{
    ALLEGRO_DISPLAY *display;
    ALLEGRO_COLOR black;
    int value;
    double aspect;

    display = al_get_current_display();
    if (al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, true)) {
        black = al_map_rgb(0, 0, 0);
        winsizex = al_get_display_width(display);
        winsizey = al_get_display_height(display);
        aspect = (double)winsizex / (double)winsizey;
        if (aspect > (4.0 / 3.0)) {
            value = 4 * winsizey / 3;
            scr_x_start = (winsizex - value) / 2;
            scr_y_start = 0;
            scr_x_size = value;
            scr_y_size = winsizey;
            al_set_target_backbuffer(display);
            // fill the gap between the left screen edge and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, black);
            // fill the gap between the BBC image and the right screen edge.
            al_draw_filled_rectangle(scr_x_start + value, 0, winsizex, winsizey, black);
        }
        else {
            value = 3 * winsizex / 4;
            scr_x_start = 0;
            scr_y_start = (winsizey - value) / 2;
            scr_x_size = winsizex;
            scr_y_size = value;
            // fill the gap between the top of the screen and the BBC image.
            al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, black);
            // fill the gap between the BBC image and the bottom of the screen.
            al_draw_filled_rectangle(0, scr_y_start + value, winsizex, winsizey, black);
        }
    } else {
        log_error("vidalleg: could not set graphics mode to full-screen");
        fullscreen = 0;
    }
}

void video_set_window_size(bool fudge)
{
    scr_x_start = 0;
    scr_y_start = 0;

    switch(vid_fullborders) {
        case 0:
            scr_x_size = BORDER_NONE_X_END_GRA - BORDER_NONE_X_START_GRA;
            scr_y_size = (BORDER_NONE_Y_END_GRA - BORDER_NONE_Y_START_GRA) * 2;
            break;
        case 1:
            scr_x_size = BORDER_MED_X_END_GRA - BORDER_MED_X_START_GRA;
            scr_y_size = (BORDER_MED_Y_END_GRA - BORDER_MED_Y_START_GRA) * 2;
            break;
        case 2:
            scr_x_size = BORDER_FULL_X_END_GRA - BORDER_FULL_X_START_GRA;
            scr_y_size = (BORDER_FULL_Y_END_GRA - BORDER_FULL_Y_START_GRA) * 2;
    }
    winsizex = scr_x_size;
    winsizey = fudge ? scr_y_size + y_fudge : scr_y_size;
    log_debug("vidalleg: video_set_window_size, scr_x_size=%d, scr_y_size=%d, fudgedy=%d", scr_x_size, scr_y_size, winsizey);
}

void video_set_borders(int borders)
{
    vid_fullborders = borders;
    video_set_window_size(false);
    al_resize_display(al_get_current_display(), winsizex, winsizey);
}

void video_update_window_size(ALLEGRO_EVENT *event)
{
    if (!fullscreen) {
        scr_x_start = 0;
        scr_x_size = winsizex = event->display.width;
        scr_y_start = 0;
        scr_y_size = winsizey = event->display.height;
        log_debug("vidalleg: video_update_window_size, scr_x_size=%d, scr_y_size=%d", scr_x_size, scr_y_size);
    }
    al_acknowledge_resize(event->display.source);
}

void video_leavefullscreen(void)
{
    ALLEGRO_DISPLAY *display;

    display = al_get_current_display();
    al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, false);
    scr_x_start = 0;
    scr_x_size = winsizex = al_get_display_width(display);
    scr_y_start = 0;
    scr_y_size = winsizey = al_get_display_height(display);
}

void video_toggle_fullscreen(void)
{
    if (fullscreen) {
        fullscreen = 0;
        video_leavefullscreen();
    } else {
        fullscreen = 1;
        video_enterfullscreen();
    }
}

static void upscale_only(ALLEGRO_BITMAP *src, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
    al_set_target_backbuffer(al_get_current_display());
    if (dw > sw+10 || dh > sh+10)
        al_draw_scaled_bitmap(src, sx, sy, sw, sh, dx, dy, dw, dh, 0);
    else {
        al_draw_bitmap_region(src, sx, sy, sw, sh, dx, dy, 0);
        if (dw > sw)
            al_draw_filled_rectangle(dx + sw, 0, dx + dw, dh, border_col);
        if (dh > sh)
            al_draw_filled_rectangle(0, dy + sh, dw, dy + dh, border_col);
    }
}

static void line_double(void)
{
    char *yptr1 = (char *)region->data + region->pitch * firsty * 2;
    char *yptr2 = yptr1 + region->pitch;
    size_t linesize = abs(region->pitch);

    for (int y = firsty; y < lasty; y++) {
        memcpy(yptr2, yptr1, linesize);
        yptr1 = yptr2 + region->pitch;
        yptr2 = yptr1 + region->pitch;
    }
}

static inline void save_screenshot(void)
{
    vid_savescrshot--;
    if (!vid_savescrshot) {
        int xsize = lastx - firstx;
        int ysize = lasty - firsty;
        ALLEGRO_BITMAP *scrshotb  = al_create_bitmap(xsize, ysize << 1);
        int c;

        if (vid_pal) {
            switch(vid_dtype_intern) {
                case VDT_SCALE:
                    pal_convert(firstx, firsty, lastx, lasty, 1);
                    al_set_target_bitmap(scrshotb);
                    al_draw_scaled_bitmap(b32, firstx, firsty, xsize, ysize, 0, 0, xsize, ysize << 1, 0);
                    break;
                case VDT_INTERLACE:
                    pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                    al_set_target_bitmap(scrshotb);
                    al_draw_bitmap_region(b32, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                    break;
                case VDT_SCANLINES:
                    pal_convert(firstx, firsty, lastx, lasty, 1);
                    al_set_target_bitmap(scrshotb);
                    c = 0;
                    for (int y = firsty; y < lasty; y++) {
                        al_draw_bitmap_region(b32, firstx, y, xsize, 1, 0, c, 0);
                        c += 2;
                    }
                    break;
                case VDT_LINEDOUBLE:
                    line_double();
                    pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                    al_set_target_bitmap(scrshotb);
                    al_draw_bitmap_region(b32, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                    break;
            }
        }
        else {
            al_set_target_bitmap(scrshotb);
            switch(vid_dtype_intern) {
                case VDT_SCALE:
                    al_unlock_bitmap(b);
                    al_set_target_bitmap(scrshotb);
                    al_draw_scaled_bitmap(b, firstx, firsty, xsize, ysize, 0, 0, xsize, ysize << 1, 0);
                    break;
                case VDT_INTERLACE:
                    al_unlock_bitmap(b);
                    al_set_target_bitmap(scrshotb);
                    al_draw_bitmap_region(b, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                    break;
                case VDT_SCANLINES:
                    al_unlock_bitmap(b);
                    al_set_target_bitmap(scrshotb);
                    c = 0;
                    for (int y = firsty; y < lasty; y++) {
                        al_draw_bitmap_region(b, firstx, y, xsize, 1, 0, c, 0);
                        c += 2;
                    }
                    break;
                case VDT_LINEDOUBLE:
                    line_double();
                    al_unlock_bitmap(b);
                    al_draw_scaled_bitmap(b, firstx, firsty << 1, xsize, ysize << 1, 0, 0, xsize, ysize << 1, 0);
                    break;
            }
            region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
        }
        al_save_bitmap(vid_scrshotname, scrshotb);
        al_destroy_bitmap(scrshotb);
    }
}

static inline void calc_limits(bool non_ttx, uint8_t vtotal)
{
    switch(vid_fullborders) {
        case 0:
            if (non_ttx) {
                firstx = BORDER_NONE_X_START_GRA;
                lastx  = BORDER_NONE_X_END_GRA;
            }
            else {
                firstx = BORDER_NONE_X_START_TTX;
                lastx  = BORDER_NONE_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_NONE_Y_START_GRA;
                lasty  = BORDER_NONE_Y_END_GRA;
            }
            else {
                firsty = BORDER_NONE_Y_START_TXT;
                lasty  = BORDER_NONE_Y_END_TXT;
            }
            break;
        case 1:
            if (non_ttx) {
                firstx = BORDER_MED_X_START_GRA;
                lastx  = BORDER_MED_X_END_GRA;
            }
            else {
                firstx = BORDER_MED_X_START_TTX;
                lastx  = BORDER_MED_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_MED_Y_START_GRA;
                lasty  = BORDER_MED_Y_END_GRA;
            }
            else {
                firsty = BORDER_MED_Y_START_TXT;
                lasty  = BORDER_MED_Y_END_TXT;
            }
            break;
        case 2:
            if (non_ttx) {
                firstx = BORDER_FULL_X_START_GRA;
                lastx  = BORDER_FULL_X_END_GRA;
            }
            else {
                firstx = BORDER_FULL_X_START_TTX;
                lastx  = BORDER_FULL_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_FULL_Y_START_GRA;
                lasty  = BORDER_FULL_Y_END_GRA;
            }
            else {
                firsty = BORDER_FULL_Y_START_TXT;
                lasty  = BORDER_FULL_Y_END_TXT;
            }
    }
}

static inline void blit_screen(void)
{
    int xsize = lastx - firstx;
    int ysize = lasty - firsty + 1;

    if (vid_pal) {
        switch(vid_dtype_intern) {
            case VDT_SCALE:
                pal_convert(firstx, firsty, lastx, lasty, 1);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(b32, firstx, firsty, xsize, ysize, scr_x_start, scr_y_start, scr_x_size, scr_y_size, 0);
                break;
            case VDT_INTERLACE:
                pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                upscale_only(b32, firstx, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                break;
            case VDT_SCANLINES:
                pal_convert(firstx, firsty, lastx, lasty, 1);
                al_set_target_bitmap(b16);
                al_clear_to_color(al_map_rgb(0, 0,0));
                for (int c = firsty; c < lasty; c++)
                    al_draw_bitmap_region(b32, firstx, c, lastx - firstx, 1, 0, c << 1, 0);
                upscale_only(b16, 0, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                break;
            case VDT_LINEDOUBLE:
                line_double();
                pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                upscale_only(b32, firstx, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                break;
        }
    }
    else {
        switch(vid_dtype_intern) {
            case VDT_SCALE:
                al_unlock_bitmap(b);
                al_set_target_backbuffer(al_get_current_display());
                al_draw_scaled_bitmap(b, firstx, firsty, xsize, ysize, scr_x_start, scr_y_start, scr_x_size, scr_y_size, 0);
                break;
            case VDT_INTERLACE:
                al_unlock_bitmap(b);
                upscale_only(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                break;
            case VDT_SCANLINES:
                al_unlock_bitmap(b);
                al_set_target_bitmap(b16);
                al_clear_to_color(border_col);
                for (int c = firsty; c < lasty; c++)
                    al_draw_bitmap_region(b, firstx, c, xsize, 1, 0, c << 1, 0);
                upscale_only(b16, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                break;
            case VDT_LINEDOUBLE:
                line_double();
                al_unlock_bitmap(b);
                upscale_only(b, firstx, firsty << 1, xsize, ysize  << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
        }
        region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, ALLEGRO_LOCK_READWRITE);
    }
}

static inline void fill_pillarbox(void)
{
    // fill the gap between the left screen edge and the BBC image.
    al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, border_col);
    // fill the gap between the BBC image and the right screen edge.
    al_draw_filled_rectangle(scr_x_start + scr_x_size, 0, winsizex, winsizey, border_col);
}

static inline void fill_letterbox(void)
{
    // fill the gap between the top of the screen and the BBC image.
    al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, border_col);
    // fill the gap between the BBC image and the bottom of the screen.
    al_draw_filled_rectangle(0, scr_y_start + scr_y_size, winsizex, winsizey, border_col);
}

void video_doblit(bool non_ttx, uint8_t vtotal)
{
    if (vid_savescrshot)
        save_screenshot();

    if (++fskipcount >= ((motor && fasttape) ? 5 : vid_fskipmax)) {
        lasty++;
        calc_limits(non_ttx, vtotal);
        fskipcount = 0;
        blit_screen();
        if (scr_x_start > 0)
            fill_pillarbox();
        else if (scr_y_start > 0)
            fill_letterbox();
        al_flip_display();
    }
    firstx = firsty = 65535;
    lastx  = lasty  = 0;
}
