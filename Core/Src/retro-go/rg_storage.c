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