#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { T7O_CP_HEADER=1024,T7O_CP_CAMERA=176,T7O_CP_SERVER_MAX=0x480000,T7O_CP_CLIENT_MAX=0x280000 };
typedef struct {
    uint64_t owner;
    uint32_t max_clients,slot;
    char map[64],mod[128],folder[192];
} T7oCheckpointIdentity;
typedef struct {
    T7oCheckpointIdentity identity;
    uint64_t timestamp;
    uint32_t size[2];
    uint8_t camera[T7O_CP_CAMERA];
    uint8_t *payload[2];
} T7oCheckpointImage;
/* Mac serialized engine data is deliberately a separate format from Windows
 * AAE files. The build ID identifies the engine layout, not a Workshop version. */
bool t7o_checkpoint_identity_valid(const T7oCheckpointIdentity *identity);
bool t7o_checkpoint_identity_equal(const T7oCheckpointIdentity *a,const T7oCheckpointIdentity *b);
bool t7o_checkpoint_chunks_valid(const void *bytes,size_t size);
bool t7o_checkpoint_header(uint8_t header[T7O_CP_HEADER],const T7oCheckpointImage *image);
bool t7o_checkpoint_decode(const uint8_t header[T7O_CP_HEADER],T7oCheckpointImage *image);
bool t7o_checkpoint_payload_valid(const uint8_t header[T7O_CP_HEADER],const T7oCheckpointImage *image);
void t7o_checkpoint_dispose(T7oCheckpointImage *image);
bool t7o_checkpoint_read(const char *path,T7oCheckpointImage *image);
bool t7o_checkpoint_write(const char *path,const T7oCheckpointImage *image);
bool t7o_checkpoint_path(const char *root,const T7oCheckpointIdentity *identity,char *out,size_t capacity);
