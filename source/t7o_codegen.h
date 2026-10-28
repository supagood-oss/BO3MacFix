#pragma once
#include <stddef.h>
#include <stdint.h>
/* Replace ADDSS xmm0,xmm0 plus its exact non-relative four-byte tail with
 * MULSS xmm0,[slot]. A zero slot runs all original instructions. */
size_t t7o_codegen_ads_scale(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t original[8]);
/* The callback must preserve all registers/flags through the assembly bridge.
 * Protect the game's 128-byte red zone before any stack write. short_jcc
 * permits precisely one leading short Jcc; all other stolen bytes must be
 * verified non-relative instructions. RSP must be 16-byte aligned at site. */
size_t t7o_codegen_inline(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t *original,size_t length,int short_jcc);
/* Legacy MOVSS/MULSS/DIVSS xmm0..7,[rip+disp32], exactly eight bytes. */
size_t t7o_codegen_float(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t original[8]);
/* Audited x86-64 patch forms only. These builders write an output buffer, never
 * process memory. slot is an aligned uint64: zero selects original behavior;
 * nonzero has the selected uint32 value in its low half and enable in bit 32. */
size_t t7o_codegen_cmp32(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t *original,size_t length);
/* A conditional jump at the start, followed by whole, non-relative instructions
 * up to length. force_taken selects the enabled path. All incoming flags and
 * registers survive. Only near/short Jcc encodings are accepted. */
size_t t7o_codegen_branch(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t *original,size_t length,int force_taken);
