#include "odroid_overlay.h"
#include <stdio.h>

int odroid_overlay_draw_text(uint16_t x, uint16_t y, uint16_t width, const char *text, uint16_t color, uint16_t color_bg)
{
    (void)x;
    (void)y;
    (void)width;
    (void)color;
    (void)color_bg;
    if (text)
        printf("[overlay] %s\n", text);
    return 0;
}
