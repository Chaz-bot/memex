#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct PlatformDir {
    DIR *dir;
};

int platform_init(void)
{
    return 1;
}

void platform_shutdown(void)
{
}

int platform_getcwd(char *buf, size_t size)
{
    return getcwd(buf, size) != NULL;
}

int platform_mkdir(const char *path)
{
    if (mkdir(path, 0777) == 0)
        return 1;
    return errno == EEXIST && platform_is_dir(path);
}

int platform_file_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

int platform_is_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int platform_stat(const char *path, PlatformStat *st)
{
    struct stat native_st;

    if (stat(path, &native_st) != 0) {
        if (st)
            memset(st, 0, sizeof(*st));
        return 0;
    }
    if (st) {
        st->exists = 1;
        st->is_dir = S_ISDIR(native_st.st_mode) ? 1 : 0;
        st->mtime = (long)native_st.st_mtime;
        st->ctime = (long)native_st.st_ctime;
    }
    return 1;
}

int platform_rename(const char *old_path, const char *new_path)
{
    return rename(old_path, new_path) == 0;
}

char platform_path_sep(void)
{
    return '/';
}

PlatformDir *platform_opendir(const char *path)
{
    PlatformDir *dir;

    dir = (PlatformDir *)malloc(sizeof(*dir));
    if (!dir)
        return NULL;
    dir->dir = opendir(path);
    if (!dir->dir) {
        free(dir);
        return NULL;
    }
    return dir;
}

int platform_readdir(PlatformDir *dir, char *name, size_t size)
{
    struct dirent *ent;

    if (!dir || !dir->dir || !name || size == 0)
        return 0;
    ent = readdir(dir->dir);
    if (!ent)
        return 0;
    strncpy(name, ent->d_name, size - 1);
    name[size - 1] = '\0';
    return 1;
}

void platform_closedir(PlatformDir *dir)
{
    if (!dir)
        return;
    if (dir->dir)
        closedir(dir->dir);
    free(dir);
}
