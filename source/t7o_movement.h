#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { float direction[2];uint32_t flags;int32_t contact; } T7oMovementLocal;
typedef void (*T7oAccelerate)(void *context,const float direction[3],float speed,float acceleration);
/* Native AAE slide-momentum replacement, including its double/single rounding
 * boundaries and velocity restoration on its extreme-speed branch. */
void t7o_slide_momentum(float velocity[3],uint64_t flags,const T7oMovementLocal *local,
    T7oAccelerate accelerate,void *context);
