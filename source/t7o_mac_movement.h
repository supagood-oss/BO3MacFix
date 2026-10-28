#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Exact-build development adapter. The eleven named settings, native command
 * callbacks, strafe, omni and mod reset still require live Mac qualification. */
bool t7o_mac_movement_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_movement_activate(void);
bool t7o_mac_movement_set(const char *name,bool enabled);
bool t7o_mac_movement_set_all(bool enabled);
void t7o_mac_movement_deactivate(void);
