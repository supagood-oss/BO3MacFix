#ifndef BO3_MOD_FILEIO_H
#define BO3_MOD_FILEIO_H
#include <stddef.h>
int bo3_lua_path(const char *base, const char *input, char *output, size_t capacity);
int bo3_mkdirs(const char *path);
int bo3_prepare_save(const char *base, const char *input, const char *mode);
#endif
