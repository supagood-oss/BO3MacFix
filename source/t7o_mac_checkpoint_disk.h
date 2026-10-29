#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_checkpoint_disk_install(uint64_t base);
bool t7o_mac_checkpoint_disk_activate(void);
void t7o_mac_checkpoint_disk_deactivate(void);
/* Runs after the engine's server checkpoint frame, never on the file worker. */
void t7o_mac_checkpoint_disk_tick(void);
void t7o_mac_checkpoint_disk_frame_begin(int channel);
