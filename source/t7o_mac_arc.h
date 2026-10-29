#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_arc_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_arc_activate(void);
void t7o_mac_arc_deactivate(void);
