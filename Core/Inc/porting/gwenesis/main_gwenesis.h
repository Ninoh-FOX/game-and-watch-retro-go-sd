#pragma once

int app_main_gwenesis(uint8_t load_state, uint8_t start_paused, int8_t save_slot);
void gwenesis_save_local_data(FILE *file);
void gwenesis_load_local_data(FILE *file);
