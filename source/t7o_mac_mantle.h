#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_mantle_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_mantle_activate(void);
void t7o_mac_mantle_deactivate(void);
bool t7o_mac_mantle_ui_mode(int mode,uintptr_t caller,bool original);
