#ifndef T7O_SAVE_CODEC_H
#define T7O_SAVE_CODEC_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint64_t seed, factor; } T7oSaveKey;
/* Caller verifies Dead High's DLL hash before reading its two key generations. */
int t7o_save_keys(const unsigned char *dll,size_t size,T7oSaveKey keys[2]);
char *t7o_save_encode(const char *text,T7oSaveKey key);
char *t7o_save_decode(const char *hex,const T7oSaveKey keys[2]);
#endif
