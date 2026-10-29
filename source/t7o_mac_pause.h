#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Development-only. Requires the exact-build verification performed by the FX
 * install path. Reads the live hook_pausegame setting on each policy call.
 * Full mod-switch deactivation/reset still needs lifecycle integration. */
bool t7o_mac_pause_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_pause_activate(void);
void t7o_mac_pause_deactivate(void);
bool t7o_mac_pause_original_client(void);
