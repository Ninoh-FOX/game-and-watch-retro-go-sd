#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_main_pico8(uint8_t load_state, uint8_t start_paused, int8_t save_slot);

#ifdef __cplusplus
}
#endif
