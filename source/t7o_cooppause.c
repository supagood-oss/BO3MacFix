#include "t7o_cooppause.h"
#include <stdio.h>
#include <string.h>
bool t7o_coop_pause_map_active(const char *name) {
    if (!name || !*name) return false;
    unsigned n=0;
    for (;n<=64 && name[n];++n) {
        unsigned c=(unsigned char)name[n];
        if (!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') && c!='_' && c!='-') return false;
    }
    return n<=64 && strcmp(name,"core_frontend");
}
static bool local_only(const T7oCoopContext *c) { return c->in_game && c->server_role==1 && c->total>0 && c->remote_connected==0; }
static bool remote_host(const T7oCoopContext *c) { return c->remote_enabled && c->in_game && c->server_role==1 && c->remote_active>0; }
enum { GUEST_HEARTBEAT_TIMEOUT_MS = 5000 };
static void set(T7oCoopContext *c,const char *name,bool enabled) {
    if (c->set) c->set(c->user,name,enabled ? "1" : "0");
    if (!strcmp(name,"cl_paused")) c->paused=enabled;
}
static void clear_handshake(T7oCoopPause *s) {
    s->host_frozen=s->negotiating=s->aborted=s->acknowledged=false;
    s->participants=0;s->started_at=s->last_sent=s->heartbeat_at=0;
    memset(s->required,0,sizeof(s->required));memset(s->last_ack,0,sizeof(s->last_ack));
    memset(s->sent_at,0,sizeof(s->sent_at));memset(s->progress_at,0,sizeof(s->progress_at));
}
void t7o_coop_pause_reset(T7oCoopPause *s) { memset(s,0,sizeof(*s));s->published=-1; }
static bool send(T7oCoopContext *c,unsigned i,const char *text) {
    uint32_t sequence=c->clients[i].sequence;
    if (!c->send || !c->send(c->user,i,text,&sequence)) return false;
    c->clients[i].sequence=sequence;return true;
}
static void broadcast(T7oCoopPause *s,T7oCoopContext *c,bool paused,bool begin) {
    if (c->max_clients<2 || c->max_clients>T7O_COOP_CLIENTS) return;
    for (unsigned i=0;i<(unsigned)c->max_clients;++i) {
        T7oCoopClient *client=&c->clients[i];if (client->state!=5 || !client->remote) continue;
        if (!begin && !(s->participants&(UINT32_C(1)<<i))) continue;
        uint32_t before=client->sequence;char text[32];
        if (before-client->ack>31) continue;
        if (!paused || begin) snprintf(text,sizeof(text),"q @ %u",paused ? 1u : 0u);
        else snprintf(text,sizeof(text),"q @ 1 %04x",++s->nonce&0xffff);
        if (!send(c,i,text)) continue;
        if (client->sequence==before) {
            snprintf(text,sizeof(text),"q @ %u %04x",paused ? 1u : 0u,++s->nonce&0xffff);
            if (!send(c,i,text) || client->sequence==before) continue;
        }
        if (paused) {
            s->last_sent=s->sent_at[i]=c->now;
            if (begin) { s->required[i]=client->sequence;s->participants|=UINT32_C(1)<<i; }
        }
    }
}
static void abort_pause(T7oCoopPause *s,T7oCoopContext *c) {
    broadcast(s,c,false,false);
    s->aborted=true;
    s->host_frozen=false;
    if (c->paused==1) set(c,"cl_paused",false);
}
static void heartbeat(T7oCoopPause *s,T7oCoopContext *c) {
    uint64_t interval=s->heartbeat_at<=s->last_sent ? 800 : 250;
    if (s->heartbeat_at && s->heartbeat_at<=c->now && c->now-s->heartbeat_at<interval) return;
    broadcast(s,c,true,false);s->heartbeat_at=c->now;
}
static bool client_requested(const T7oCoopPause *s,const T7oCoopContext *c) {
    return c->in_game && c->server_role==0 && s->remote_paused && c->argument_state!=0 &&
        c->now>=s->remote_since && c->now-s->remote_since<GUEST_HEARTBEAT_TIMEOUT_MS;
}
static bool host_frozen(const T7oCoopPause *s,const T7oCoopContext *c) {
    return remote_host(c) && s->host_frozen && c->paused==1;
}
static bool grace_frozen(const T7oCoopContext *c) { return local_only(c) && c->paused==1; }
bool t7o_coop_pause_freeze_host(const T7oCoopPause *s,const T7oCoopContext *c) { return host_frozen(s,c) || grace_frozen(c); }
bool t7o_coop_pause_force_menu(T7oCoopPause *s,const T7oCoopContext *c) {
    if (!c->in_game) return false;
    if (c->server_role==1) return local_only(c) || (remote_host(c) && !s->aborted);
    return c->server_role==0 && client_requested(s,c);
}
static int parsed(const char *text) {
    if (!text || strncmp(text,"q @ ",4) || (text[4]!='0' && text[4]!='1')) return -1;
    if (!text[5]) return text[4]-'0';
    if (text[5]!=' ' || strlen(text)!=10) return -1;
    for (unsigned i=6;i<10;++i) if (!(text[i]>='0' && text[i]<='9') && !(text[i]>='a' && text[i]<='f')) return -1;
    return text[4]-'0';
}
const char *t7o_coop_pause_receive(T7oCoopPause *s,T7oCoopContext *c,const char *text) {
    if (!c->in_game || c->server_role!=0 || !text) return text;
    int value=parsed(text);const char *result=text;
    if (value>=0) result=value ? "q cl_paused \"1\"" : "q cl_paused \"0\"";
    if (value<0) {
        if (strncmp(text,"q cl_paused ",12)) return result;
        const char *p=text+12;if (*p=='"') ++p;
        if (*p!='0' && *p!='1') return result;
        value=*p++-'0';if (*p=='"') ++p;if (*p) return result;
    }
    s->remote_since=value ? c->now : 0;
    s->remote_paused=value!=0;
    return result;
}
bool t7o_coop_pause_client_tick(T7oCoopPause *s,T7oCoopContext *c) {
    bool client=client_requested(s,c);
    if (s->remote_paused && !client) {
        if (c->in_game && c->server_role==0 && c->paused==1) set(c,"cl_paused",false);
        s->remote_paused=false;
        s->remote_since=0;
    }
    if (s->published!=(int)client) { set(c,"aae_coop_paused",client);s->published=client; }
    return t7o_coop_pause_freeze_host(s,c) || client;
}
bool t7o_coop_pause_allow_server(T7oCoopPause *s,T7oCoopContext *c) {
    if (!c->in_game) return true;
    if (c->server_role==0) return !client_requested(s,c);
    return !remote_host(c) || s->aborted || c->paused!=1;
}
void t7o_coop_pause_server_tick(T7oCoopPause *s,T7oCoopContext *c) {
    if (!c->in_game) {
        uint32_t nonce=s->nonce;int published=s->published;
        t7o_coop_pause_reset(s);s->nonce=nonce;s->published=published;return;
    }
    if (local_only(c)) { clear_handshake(s);return; }
    if (!remote_host(c) || c->paused!=1) {
        if (c->server_role==1 && s->negotiating && !s->aborted) {
            if (c->paused==1) abort_pause(s,c);
            else broadcast(s,c,false,false);
        }
        clear_handshake(s);
        return;
    }
    if (s->aborted) return;
    if (!s->negotiating) {
        if (c->total<2) return;
        broadcast(s,c,true,true);
        s->negotiating=true;
        s->started_at=s->heartbeat_at=c->now;
    }

    uint32_t active=0;
    bool ready=c->remote_connected==c->remote_active;
    if (c->max_clients>=2 && c->max_clients<=T7O_COOP_CLIENTS) {
        for (unsigned i=0;i<(unsigned)c->max_clients;++i) {
            const T7oCoopClient *p=&c->clients[i];
            if (p->state!=5 || !p->remote) continue;
            active|=UINT32_C(1)<<i;
            if ((int32_t)(p->ack-s->required[i])<0) ready=false;
        }
    }
    ready=ready && active && active==s->participants;
    if (!s->acknowledged) {
        if (ready) {
            s->acknowledged=s->host_frozen=true;
            for (unsigned i=0;i<(unsigned)c->max_clients;++i) if (active&(UINT32_C(1)<<i)) {
                s->last_ack[i]=c->clients[i].ack;
                s->progress_at[i]=c->now;
            }
        } else if (c->now>=s->started_at && c->now-s->started_at>=3500) {
            abort_pause(s,c);
        } else heartbeat(s,c);
        return;
    }

    // A changing roster or a stalled connection invalidates the pause.
    // Check before sending the next heartbeat, so we cannot refresh a stale freeze.
    if (!ready) { abort_pause(s,c);return; }
    for (unsigned i=0;i<(unsigned)c->max_clients;++i) if (active&(UINT32_C(1)<<i)) {
        const T7oCoopClient *p=&c->clients[i];
        if (p->ack!=s->last_ack[i]) {
            s->last_ack[i]=p->ack;
            s->progress_at[i]=c->now;
        }
        if ((c->now>=s->progress_at[i] && c->now-s->progress_at[i]>=4000) ||
            (c->now>=s->sent_at[i] && c->now-s->sent_at[i]>=3500)) {
            abort_pause(s,c);
            return;
        }
    }
    heartbeat(s,c);
}
