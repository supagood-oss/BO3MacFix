#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint64_t started[64],seen[64][2];
    bool latched[64][2],mantling[64];
    float height;
} T7oMantleState;
void t7o_mantle_height(T7oMantleState *,float);
void t7o_mantle_track(T7oMantleState *,unsigned client,bool mantling);
bool t7o_mantle_probe(T7oMantleState *,unsigned client,unsigned server,uint64_t now);
void t7o_mantle_result(T7oMantleState *,unsigned client,unsigned server,uint64_t now,bool found);
uint32_t t7o_mantle_ui(const T7oMantleState *,unsigned client,uint64_t now,uint32_t flags);
