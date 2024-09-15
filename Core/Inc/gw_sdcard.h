#ifndef _SDCARD_H_
#define _SDCARD_H_

#include "stm32h7xx_hal.h"

extern bool fs_mounted;

void sdcard_init(void);
void sdcard_error_screen(void);

#endif
