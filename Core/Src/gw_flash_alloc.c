#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "main.h"
#include "crc32.h"
#include "gw_flash.h"
#include "gw_linker.h"
#include "config.h"
#include "gw_malloc.h"
#include "gw_flash_alloc.h"

#define METADATA_FILE ODROID_BASE_PATH_SAVES "/flashcachedata.bin"
#define MAX_FILES 50

// Metadata for each file
typedef struct
{
    uint32_t file_crc32;
    uint32_t flash_address;
    uint32_t file_size;
    bool valid;
} FileMetadata;

// Global Metadata
typedef struct
{
    FileMetadata files[MAX_FILES];
    uint32_t flash_write_pointer;
    uint16_t last_written_slot_index;
} Metadata;

static Metadata *metadata = NULL;
static uint32_t flash_write_pointer = 0;

static uint32_t compute_file_crc32(const char *file_path)
{
    // Include file modification time or content in CRC32 calculation
    struct stat file_stat;
    if (stat(file_path, &file_stat) == 0) {
        uint32_t crc = crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
        crc = crc32_le(crc, (const uint8_t *)&file_stat.st_mtime, sizeof(file_stat.st_mtime));
        printf("file time %ld\n", file_stat.st_mtime);
        return crc;
    } else {
        return crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
    }
    return 0;
}

static uint32_t align_to_next_block(uint32_t pointer)
{
    uint32_t block_size = OSPI_GetSmallestEraseSize(); // Typically 4KB
    return (pointer + block_size - 1) & ~(block_size - 1);
}

static void load_metadata()
{
    FILE *file = fopen(METADATA_FILE, "rb");
    if (!file)
    {
        metadata->flash_write_pointer = (uint32_t)&__EXTFLASH_BASE__;
        return;
    }
    fread(metadata, sizeof(Metadata), 1, file);
    fclose(file);
}

static void save_metadata()
{
    FILE *file = fopen(METADATA_FILE, "wb");
    if (!file)
        return;
    fwrite(metadata, sizeof(Metadata), 1, file);
    fclose(file);
}

static void initialize_flash_pointer()
{
    load_metadata();
    flash_write_pointer = metadata->flash_write_pointer;
}

static void update_flash_pointer(uint32_t new_pointer)
{
    if (metadata == NULL)
    {
        metadata = ram_calloc(1, sizeof(Metadata));
    }
    metadata->flash_write_pointer = new_pointer;
    save_metadata();
}

static bool is_file_in_flash(uint32_t file_crc32, uint32_t *flash_address, uint32_t *file_size_p)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (metadata->files[i].valid && metadata->files[i].file_crc32 == file_crc32)
        {
            *flash_address = metadata->files[i].flash_address;
            if (*file_size_p == 0)
                *file_size_p = metadata->files[i].file_size;
            return true;
        }
    }
    return false;
}

static void invalidate_overwritten_files(uint32_t flash_address, uint32_t data_size)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        uint32_t file_start = metadata->files[i].flash_address;
        uint32_t file_end = file_start + metadata->files[i].file_size;
        uint32_t flash_end = flash_address + data_size;

        if (metadata->files[i].valid && (flash_address < file_end && file_start < flash_end))
        {
            metadata->files[i].valid = false;
        }
    }
}

static bool circular_flash_write(const char *file_path,
                                 uint32_t *data_size,
                                 uint32_t *flash_address_out,
                                 bool byte_swap,
                                 void_progress_cb progress_cb)
{
    uint8_t buffer[16 * 1024];
    uint32_t total_bytes_processed = 0;
    uint8_t progress = 0;

    FILE *file = fopen(file_path, "rb");
    if (!file)
        return false;

    if (*data_size == 0) {
        fseek(file, 0, SEEK_END);
        *data_size = ftell(file);
        fseek(file, 0, SEEK_SET);
    }

    // If there is not enough space available, write the file at the beginning of the flash
    if (flash_write_pointer - (uint32_t)&__EXTFLASH_START__ + *data_size > OSPI_GetFlashSize())
    {
        flash_write_pointer = align_to_next_block((uint32_t)&__EXTFLASH_BASE__);
    }

    // Data are larger than flash size ... Abort
    if (flash_write_pointer - (uint32_t)&__EXTFLASH_START__ + *data_size > OSPI_GetFlashSize())
    {
        fclose(file);
        return false;
    }

    uint32_t old_flash_write_pointer = flash_write_pointer;
    uint32_t address_in_flash = flash_write_pointer - (uint32_t)&__EXTFLASH_BASE__;
    uint32_t block_size = OSPI_GetSmallestEraseSize();

    OSPI_DisableMemoryMappedMode();

    *flash_address_out = flash_write_pointer;

    while (total_bytes_processed < *data_size) {
        OSPI_EraseSync(address_in_flash, block_size);

        size_t bytes_read = fread(buffer, 1, block_size, file);
        if (bytes_read > 0) {
            if (byte_swap) {
                for (size_t i = 0; i < bytes_read; i += 2) {
                    uint8_t temp = buffer[i];
                    buffer[i] = buffer[i + 1];
                    buffer[i + 1] = temp;
                }
            }

            OSPI_Program(address_in_flash, buffer, bytes_read);

            address_in_flash += block_size;
            flash_write_pointer += block_size;
            total_bytes_processed += block_size;

            if (progress_cb) {
                progress = (uint8_t)((total_bytes_processed * 100) / (*data_size));
                progress_cb(progress);
            }
        }

        if (bytes_read < block_size) {
            break;
        }
    }

    OSPI_EnableMemoryMappedMode();
    fclose(file);

    invalidate_overwritten_files(old_flash_write_pointer, total_bytes_processed);
    update_flash_pointer(flash_write_pointer);

    return true;
}

// Clear the metadata in RAM only, to call if ram has been freed
void clear_flash_alloc_metadata() {
    metadata = NULL;
}

// Clear all metadata and delete the metadata file
void flash_alloc_reset()
{
    metadata = NULL;
    remove(METADATA_FILE);
}

uint8_t *store_file_in_flash(const char *file_path, uint32_t *file_size_p, bool byte_swap, void_progress_cb progress_cb)
{
    if (metadata == NULL)
    {
        metadata = ram_calloc(1, sizeof(Metadata));
    }
    initialize_flash_pointer();
    // TODO : append file modification time to filepath for crc32
    // to handle case where rom file in sd card has been modified
    uint32_t file_crc32 = compute_file_crc32(file_path);
    uint32_t flash_address;

    if (is_file_in_flash(file_crc32, &flash_address, file_size_p))
    {
        return (uint8_t *)flash_address;
    }

    if (!circular_flash_write(file_path, file_size_p, &flash_address, byte_swap, progress_cb))
    {
        return NULL;
    }

    bool metadata_updated = false;

    for (int i = 0; i < MAX_FILES; i++)
    {
        if (!metadata->files[i].valid)
        {
            metadata->files[i].file_crc32 = file_crc32;
            metadata->files[i].flash_address = flash_address;
            metadata->files[i].file_size = *file_size_p;
            metadata->files[i].valid = true;
            metadata->last_written_slot_index = i;
            metadata_updated = true;
            break;
        }
    }

    if (!metadata_updated)
    {
        metadata->last_written_slot_index = (metadata->last_written_slot_index + 1) % MAX_FILES;
        metadata->files[metadata->last_written_slot_index].file_crc32 = file_crc32;
        metadata->files[metadata->last_written_slot_index].flash_address = flash_address;
        metadata->files[metadata->last_written_slot_index].file_size = *file_size_p;
        metadata->files[metadata->last_written_slot_index].valid = true;
    }

    save_metadata();
    wdog_refresh();
    return (uint8_t *)flash_address;
}
