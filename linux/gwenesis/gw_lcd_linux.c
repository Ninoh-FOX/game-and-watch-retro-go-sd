#include "gw_lcd.h"
#include "gw_audio.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

uint16_t framebuffer1[GW_LCD_WIDTH * GW_LCD_HEIGHT];
uint16_t framebuffer2[GW_LCD_WIDTH * GW_LCD_HEIGHT];

uint32_t active_framebuffer;
uint32_t frame_counter;
static uint32_t last_refresh_hz = 60;

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static bool swap_pending;

void gwenesis_sdl_set_video(SDL_Window *window, SDL_Renderer *renderer)
{
    g_window = window;
    g_renderer = renderer;
    if (g_texture)
        SDL_DestroyTexture(g_texture);
    g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  GW_LCD_WIDTH, GW_LCD_HEIGHT);
    if (!g_texture)
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
}

void lcd_deinit(void *unused_spi)
{
    (void)unused_spi;
    if (g_texture) {
        SDL_DestroyTexture(g_texture);
        g_texture = NULL;
    }
}

void lcd_init(void *spi, void *ltdc, lcd_init_flags_t flags)
{
    (void)spi;
    (void)ltdc;
    active_framebuffer = 0;
    frame_counter = 0;
    if (flags & LCD_INIT_CLEAR_BUFFERS)
        lcd_clear_buffers();
}

void *lcd_clear_active_buffer(void)
{
    void *p = lcd_get_active_buffer();
    memset(p, 0, GW_LCD_WIDTH * GW_LCD_HEIGHT * sizeof(uint16_t));
    return p;
}

void *lcd_clear_inactive_buffer(void)
{
    void *p = lcd_get_inactive_buffer();
    memset(p, 0, GW_LCD_WIDTH * GW_LCD_HEIGHT * sizeof(uint16_t));
    return p;
}

void lcd_clear_buffers(void)
{
    memset(framebuffer1, 0, sizeof(framebuffer1));
    memset(framebuffer2, 0, sizeof(framebuffer2));
}

uint8_t lcd_backlight_get(void) { return 255; }
void lcd_backlight_set(uint8_t brightness) { (void)brightness; }
void lcd_backlight_on(void) {}
void lcd_backlight_off(void) {}

void *lcd_get_active_buffer(void)
{
    return active_framebuffer ? framebuffer2 : framebuffer1;
}

void *lcd_get_inactive_buffer(void)
{
    return active_framebuffer ? framebuffer1 : framebuffer2;
}

void lcd_set_buffers(uint16_t *buf1, uint16_t *buf2)
{
    (void)buf1;
    (void)buf2;
}

void lcd_swap(void)
{
    active_framebuffer ^= 1u;
    frame_counter++;
    swap_pending = true;

    dma_counter++;

    if (g_texture && g_renderer) {
        void *pixels;
        int pitch;
        uint16_t *src = (uint16_t *)lcd_get_inactive_buffer();
        if (SDL_LockTexture(g_texture, NULL, &pixels, &pitch) == 0) {
            if (pitch == (int)(GW_LCD_WIDTH * 2))
                memcpy(pixels, src, GW_LCD_WIDTH * GW_LCD_HEIGHT * 2);
            else {
                for (int y = 0; y < GW_LCD_HEIGHT; y++)
                    memcpy((uint8_t *)pixels + y * pitch,
                           src + y * GW_LCD_WIDTH, GW_LCD_WIDTH * 2);
            }
            SDL_UnlockTexture(g_texture);
        }
        SDL_RenderClear(g_renderer);
        SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
        SDL_RenderPresent(g_renderer);
    }
    swap_pending = false;
}

void lcd_sync(void) {}

void lcd_clone(void) {}

void lcd_wait_for_vblank(void)
{
    SDL_Delay(1);
}

uint32_t lcd_is_swap_pending(void) { return swap_pending ? 1u : 0u; }

bool lcd_sleep_while_swap_pending(void)
{
    swap_pending = false;
    return false;
}

void lcd_reset_active_buffer(void) {}

uint32_t lcd_get_frame_counter(void) { return frame_counter; }

uint32_t lcd_get_pixel_position(void)
{
    /* Same as GWENESIS_AUDIOSYNC_START_LCD_LINE in main_gwenesis.c */
    return 248;
}

void lcd_set_dithering(uint32_t enable) { (void)enable; }

void lcd_set_refresh_rate(uint32_t frequency)
{
    last_refresh_hz = frequency ? frequency : 60;
}

uint32_t lcd_get_last_refresh_rate(void) { return last_refresh_hz; }
