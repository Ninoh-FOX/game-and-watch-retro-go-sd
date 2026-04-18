#include <stdint.h>
#include <stdbool.h>

typedef void (*file_progress_cb_t)(uint32_t total_size, uint32_t total_processed, uint8_t progress);

void flash_alloc_reset();
uint8_t *store_file_in_flash(const char *file_path, uint32_t *file_size_p, bool byte_swap, file_progress_cb_t progress_cb);
