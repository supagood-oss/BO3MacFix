#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_checkpoint_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_checkpoint_activate(void);
void t7o_mac_checkpoint_deactivate(void);
/* The camera adapter owns the two client root hooks. Keep the checkpoint
 * transaction callbacks in that chain instead of installing competing hooks. */
void t7o_mac_checkpoint_client_load_begin(void *file);
void t7o_mac_checkpoint_client_load_end(void *file);
