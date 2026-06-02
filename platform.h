#ifndef MEMEX_PLATFORM_H
#define MEMEX_PLATFORM_H

#include <stddef.h>

typedef struct PlatformDir PlatformDir;

typedef struct {
    int exists;
    int is_dir;
    long mtime;
    long ctime;
} PlatformStat;

int platform_init(void);
void platform_shutdown(void);
int platform_getcwd(char *buf, size_t size);
int platform_mkdir(const char *path);
int platform_file_exists(const char *path);
int platform_is_dir(const char *path);
int platform_stat(const char *path, PlatformStat *st);
int platform_rename(const char *old_path, const char *new_path);
char platform_path_sep(void);

PlatformDir *platform_opendir(const char *path);
int platform_readdir(PlatformDir *dir, char *name, size_t size);
void platform_closedir(PlatformDir *dir);

#endif
