#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { T7O_AAE_PAUSE_PEERS = 18, T7O_AAE_PAUSE_MESSAGE_SIZE = 48 };
typedef enum { T7O_AAE_PAUSE_IDLE, T7O_AAE_PAUSE_HOST, T7O_AAE_PAUSE_GUEST } T7oAaePauseRole;

/* BO3's x86-64 netadr_t, passed by value to the Mac transport. */
typedef struct {
    uint32_t ip;
    uint16_t port, padding;
    uint32_t type, socket;
} T7oAaePauseAddress;

typedef struct {
    T7oAaePauseAddress address;
    uint64_t nonce, acknowledged, last_ack, last_send;
} T7oAaePausePeer;

typedef struct {
    T7oAaePauseAddress address;
    uint8_t message[T7O_AAE_PAUSE_MESSAGE_SIZE];
} T7oAaePauseSend;

typedef struct {
    T7oAaePauseRole role;
    int socket;
    uint64_t nonce, generation, updated, request_since;
    bool paused, frozen;
    T7oAaePausePeer guests[T7O_AAE_PAUSE_PEERS];
    T7oAaePauseAddress host;
    uint64_t host_nonce, received, applied, last_hello, last_ack;
} T7oAaePauseChannel;

bool t7o_aae_pause_address_valid(T7oAaePauseAddress);
void t7o_aae_pause_begin(T7oAaePauseChannel *, T7oAaePauseRole, int socket, uint64_t nonce);
/* Returns true when BO3 should clear its requested multiplayer pause. */
bool t7o_aae_pause_host_frame(T7oAaePauseChannel *, uint64_t now, bool want_pause,
    const T7oAaePauseAddress guests[T7O_AAE_PAUSE_PEERS], unsigned connected);
/* Freeze only after applying the newest state on the game thread. */
bool t7o_aae_pause_guest_frame(T7oAaePauseChannel *, uint64_t now, T7oAaePauseAddress host);
/* Dedicated protocol messages are consumed; other game traffic passes through. */
bool t7o_aae_pause_receive(T7oAaePauseChannel *, uint64_t now, int socket, T7oAaePauseAddress sender,
    const void *data, size_t size, uint64_t replacement_nonce);
size_t t7o_aae_pause_outbound(T7oAaePauseChannel *, uint64_t now,
    T7oAaePauseSend out[T7O_AAE_PAUSE_PEERS]);
