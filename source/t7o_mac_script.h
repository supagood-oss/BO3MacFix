#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Development-only bindings. The caller must have verified the exact original
 * game through t7o_mac_fx_install. Activation changes a builtin DATA entry at
 * a quiescent mod-load boundary, never executable instructions. */
bool t7o_mac_script_install(uint64_t game_base, bool exact_build_verified);
bool t7o_mac_script_activate(void);
bool t7o_mac_script_active(void);
bool t7o_mac_script_deactivate(void);
