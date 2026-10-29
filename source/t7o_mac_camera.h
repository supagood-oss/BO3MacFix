#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_camera_install(uint64_t game_base,bool exact_build_verified);
bool t7o_mac_camera_activate(void);
void t7o_mac_camera_deactivate(void);
/* Pointer-free checkpoint sidecar, shared with the Mac disk container. */
bool t7o_mac_camera_checkpoint_valid(const uint8_t bytes[176]);
void t7o_mac_camera_checkpoint_export(unsigned slot,uintptr_t buffer,uint8_t bytes[176]);
bool t7o_mac_camera_checkpoint_import(unsigned slot,uintptr_t buffer,const uint8_t bytes[176]);
