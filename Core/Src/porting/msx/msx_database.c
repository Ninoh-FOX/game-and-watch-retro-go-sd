#include <string.h>
#include "msx_database.h"
#include "hw_sha1.h"

#define ENTRY_SIZE (SHA1_COMPACT_SIZE + 3) // 6 bytes of trunked sha1 + 3 bytes of configuration

static int8_t msx_find_rom_info(const uint8_t *target_sha1, RomInfo *result) {
    long low = 0;
    FILE *file = fopen("/bios/msx/msxromdb.bin", "rb");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long high = ftell(file) / ENTRY_SIZE - 1;

    // sha1 are sorted in database file, use binary search
    // to find the target sha1
    while (low <= high) {
        long mid = (low + high) / 2;

        fseek(file, mid * ENTRY_SIZE, SEEK_SET);
        fread(result, ENTRY_SIZE, 1, file);

        int cmp = memcmp(target_sha1, result->sha1, SHA1_COMPACT_SIZE);
        
        if (cmp == 0) {
            fclose(file);
            return 1;  // SHA1 found
        } else if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    fclose(file);
    return 0; // SHA1 Not found
}

int8_t msx_get_game_info(const char *file_path, RomInfo *result) {
    uint8_t sha1[SHA1_SIZE];
    if (calculate_sha1_file(file_path,sha1)) {
        // get info
        return msx_find_rom_info(sha1, result);
    }
    else
    {
        return 0;
    }
}
