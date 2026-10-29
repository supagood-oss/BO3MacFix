#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_migration_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_migration_activate(void);
void t7o_mac_migration_deactivate(void);
