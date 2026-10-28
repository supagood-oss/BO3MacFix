#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_lifecycle_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_lifecycle_ready(void);
/* 1 accepted, 0 disabled by the mod, 2 no loaded mod, -1 unavailable. */
int t7o_mac_lifecycle_unload(void);
