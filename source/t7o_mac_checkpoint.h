#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_checkpoint_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_checkpoint_activate(void);
void t7o_mac_checkpoint_deactivate(void);
