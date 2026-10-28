#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum { T7O_CAP_FX=1,T7O_CAP_CSC=2,T7O_CAP_MOVEMENT=4,T7O_CAP_PAUSE=8,
    T7O_CAP_UNLOAD=16,T7O_CAP_AIM=32,T7O_CAP_ALL=63 };
void t7o_platform_engine_capabilities(uint32_t capabilities);

/* Results are malloc-owned. Async completions never access a Lua VM. */
char *t7o_platform_call(const char *operation, size_t argc, const char *const *argv);
void t7o_platform_begin_mod(const char *dll_path);
/* Provider returns a malloc-owned, ready Steam ticket, never a logged value. */
void t7o_platform_ticket_provider(char *(*provider)(void));
