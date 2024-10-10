#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
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
    return crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
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
    metadata->flash_write_pointer = new_pointer;
    save_metadata();
}

static bool is_file_in_flash(uint32_t file_crc32, uint32_t *flash_address)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (metadata->files[i].valid && metadata->files[i].file_crc32 == file_crc32)
        {
            *flash_address = metadata->files[i].flash_address;
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

        if (metadata->files[i].valid && ((flash_address >= file_start && flash_address < file_end) ||
                                         (flash_address + data_size > file_start && flash_address + data_size <= file_end)))
        {
            metadata->files[i].valid = false;
        }
    }
}

static bool circular_flash_write(const char *file_path,
                                 uint32_t data_size,
                                 uint32_t *flash_address_out,
                                 bool byte_swap)
{
    uint8_t buffer[16 * 1024];

    // If there is not enough space available, write the file at the beginning of the flash
    if (flash_write_pointer - (uint32_t)&__EXTFLASH_START__ + data_size > OSPI_GetFlashSize())
    {
        flash_write_pointer = align_to_next_block((uint32_t)&__EXTFLASH_BASE__);
    }

    // Data are larger than flash size ... Abort
    if (flash_write_pointer - (uint32_t)&__EXTFLASH_START__ + data_size > OSPI_GetFlashSize())
    {
        return false;
    }
    uint32_t address_in_flash = flash_write_pointer - (uint32_t)&__EXTFLASH_BASE__;
    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(address_in_flash, align_to_next_block(data_size));
    *flash_address_out = flash_write_pointer;

    FILE *file = fopen(file_path, "rb");
    if (!file)
        return false;

    while (fread(buffer, 1, sizeof(buffer), file) > 0)
    {
        if (byte_swap) {
            for (int i=0; i < sizeof(buffer); i+=2)
            {
                char temp = buffer[i];
                buffer[i]=buffer[i+1];
                buffer[i+1]=temp;
            }
        }
        OSPI_Program(address_in_flash, buffer, sizeof(buffer));
        address_in_flash += sizeof(buffer);
        flash_write_pointer += sizeof(buffer);
    }
    OSPI_EnableMemoryMappedMode();
    fclose(file);

    printf("write @%lx done\n", address_in_flash);

    invalidate_overwritten_files(flash_write_pointer, data_size);

    update_flash_pointer(flash_write_pointer);

    return true;
}

uint8_t *store_file_in_flash(const char *file_path, uint32_t file_size, bool byte_swap)
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

    if (is_file_in_flash(file_crc32, &flash_address))
    {
        return (uint8_t *)flash_address;
    }

    if (!circular_flash_write(file_path, file_size, &flash_address, byte_swap))
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
            metadata->files[i].file_size = file_size;
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
        metadata->files[metadata->last_written_slot_index].file_size = file_size;
        metadata->files[metadata->last_written_slot_index].valid = true;
    }

    save_metadata();
    return (uint8_t *)flash_address;
}
