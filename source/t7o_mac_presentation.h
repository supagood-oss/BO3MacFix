#pragma once
#include <stdbool.h>
#include <stdint.h>
bool t7o_mac_presentation_install(uint64_t base,bool exact_build_verified);
bool t7o_mac_presentation_activate(void);
void t7o_mac_presentation_deactivate(void);
int t7o_mac_presentation_gamepad(int fallback);
