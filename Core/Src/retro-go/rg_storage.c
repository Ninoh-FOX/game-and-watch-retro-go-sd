#include <odroid_system.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "ff.h"
#include "rg_storage.h"
#include <unistd.h>

static bool disk_mounted = false;

#define CHECK_PATH(path)          \
    if (!(path && path[0]))       \
    {                             \
        printf("No path given"); \
        return false;             \
    }

void rg_storage_init(void)
{
    disk_mounted = true;
}

void rg_storage_deinit(void)
{
    if (!disk_mounted)
        return;

    rg_storage_commit();

    printf("Storage unmounted.");

    disk_mounted = false;
}

bool rg_storage_ready(void)
{
    return disk_mounted;
}

void rg_storage_commit(void)
{
    if (!disk_mounted)
        return;
    // flush buffers();
}

bool rg_storage_mkdir(const char *dir)
{
    CHECK_PATH(dir);

    if (mkdir(dir, 0777) == 0)
        return true;

    // FIXME: Might want to stat to see if it's a dir
    if (errno == EEXIST)
        return true;

    // Possibly missing some parents, try creating them
    char *temp = strdup(dir);
    for (char *p = temp + strlen(RG_STORAGE_ROOT) + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            if (strlen(temp) > 0)
            {
                mkdir(temp, 0777);
            }
            *p = '/';
            while (*(p + 1) == '/')
                p++;
        }
    }
    free(temp);

    // Finally try again
    if (mkdir(dir, 0777) == 0)
        return true;

    return false;
}

static int delete_cb(const rg_scandir_t *file, void *arg)
{
    rg_storage_delete(file->path);
    return RG_SCANDIR_CONTINUE;
}

bool rg_storage_delete(const char *path)
{
    CHECK_PATH(path);

    // Try the fast way first
    if (remove(path) == 0 || rmdir(path) == 0)
        return true;

    // If that fails, it's likely a non-empty directory and we go recursive
    // (errno could confirm but it has proven unreliable across platforms...)
    if (rg_storage_scandir(path, delete_cb, NULL, 0))
        return rmdir(path) == 0;

    return false;
}

rg_stat_t rg_storage_stat(const char *path)
{
    rg_stat_t ret = {0};
    struct stat statbuf;
    FILE *file = fopen(path,"rb");
    if (path && fstat(fileno(file), &statbuf) == 0)
    {
        ret.basename = rg_basename(path);
        ret.extension = rg_extension(path);
        ret.size = statbuf.st_size;
        ret.mtime = statbuf.st_mtime;
        ret.is_file = S_ISREG(statbuf.st_mode);
        ret.is_dir = S_ISDIR(statbuf.st_mode);
        ret.exists = true;
    }
    fclose(file);
    return ret;
}

bool rg_storage_exists(const char *path)
{
    CHECK_PATH(path);
    return access(path, F_OK) == 0;
}

bool rg_storage_scandir(const char *path, rg_scandir_cb_t *callback, void *arg, uint32_t flags)
{
    CHECK_PATH(path);
    uint32_t types = flags & (RG_SCANDIR_FILES | RG_SCANDIR_DIRS);
    size_t path_len = strlen(path) + 1;
    FILINFO fno;
    DIR dir;
    FRESULT res;

    if (path_len > RG_PATH_MAX - 5)
    {
        printf("Folder path too long '%s'", path);
        return false;
    }

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return false;
    }

    // We allocate on heap in case we go recursive through rg_storage_delete
    rg_scandir_t *result = calloc(1, sizeof(rg_scandir_t));
    if (!result)
    {
        f_closedir(&dir);
        return false;
    }

    strcat(strcpy(result->path, path), "/");
    result->basename = result->path + path_len;
    result->dirname = path;

    while (true)
    {
        wdog_refresh();
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0)
            break;

        if (fno.fname[0] == '.' && (!fno.fname[1] || fno.fname[1] == '.'))
        {
            // Skip self and parent
            continue;
        }

        if (path_len + strlen(fno.fname) >= RG_PATH_MAX)
        {
            printf("File path too long '%s/%s'", path, fno.fname);
            continue;
        }

        strcpy((char *)result->basename, fno.fname);

        result->is_file = !(fno.fattrib & AM_DIR);
        result->is_dir = fno.fattrib & AM_DIR;
        result->size = fno.fsize;
        result->mtime = fno.ftime;

        if ((result->is_dir && types != RG_SCANDIR_FILES) || (result->is_file && types != RG_SCANDIR_DIRS))
        {
            int ret = (callback)(result, arg);

            if (ret == RG_SCANDIR_STOP)
                break;

            if (ret == RG_SCANDIR_SKIP)
                continue;
        }

        if ((flags & RG_SCANDIR_RECURSIVE) && result->is_dir)
        {
            rg_storage_scandir(result->path, callback, arg, flags);
        }
    }
    f_closedir(&dir);
    free(result);

    return true;
}

/* copy file content into ram */
size_t rg_storage_copy_file_to_ram(char *file_path, uint8_t *ram_dest, file_progress_cb_t file_progress_cb) {
    FILE *file;
    size_t bytes_read;
    uint32_t total_written;

    file = fopen(file_path,"rb");
    if (file == NULL) {
        return 0;
    } 

    fseek(file, 0, SEEK_END);
    uint32_t total_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    total_written = 0;
    if (file_progress_cb) {
        file_progress_cb(total_size, 0, 0);
    }

    while ((bytes_read = fread(ram_dest+total_written, 1, 32*1024, file))) {
        wdog_refresh();
        total_written += bytes_read;
        if (file_progress_cb) {
            file_progress_cb(total_size, total_written, (uint8_t)((total_written * 100) / (total_size)));
        }
    }

    fclose(file);

    return total_written;
}

bool rg_storage_get_adjacent_files(const char *path, char *prev_path, char *next_path) {
    CHECK_PATH(path);
    
    // Get directory and extension
    char *dir = strdup(path);
    char *last_slash = strrchr(dir, '/');
    if (!last_slash) {
        free(dir);
        return false;
    }
    *last_slash = '\0';
    
    const char *ext = rg_extension(path);
    if (!ext) {
        free(dir);
        return false;
    }
    
    const char *current_basename = rg_basename(path);
    char best_prev[RG_PATH_MAX] = {0};
    char best_next[RG_PATH_MAX] = {0};
    bool need_prev = prev_path != NULL;
    bool need_next = next_path != NULL;
    
    DIR dir_obj;
    FILINFO fno;
    FRESULT res = f_opendir(&dir_obj, dir);
    if (res != FR_OK) {
        free(dir);
        return false;
    }
    
    // Single pass to find previous and next files
    while (true) {
        wdog_refresh();
        res = f_readdir(&dir_obj, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        // Skip directories and hidden files
        if (fno.fattrib & AM_DIR || fno.fname[0] == '.') continue;
        
        const char *file_ext = rg_extension(fno.fname);
        if (file_ext && strcasecmp(file_ext, ext) == 0) {
            // Compare with current file to determine if it's a better match
            int cmp = strcasecmp(fno.fname, current_basename);
            
            // For previous file: we want the highest file that's still lower than current
            if (need_prev && cmp < 0) {
                // If we don't have a previous file yet, or this one is higher than our current best
                if (!best_prev[0] || strcasecmp(fno.fname, best_prev + strlen(dir) + 1) > 0) {
                    sprintf(best_prev, "%s/%s", dir, fno.fname);
                }
            }
            
            // For next file: we want the lowest file that's still higher than current
            if (need_next && cmp > 0) {
                // If we don't have a next file yet, or this one is lower than our current best
                if (!best_next[0] || strcasecmp(fno.fname, best_next + strlen(dir) + 1) < 0) {
                    sprintf(best_next, "%s/%s", dir, fno.fname);
                }
            }
        }
    }
    
    f_closedir(&dir_obj);
    
    // Copy results to output buffers, using current path if no match found
    if (need_prev) {
        strcpy(prev_path, best_prev[0] ? best_prev : path);
    }
    if (need_next) {
        strcpy(next_path, best_next[0] ? best_next : path);
    }
    
    free(dir);
    return true;
}
