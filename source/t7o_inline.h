#pragma once
#include <stdint.h>
/* Exact layout of t7o_inline.S, checked with offsetof assertions in C. */
typedef struct {
    uint8_t xmm[16][16];
    uint64_t padding;
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax,flags;
    /* Existing CALL return slot above the saved registers. Only a dedicated
     * resume thunk may replace it; that thunk must release the red zone. */
    uint64_t return_address;
} T7oInlineContext;
enum { T7O_INLINE_JUMP_CAP, T7O_INLINE_LAND_BEGIN, T7O_INLINE_LAND_END,
       T7O_INLINE_TRAVERSAL_INPUT, T7O_INLINE_TRAVERSAL_ELIGIBLE, T7O_INLINE_TRAVERSAL_SURFACE,
       T7O_INLINE_VIEWMODEL_OBJECT,T7O_INLINE_FREEAIM_RETICLE,T7O_INLINE_FREEAIM_ADS,T7O_INLINE_FREEAIM_FADE,
       T7O_INLINE_FREEAIM_SPREAD,T7O_INLINE_PROMPT_POSITION,T7O_INLINE_PROMPT_RESET,T7O_INLINE_PROMPT_CLEAR_MODEL,
       T7O_INLINE_FREEAIM_TRACE_BEGIN,T7O_INLINE_FREEAIM_TRACE_END,T7O_INLINE_MANTLE_HEIGHT };
void t7o_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_jump_cap(void);
void t7o_inline_land_begin(void);
void t7o_inline_land_end(void);
void t7o_mac_traversal_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_traversal_input(void);
void t7o_inline_traversal_eligible(void);
void t7o_inline_traversal_surface(void);
void t7o_mac_ui_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_viewmodel_object(void);
void t7o_mac_freeaim_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_freeaim_reticle(void);
void t7o_inline_freeaim_ads(void);
void t7o_inline_freeaim_fade(void);
void t7o_inline_freeaim_reticle_skip(void);
void t7o_inline_freeaim_spread(void);
void t7o_mac_prompt_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_prompt_position(void);
void t7o_inline_prompt_reset(void);
void t7o_inline_prompt_clear_model(void);
void t7o_inline_freeaim_trace_begin(void);
void t7o_inline_freeaim_trace_end(void);
void t7o_mac_mantle_inline_dispatch(T7oInlineContext *,unsigned);
void t7o_inline_mantle_height(void);
