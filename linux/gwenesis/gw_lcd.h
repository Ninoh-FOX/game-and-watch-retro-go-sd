/* LCD API compatible with Core/Inc/gw_lcd.h — Linux / SDL implementation */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GW_LCD_WIDTH  320
#define GW_LCD_HEIGHT 240

extern uint16_t framebuffer1[GW_LCD_WIDTH * GW_LCD_HEIGHT];
extern uint16_t framebuffer2[GW_LCD_WIDTH * GW_LCD_HEIGHT];
typedef uint16_t pixel_t;

typedef enum {
   LCD_INIT_CLEAR_BUFFERS = 1 << 0
} lcd_init_flags_t;

extern uint32_t active_framebuffer;
extern uint32_t frame_counter;

void lcd_deinit(void *unused_spi);
void lcd_init(void *spi, void *ltdc, lcd_init_flags_t flags);
void *lcd_clear_active_buffer(void);
void *lcd_clear_inactive_buffer(void);
void lcd_clear_buffers(void);
uint8_t lcd_backlight_get(void);
void lcd_backlight_set(uint8_t brightness);
void lcd_backlight_on(void);
void lcd_backlight_off(void);
void lcd_swap(void);
void lcd_sync(void);
void lcd_clone(void);
void *lcd_get_active_buffer(void);
void *lcd_get_inactive_buffer(void);
void lcd_set_buffers(uint16_t *buf1, uint16_t *buf2);
void lcd_wait_for_vblank(void);
uint32_t lcd_is_swap_pending(void);
bool lcd_sleep_while_swap_pending(void);
void lcd_reset_active_buffer(void);
uint32_t lcd_get_frame_counter(void);
uint32_t lcd_get_pixel_position(void);
void lcd_set_dithering(uint32_t enable);
void lcd_set_refresh_rate(uint32_t frequency);
uint32_t lcd_get_last_refresh_rate(void);
