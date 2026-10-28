#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Boot-time main-pool reservation. Must install before engine initialization;
 * an already populated arena is never relocated or resized. */
bool t7o_mac_memory_install(uint64_t game_base,bool exact_build_verified);
