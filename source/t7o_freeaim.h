#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { T7O_AIM_INVALID,T7O_AIM_OFF,T7O_AIM_TARGET } T7oAimCommand;
bool t7o_freeaim_number(const char **cursor,float *out);
bool t7o_freeaim_target(const char *text,float out[3]);
T7oAimCommand t7o_freeaim_command(const char *text,float out[3]);
bool t7o_freeaim_rotate(const float origin[3],const float axis[9],const float target[3],float out[9]);
typedef struct {
    float origin[3],muzzle[3],target[3],target_copy[3];
    uint64_t time;
    uint8_t consumed,padding[7];
} T7oAimSnapshot;
bool t7o_freeaim_trace_select(T7oAimSnapshot records[32],unsigned cursor,uint64_t now,
    const float origin[3],const float end[3],const float muzzle[3],float start_out[3],float end_out[3]);
