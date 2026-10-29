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
static bool local_transition(const T7oCoopPause *s,const T7oCoopContext *c) { return s->local_transition && local_only(c); }
static void set(T7oCoopContext *c,const char *name,bool enabled) {
    if (c->set) c->set(c->user,name,enabled ? "1" : "0");
    if (!strcmp(name,"cl_paused")) c->paused=enabled;
}
static void clear_handshake(T7oCoopPause *s) {
    s->negotiating=s->aborted=s->acknowledged=s->failure_reported=s->needs_release=false;
    s->participants=0;s->started_at=s->acknowledged_at=s->last_sent=s->heartbeat_at=0;
    memset(s->required,0,sizeof(s->required));memset(s->last_ack,0,sizeof(s->last_ack));
    memset(s->sent_at,0,sizeof(s->sent_at));memset(s->progress_at,0,sizeof(s->progress_at));
}
void t7o_coop_pause_reset(T7oCoopPause *s) { memset(s,0,sizeof(*s));s->published=-1; }
static bool send(T7oCoopContext *c,unsigned i,const char *text) {
    uint32_t sequence=c->clients[i].sequence;
    if (!c->send || !c->send(c->user,i,text,&sequence)) return false;
    c->clients[i].sequence=sequence;return true;
}
static unsigned broadcast(T7oCoopPause *s,T7oCoopContext *c,bool paused,bool begin) {
    if (c->max_clients<2 || c->max_clients>T7O_COOP_CLIENTS) return 0;
    unsigned count=0;
    for (unsigned i=0;i<(unsigned)c->max_clients;++i) {
        T7oCoopClient *client=&c->clients[i];if (client->state!=5 || !client->remote) continue;
        uint32_t before=client->sequence;char text[32];
        if (!paused || begin) snprintf(text,sizeof(text),"q @ %u",paused ? 1u : 0u);
        else {
            if (client->sequence-client->ack>31) continue;
            snprintf(text,sizeof(text),"q @ 1 %04x",++s->nonce&0xffff);
        }
        if (!send(c,i,text)) continue;
        if (client->sequence==before) {
            if (before-client->ack>31) continue;
            snprintf(text,sizeof(text),"q @ %u %04x",paused ? 1u : 0u,++s->nonce&0xffff);
            if (!send(c,i,text) || client->sequence==before) continue;
        }
        ++count;
        if (paused) {
            s->last_sent=s->sent_at[i]=c->now;
            if (begin) { s->required[i]=client->sequence;s->participants|=UINT32_C(1)<<i; }
        }
    }
    return count;
}
static void heartbeat(T7oCoopPause *s,T7oCoopContext *c) {
    if (!s->negotiating || s->aborted || !remote_host(c)) return;
    uint64_t interval=s->heartbeat_at<=s->last_sent ? 800 : 250;
    if (s->heartbeat_at && s->heartbeat_at<=c->now && c->now-s->heartbeat_at<interval) return;
    broadcast(s,c,true,false);s->heartbeat_at=c->now;
}
static void local_grace(T7oCoopPause *s,const T7oCoopContext *c) {
    if (!s->was_multi) return;
    if (!s->local_grace) { s->local_since=c->now;s->local_ready=false;s->host_frozen=true; }
    s->local_grace=true;clear_handshake(s);
}
static bool client_requested(T7oCoopPause *s,const T7oCoopContext *c) {
    if (!s->received || !s->remote_paused) return false;
    if (c->in_game) {
        if (s->remote_since && s->remote_since<=c->now && c->now-s->remote_since>4999) s->client_expired=true;
        return true;
    }
    s->received=s->remote_paused=s->client_expired=s->client_frozen=false;s->remote_since=0;return false;
}
static bool host_frozen(T7oCoopPause *s,const T7oCoopContext *c) {
    if (remote_host(c) && s->host_frozen && c->paused==1) return true;
    s->host_frozen=false;return false;
}
static bool grace_frozen(const T7oCoopContext *c) { return local_only(c) && c->paused==1; }
bool t7o_coop_pause_freeze_host(T7oCoopPause *s,const T7oCoopContext *c) { return host_frozen(s,c) || grace_frozen(c); }
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
    int value=parsed(text);const char *result=text;
    if (value>=0 && c->in_game) result=value ? "q cl_paused \"1\"" : "q cl_paused \"0\"";
    if (!c->in_game || !text) return result;
    if (value<0) {
        if (strncmp(text,"q cl_paused ",12)) return result;
        const char *p=text+12;if (*p=='"') ++p;
        if (*p!='0' && *p!='1') return result;
        value=*p++-'0';if (*p=='"') ++p;if (*p) return result;
    }
    bool recent=s->client_role_known && s->client_tick && s->client_tick<=c->now && c->now-s->client_tick<=2000;
    if (recent || (!value && s->client_is_guest)) {
        s->received=true;s->remote_since=value ? c->now : 0;s->client_expired=false;s->remote_paused=value!=0;
    }
    return result;
}
bool t7o_coop_pause_client_tick(T7oCoopPause *s,T7oCoopContext *c) {
    if (!c->in_game || c->server_role!=0) {
        s->received=s->remote_paused=s->client_frozen=s->client_expired=false;s->remote_since=0;
    }
    s->client_role_known=c->in_game && c->server_role==0;
    s->client_is_guest=s->client_role_known;s->client_tick=s->client_role_known ? c->now : 0;
    bool client=s->client_role_known && client_requested(s,c);
    if (client && (c->argument_state==0 || (s->remote_since && s->remote_since<=c->now && c->now-s->remote_since>119999))) {
        s->received=s->remote_paused=s->client_expired=false;s->remote_since=0;client=false;
    }
    s->client_frozen=client;
    if (s->published!=(int)client) { set(c,"aae_coop_paused",client);s->published=client; }
    return (remote_host(c) && host_frozen(s,c)) || client || grace_frozen(c);
}
bool t7o_coop_pause_allow_server(T7oCoopPause *s,T7oCoopContext *c) {
    if (!c->in_game) return true;
    if (c->server_role==0) return !client_requested(s,c);
    if (grace_frozen(c) || !remote_host(c)) return true;
    bool transition=local_transition(s,c);
    if (c->paused!=1) { s->host_frozen=s->was_multi=s->local_grace=s->local_ready=false;return !transition; }
    if (!transition) {
        if (s->local_grace) {
            if (s->local_ready) {
                if (c->total==1 && c->local_active && c->local_menu_pause) {
                    s->host_frozen=s->was_multi=s->local_ready=s->local_grace=false;return true;
                }
                s->local_ready=false;s->local_since=c->now;
            }
            return false;
        }
        if (s->was_multi && c->total<2) { local_grace(s,c);return false; }
        if (local_only(c)) { clear_handshake(s);s->host_frozen=true;s->local_grace=s->local_ready=false;return false; }
        if (c->total<2) { s->host_frozen=false;return c->total==1 || c->server_role!=0; }
        if (c->server_role!=1 || !s->negotiating || !s->acknowledged || s->aborted || !s->acknowledged_at || c->now<s->acknowledged_at) return false;
    }
    s->host_frozen=true;return false;
}
void t7o_coop_pause_server_tick(T7oCoopPause *s,T7oCoopContext *c) {
    if (!c->in_game) {
        uint32_t nonce=s->nonce;int published=s->published;
        t7o_coop_pause_reset(s);s->nonce=nonce;s->published=published;return;
    }
    if (local_only(c)) {
        s->host_frozen=s->was_multi=s->local_grace=s->local_ready=false;clear_handshake(s);return;
    }
    if (!c->remote_enabled) {
        bool was=s->negotiating || s->participants || s->host_frozen || s->was_multi;
        if (c->server_role==1 && (s->negotiating || s->participants)) broadcast(s,c,false,false);
        clear_handshake(s);s->host_frozen=s->was_multi=s->local_grace=s->local_ready=s->had_multi=s->local_transition=false;
        if (was && c->paused==1) set(c,"cl_paused",false);
        return;
    }
    if (!remote_host(c)) {
        s->host_frozen=s->was_multi=s->local_grace=s->local_ready=s->had_multi=s->local_transition=false;clear_handshake(s);return;
    }
    if (c->total>=2 && c->remote_active>0 && c->server_role==1) { s->had_multi=true;s->local_transition=false; }
    else if (c->remote_connected==0 && s->had_multi && c->server_role==1) {
        if (!s->local_transition) { clear_handshake(s);s->was_multi=s->local_grace=s->local_ready=false; }
        s->local_transition=true;
    }
    if (c->paused!=1) {
        if (s->negotiating && c->server_role==1 && c->remote_active>0) broadcast(s,c,false,false);
        clear_handshake(s);s->local_grace=s->local_ready=s->was_multi=s->host_frozen=false;return;
    }
    if (local_transition(s,c)) { s->host_frozen=true;return; }
    if (s->was_multi) {
        if (c->total<1) { s->local_ready=false;s->local_since=c->now; }
        else if (!c->remote_active) local_grace(s,c);
        else if (s->participants) for (unsigned i=0;i<T7O_COOP_CLIENTS;++i) if (s->participants&(UINT32_C(1)<<i)) {
            if (i>=(unsigned)c->max_clients || c->clients[i].state!=5 || !c->clients[i].remote) {
                broadcast(s,c,false,false);local_grace(s,c);break;
            }
        }
    }
    if (s->local_grace) {
        if (c->total==1 && c->local_active && c->server_role==1) {
            if (c->now-s->local_since>1249 && c->local_menu_pause) s->local_ready=true;
        } else { s->local_ready=false;s->local_since=c->now; }
    }
    if (local_only(c)) { clear_handshake(s);s->host_frozen=true;s->local_grace=s->local_ready=false;return; }
    if (s->local_grace) return;
    if (!s->host_frozen) {
        if (c->total<2 || c->remote_active<1 || c->server_role!=1) return;
        if (!s->negotiating) {
            clear_handshake(s);s->was_multi=true;broadcast(s,c,true,true);s->negotiating=true;s->started_at=s->heartbeat_at=c->now;
        }
        heartbeat(s,c);if (s->aborted) return;
        if (!s->acknowledged) {
            bool ready=c->max_clients>=2 && c->max_clients<=T7O_COOP_CLIENTS && s->participants;unsigned count=0;
            for (unsigned i=0;ready && i<(unsigned)c->max_clients;++i) {
                const T7oCoopClient *p=&c->clients[i];if (!p->remote) continue;
                if (p->state==3 || p->state==4) { ready=false;break; }
                if (p->state==5) {
                    if (!(s->participants&(UINT32_C(1)<<i)) || (int32_t)(p->ack-s->required[i])<0) ready=false;
                    ++count;
                }
            }
            if (ready && count) {
                s->acknowledged=true;s->acknowledged_at=c->now;
                for (unsigned i=0;i<(unsigned)c->max_clients;++i) if (s->participants&(UINT32_C(1)<<i)) {
                    s->last_ack[i]=c->clients[i].ack;s->progress_at[i]=c->now;
                }
            } else if (!s->failure_reported && c->now-s->started_at>3499) {
                s->failure_reported=s->aborted=true;broadcast(s,c,false,false);
            }
        }
        if (s->acknowledged && s->acknowledged_at<=c->now) s->host_frozen=true;
        return;
    }
    heartbeat(s,c);if (s->aborted) return;
    bool release=!s->acknowledged || !s->participants || c->max_clients<2 || c->max_clients>T7O_COOP_CLIENTS;
    if (!s->last_sent || (s->last_sent<=c->now && c->now-s->last_sent>3499)) release=true;
    for (unsigned i=0;!release && i<T7O_COOP_CLIENTS;++i) if (s->participants&(UINT32_C(1)<<i)) {
        const T7oCoopClient *p=&c->clients[i];
        if (i>=(unsigned)c->max_clients || p->state!=5 || !p->remote || !s->sent_at[i] ||
            (s->sent_at[i]<=c->now && c->now-s->sent_at[i]>3499)) { release=true;break; }
        if (!s->progress_at[i] || p->ack!=s->last_ack[i]) { s->last_ack[i]=p->ack;s->progress_at[i]=c->now; }
        else if (s->progress_at[i]<=c->now && c->now-s->progress_at[i]>3999) release=true;
    }
    s->needs_release=release;
}
