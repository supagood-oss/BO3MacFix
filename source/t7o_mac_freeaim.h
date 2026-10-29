#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_freeaim_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_freeaim_activate(void);
void t7o_mac_freeaim_deactivate(void);
void t7o_mac_freeaim_seen(const void *cg,uintptr_t caller);
bool t7o_mac_freeaim_crosshair(const void *cg,uintptr_t caller,bool original);
