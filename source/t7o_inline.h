#pragma once
#include <stdint.h>
/* Exact layout of t7o_inline.S, checked with offsetof assertions in C. */
typedef struct {
    uint8_t xmm[16][16];
    uint64_t padding;
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax,flags;
} T7oInlineContext;
enum { T7O_INLINE_JUMP_CAP, T7O_INLINE_LAND_BEGIN, T7O_INLINE_LAND_END,
       T7O_INLINE_TRAVERSAL_INPUT, T7O_INLINE_TRAVERSAL_ELIGIBLE, T7O_INLINE_TRAVERSAL_SURFACE };
void t7o_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_jump_cap(void);
void t7o_inline_land_begin(void);
void t7o_inline_land_end(void);
void t7o_mac_traversal_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_traversal_input(void);
void t7o_inline_traversal_eligible(void);
void t7o_inline_traversal_surface(void);
