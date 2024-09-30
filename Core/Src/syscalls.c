#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include "rg_rtc.h"
#include "ff.h"

extern uint32_t log_idx;
extern char logbuf[1024 * 4];

typedef struct {
    FIL file;
    int is_open;
} FatFSFile;

#define MAX_OPEN_FILES 10
FatFSFile file_table[MAX_OPEN_FILES];

void init_file_table() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        file_table[i].is_open = 0;
    }
}

#define FATFS_FD_OFFSET 3 // Prevent collision with STDOUT_FILENO, ...
int find_free_slot() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_table[i].is_open) {
            return i;
        }
    }
    return -1; // No free slot
}

int _open(const char *name, int flags, int mode)
{
    int slot = find_free_slot();
    if (slot == -1) {
        errno = ENFILE;
        return -1;
    }

    BYTE mode_flags = 0;
    switch (flags&0xFFFF) {
        case 0x0:       // "r"
            mode_flags = FA_READ;
            break;
        case 0x601:     // "w"
            mode_flags = FA_CREATE_ALWAYS | FA_WRITE;
            break;
        case 0x209:     // "a"
            mode_flags = FA_OPEN_APPEND | FA_WRITE;
            break;
        case 0x2:       // "r+"
            mode_flags = FA_READ | FA_WRITE;
            break;
        case 0x602:     // "w+"
            mode_flags = FA_CREATE_ALWAYS | FA_READ | FA_WRITE;
            break;
        case 0x20a:     // "a+"
            mode_flags = FA_OPEN_APPEND | FA_READ | FA_WRITE;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    FRESULT res = f_open(&file_table[slot].file, name, mode_flags);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    file_table[slot].is_open = 1;
    return slot + FATFS_FD_OFFSET;
;
}

int _close(int file)
{
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    FRESULT res = f_close(&file_table[file].file);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    file_table[file].is_open = 0;
    return 0;
}

int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO)
    {
        uint32_t idx = log_idx;
        if (idx + len + 1 > sizeof(logbuf))
        {
            idx = 0;
        }

        memcpy(&logbuf[idx], ptr, len);
        idx += len;
        logbuf[idx] = '\0';

        log_idx = idx;

        return len;
    }
    else
    {
        file = file - FATFS_FD_OFFSET;
        if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
            errno = EBADF;
            return -1;
        }

        UINT bytes_written;
        FRESULT res = f_write(&file_table[file].file, ptr, len, &bytes_written);
        if (res != FR_OK) {
            errno = EIO;
            return -1;
        }

        return bytes_written;
    }
}

int _read(int file, char *ptr, int len)
{
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    UINT bytes_read;
    FRESULT res = f_read(&file_table[file].file, ptr, len, &bytes_read);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    return bytes_read;
}

off_t _lseek(int file, off_t offset, int whence) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    DWORD new_offset;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = f_tell(&file_table[file].file) + offset;
            break;
        case SEEK_END:
            new_offset = f_size(&file_table[file].file) + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    FRESULT res = f_lseek(&file_table[file].file, new_offset);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    return new_offset;
}

int _fstat(int file, struct stat *st) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    st->st_size = f_size(&file_table[file].file);
    st->st_mode = S_IFREG;
    return 0;
}

int _isatty(int file)
{
    return 0;
}

int _feof(int file) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    if (f_eof(&file_table[file].file)) {
        return 1;
    }
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    FRESULT res;

    res = f_mkdir(path);
    switch (res)
    {
        case FR_OK:
            return 0;
            break;
        case FR_EXIST:
            errno = EEXIST;
            break;
        case FR_NO_PATH:
            errno = ENOENT;
            break;
        case FR_INVALID_NAME:
            errno = EINVAL;
            break;
        case FR_DENIED:
            errno = EACCES;
            break;
        case FR_DISK_ERR:
            errno = EIO;
            break;
        default:
            errno = EIO;
            break;
    }
    return -1;
}

int rmdir(const char *path) {
    FRESULT res;
    DIR dir;
    FILINFO fno;

    // Open the directory
    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return -1;  // Directory could not be opened
    }

    // Check if the directory is empty before deleting
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] != 0) {
        // Directory is not empty or an error occurred
        f_closedir(&dir);
        return -1;
    }

    // Remove the directory if empty
    f_closedir(&dir);
    res = f_unlink(path);
    
    if (res == FR_OK) {
        return 0;  // Success
    }
    
    return -1;  // Failure
}

int _unlink(const char *path) {
    FRESULT res;
    res = f_unlink(path);
    if (res == FR_OK) {
        return 0;
    } else {
        errno = ENOENT;
        return -1;
    }
}

int __wrap_fflush(int file) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    FRESULT res = f_sync(&file_table[file].file);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int _gettimeofday(struct timeval *tv, void *tzvp)
{
    if (tv)
    {
        // get epoch UNIX time from RTC
        time_t unixTime = GW_GetUnixTime();
        tv->tv_sec = unixTime;

        // get millisecondes from rtc and convert them to microsecondes
        uint64_t millis = GW_GetCurrentMillis();
        tv->tv_usec = (millis % 1000) * 1000;
        return 0;
    }

    errno = EINVAL;
    return -1;
}
