#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float elapsed,duration,from[2],current[2],target[2];
    uint8_t have_target,pending,valid,padding;
} T7oPromptState;
void t7o_prompt_reset(T7oPromptState *state);
/* Bounds are left/top/right/bottom; current is the element's absolute XY.
 * Only the projected XY and our own state may change. */
void t7o_prompt_step(T7oPromptState *,const float bounds[4],const float current[2],
    float projected[2],float transition,unsigned delta,float width);
