#include "t7o_mantle.h"
#include <math.h>
#include <string.h>
void t7o_mantle_height(T7oMantleState *s,float height) {
    if (!isfinite(height) || height<0 || height>4096) height=57;
    if (s->height==height) return;
    s->height=height;memset(s->seen,0,sizeof(s->seen));memset(s->latched,0,sizeof(s->latched));
}
void t7o_mantle_track(T7oMantleState *s,unsigned client,bool mantling) {
    if (client>=64) return;
    s->mantling[client]=mantling;
    if (mantling) { memset(s->seen[client],0,sizeof(s->seen[client]));memset(s->latched[client],0,sizeof(s->latched[client])); }
}
bool t7o_mantle_probe(T7oMantleState *s,unsigned client,unsigned server,uint64_t now) {
    if (client>=64 || server>1 || s->mantling[client]) return false;
    if (!s->started[client]) s->started[client]=now;
    return now>=s->started[client] && now-s->started[client]>=2000;
}
void t7o_mantle_result(T7oMantleState *s,unsigned client,unsigned server,uint64_t now,bool found) {
    if (client>=64 || server>1) return;
    uint64_t seen=s->seen[client][server];
    bool recent=seen && now>=seen && now-seen<=20;
    if (found) s->seen[client][server]=now;
    s->latched[client][server]=found || (s->latched[client][server] && recent);
}
uint32_t t7o_mantle_ui(const T7oMantleState *s,unsigned client,uint64_t now,uint32_t flags) {
    if (client>=64) return flags;
    if (s->mantling[client]) return flags&~UINT32_C(0x10);
    for (unsigned server=0;server<2;++server) {
        uint64_t seen=s->seen[client][server];
        if (s->latched[client][server] && seen && now>=seen && now-seen<=20) return flags|0x10;
    }
    return flags;
}
