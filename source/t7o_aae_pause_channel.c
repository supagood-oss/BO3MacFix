#include "t7o_aae_pause_channel.h"
#include <string.h>

/* AAE and AAE Lite's November 2026 plugin, carried inside the normal BO3
 * datagram. The game still owns the routing header and checksum. */
static const uint8_t tag[] = { 0xe1,0,'A','A','E','C','P','1' };
enum { HELLO=1, STATE=2, ACK=3, FRAME_TIMEOUT=5000, GUEST_TIMEOUT=10000,
    ACK_TIMEOUT=5000, START_TIMEOUT=3500, RETRY=70, HEARTBEAT=800 };
_Static_assert(sizeof(T7oAaePauseAddress)==16,"Mac netadr_t");

typedef struct {
    uint32_t kind;
    uint64_t host, guest, generation;
    bool paused;
} Message;

static bool current(uint64_t now, uint64_t since, uint64_t timeout) {
    return now>=since && now-since<timeout;
}
static bool due(uint64_t now, uint64_t since, uint64_t interval) {
    return !since || (now>=since && now-since>=interval);
}
bool t7o_aae_pause_address_valid(T7oAaePauseAddress a) {
    /* Type 4 is the Mac game's remote route supported by its send path. */
    return a.type==4 && a.socket<5;
}
static bool same_address(T7oAaePauseAddress a, T7oAaePauseAddress b) {
    return a.ip==b.ip && a.port==b.port && a.type==b.type && a.socket==b.socket;
}
static uint64_t read_le(const uint8_t *p, unsigned n) {
    uint64_t value=0;
    for (unsigned i=0;i<n;++i) value|=(uint64_t)p[i]<<(8*i);
    return value;
}
static void write_le(uint8_t *p, unsigned n, uint64_t value) {
    for (unsigned i=0;i<n;++i) p[i]=(uint8_t)(value>>(8*i));
}
static bool decode(const uint8_t *data, size_t size, Message *m) {
    if (size!=T7O_AAE_PAUSE_MESSAGE_SIZE || read_le(data+8,4)!=1 || read_le(data+44,4)!=0) return false;
    uint64_t paused=read_le(data+40,4);
    if (paused>1) return false;
    *m=(Message){ .kind=(uint32_t)read_le(data+12,4),.host=read_le(data+16,8),
        .guest=read_le(data+24,8),.generation=read_le(data+32,8),.paused=paused!=0 };
    if (!m->guest) return false;
    if (m->kind==HELLO) return !m->host && !m->generation && !m->paused;
    return (m->kind==STATE || m->kind==ACK) && m->host && m->generation;
}
static void encode(T7oAaePauseSend *out, T7oAaePauseAddress to, Message m) {
    out->address=to;
    memcpy(out->message,tag,sizeof(tag));
    write_le(out->message+8,4,1);
    write_le(out->message+12,4,m.kind);
    write_le(out->message+16,8,m.host);
    write_le(out->message+24,8,m.guest);
    write_le(out->message+32,8,m.generation);
    write_le(out->message+40,4,m.paused ? 1 : 0);
    write_le(out->message+44,4,0);
}
void t7o_aae_pause_begin(T7oAaePauseChannel *s, T7oAaePauseRole role, int socket, uint64_t nonce) {
    *s=(T7oAaePauseChannel){0};
    if (!nonce || socket<0 || socket>=5 || role==T7O_AAE_PAUSE_IDLE) return;
    s->role=role;
    s->socket=socket;
    s->nonce=nonce;
    s->generation=role==T7O_AAE_PAUSE_HOST ? 1 : 0;
}
static void request(T7oAaePauseChannel *s, uint64_t now, bool paused) {
    if (s->paused==paused) return;
    s->paused=paused;
    s->frozen=false;
    s->request_since=now;
    ++s->generation;
    for (size_t i=0;i<T7O_AAE_PAUSE_PEERS;++i) s->guests[i].last_send=0;
}
bool t7o_aae_pause_host_frame(T7oAaePauseChannel *s, uint64_t now, bool want_pause,
    const T7oAaePauseAddress guests[T7O_AAE_PAUSE_PEERS], unsigned connected) {
    if (s->role!=T7O_AAE_PAUSE_HOST) return false;
    s->updated=now;
    bool changed=false;
    unsigned ready=0, eligible=0;
    for (size_t i=0;i<T7O_AAE_PAUSE_PEERS;++i) {
        T7oAaePausePeer *p=&s->guests[i];
        if (!same_address(p->address,guests[i])) {
            *p=(T7oAaePausePeer){.address=guests[i]};
            changed=true;
        }
        if (!t7o_aae_pause_address_valid(p->address)) continue;
        ++eligible;
        if (p->acknowledged==s->generation && current(now,p->last_ack,ACK_TIMEOUT)) ++ready;
    }
    if (!want_pause) { request(s,now,false);return false; }
    if ((s->frozen && (changed || ready!=connected || eligible!=connected)) || !connected) {
        request(s,now,false);
        return true;
    }
    if (!s->paused) { request(s,now,true);return false; }
    if (ready==connected && eligible==connected) s->frozen=true;
    else if (!current(now,s->request_since,START_TIMEOUT)) {
        request(s,now,false);
        return true;
    }
    return false;
}
bool t7o_aae_pause_guest_frame(T7oAaePauseChannel *s, uint64_t now, T7oAaePauseAddress host) {
    if (s->role!=T7O_AAE_PAUSE_GUEST) return false;
    if ((t7o_aae_pause_address_valid(s->host) && !same_address(host,s->host)) ||
        (s->host_nonce && !current(now,s->received,GUEST_TIMEOUT))) {
        /* Rejoining needs a new challenge. Delayed messages from the old
         * pause cannot re-arm the guest after a timeout or a host change. */
        t7o_aae_pause_begin(s,T7O_AAE_PAUSE_IDLE,0,0);
        return false;
    }
    s->updated=now;
    s->host=host;
    return s->paused;
}
bool t7o_aae_pause_receive(T7oAaePauseChannel *s, uint64_t now, int socket, T7oAaePauseAddress sender,
    const void *data, size_t size, uint64_t replacement_nonce) {
    if (s->role==T7O_AAE_PAUSE_IDLE || !data || size<sizeof(tag) || memcmp(data,tag,sizeof(tag))) return false;
    Message m;
    if (socket!=s->socket || !current(now,s->updated,FRAME_TIMEOUT) ||
        !t7o_aae_pause_address_valid(sender) || !decode(data,size,&m)) return true;
    if (s->role==T7O_AAE_PAUSE_HOST) {
        for (size_t i=0;i<T7O_AAE_PAUSE_PEERS;++i) {
            T7oAaePausePeer *p=&s->guests[i];
            if (!same_address(p->address,sender)) continue;
            if (m.kind==HELLO && m.guest!=p->nonce) {
                p->nonce=m.guest;
                p->acknowledged=p->last_ack=p->last_send=0;
            } else if (m.kind==ACK && m.host==s->nonce && m.guest==p->nonce &&
                m.generation==s->generation && m.paused==s->paused) {
                p->acknowledged=m.generation;
                p->last_ack=now;
            }
            break;
        }
    } else if (m.kind==STATE && same_address(sender,s->host) && m.guest==s->nonce) {
        if (s->host_nonce && m.host!=s->host_nonce) {
            if (!replacement_nonce || replacement_nonce==s->nonce) return true;
            T7oAaePauseAddress host=s->host;
            t7o_aae_pause_begin(s,T7O_AAE_PAUSE_GUEST,socket,replacement_nonce);
            s->host=host;
            s->updated=now;
            return true;
        }
        if (m.generation<s->generation || (m.generation==s->generation && m.paused!=s->paused)) return true;
        if (m.generation!=s->generation) s->last_ack=0;
        s->host_nonce=m.host;
        s->received=now;
        s->generation=m.generation;
        s->paused=m.paused;
    }
    return true;
}
size_t t7o_aae_pause_outbound(T7oAaePauseChannel *s, uint64_t now, T7oAaePauseSend out[T7O_AAE_PAUSE_PEERS]) {
    if (s->role==T7O_AAE_PAUSE_IDLE || !current(now,s->updated,FRAME_TIMEOUT)) return 0;
    size_t count=0;
    if (s->role==T7O_AAE_PAUSE_HOST) {
        for (size_t i=0;i<T7O_AAE_PAUSE_PEERS;++i) {
            T7oAaePausePeer *p=&s->guests[i];
            if (!p->nonce || !due(now,p->last_send,p->acknowledged==s->generation ? HEARTBEAT : RETRY)) continue;
            encode(&out[count++],p->address,(Message){STATE,s->nonce,p->nonce,s->generation,s->paused});
            p->last_send=now;
        }
    } else if (t7o_aae_pause_address_valid(s->host)) {
        if (s->host_nonce && s->applied && s->applied==s->generation && due(now,s->last_ack,HEARTBEAT)) {
            encode(&out[count++],s->host,(Message){ACK,s->host_nonce,s->nonce,s->generation,s->paused});
            s->last_ack=now;
        } else if (due(now,s->last_hello,HEARTBEAT)) {
            encode(&out[count++],s->host,(Message){HELLO,0,s->nonce,0,false});
            s->last_hello=now;
        }
    }
    return count;
}
