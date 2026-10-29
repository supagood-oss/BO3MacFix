#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_cooppause_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_cooppause_activate(void);
void t7o_mac_cooppause_deactivate(void);
bool t7o_mac_cooppause_force_menu(void);
