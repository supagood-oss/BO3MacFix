#ifndef T7O_JSON_H
#define T7O_JSON_H
#include <stddef.h>
/* String-valued Lua JSON API. File paths have already been resolved by the
 * game-root adapter. Returned strings are malloc-owned. */
char *t7o_json_call(const char *operation,size_t argc,const char *const *argv);
#endif
