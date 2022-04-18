#ifndef _MAIN_MSX_H_
#define _MAIN_MSX_H_
extern int msx_button_a_key_index;
extern int msx_button_b_key_index;
void app_main_msx(uint8_t load_state, uint8_t start_paused);
void save_gnw_msx_data();
void load_gnw_msx_data();
#endif