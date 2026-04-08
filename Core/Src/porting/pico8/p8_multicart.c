/*
 * p8_multicart.c — Multicart file search on SD card
 *
 * Separate file to isolate FatFs ff.h include (conflicts with
 * other headers when included in main_pico8.c).
 * Uses prefix matching: load("#kalikan_stage_1b") matches
 * "kalikan_stage_1b-3.p8.png" (BBS adds version suffix like -3).
 */
#include <string.h>
#include <stdio.h>
#include "ff.h"

int sys_find_multicart(const char* cart_id, char* out_path, int out_size) {
    static const char* search_dirs[] = {
        "/roms/pico8/",
        "/roms/pico8/.multicarts/",
    };
    int id_len = strlen(cart_id);

    for (int d = 0; d < 2; d++) {
        DIR dir;
        FILINFO fno;
        if (f_opendir(&dir, search_dirs[d]) != FR_OK) continue;

        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            const char* name = fno.fname;
            /* Check if filename starts with cart_id */
            if (strncmp(name, cart_id, id_len) != 0) continue;
            /* After the prefix, expect a separator (-,_,.) or end */
            char after = name[id_len];
            if (after != '-' && after != '.' && after != '_' && after != '\0') continue;
            /* Check for .p8.png or .p8 extension */
            int nlen = strlen(name);
            if ((nlen > 7 && strcmp(name + nlen - 7, ".p8.png") == 0) ||
                (nlen > 3 && strcmp(name + nlen - 3, ".p8") == 0)) {
                snprintf(out_path, out_size, "%s%s", search_dirs[d], name);
                f_closedir(&dir);
                printf("P8: multicart match: %s -> %s\n", cart_id, out_path);
                return 1;  /* found */
            }
        }
        f_closedir(&dir);
    }

    out_path[0] = '\0';
    return 0;  /* not found */
}
