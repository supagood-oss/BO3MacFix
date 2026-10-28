#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Development adapter: compile qualification and live lifecycle checks are
 * still required. Disabled in ordinary builds by T7O_DEV_NATIVE=0. */
bool t7o_mac_fx_install(uint64_t game_base, const char *image_path);
bool t7o_mac_fx_activate(void);
bool t7o_mac_fx_active(void);
