#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "havok.h"
bool t7o_mac_ui_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_ui_activate(lua_State *state);
void t7o_mac_ui_deactivate(void);
