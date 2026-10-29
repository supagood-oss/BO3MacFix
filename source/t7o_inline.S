/* Called only by a prepatched internal-site stub, with the game's red zone
 * already reserved. Preserve every integer/SIMD register and condition flag.
 * The callback may deliberately edit the saved context. */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
.text
.macro BRIDGE name,id,handler
.globl \name
\name:
    pushfq
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $264,%rsp
    movdqu %xmm0,0(%rsp)
    movdqu %xmm1,16(%rsp)
    movdqu %xmm2,32(%rsp)
    movdqu %xmm3,48(%rsp)
    movdqu %xmm4,64(%rsp)
    movdqu %xmm5,80(%rsp)
    movdqu %xmm6,96(%rsp)
    movdqu %xmm7,112(%rsp)
    movdqu %xmm8,128(%rsp)
    movdqu %xmm9,144(%rsp)
    movdqu %xmm10,160(%rsp)
    movdqu %xmm11,176(%rsp)
    movdqu %xmm12,192(%rsp)
    movdqu %xmm13,208(%rsp)
    movdqu %xmm14,224(%rsp)
    movdqu %xmm15,240(%rsp)
    movq %rsp,%rdi
    movl $\id,%esi
    cld
    call \handler
    movdqu 0(%rsp),%xmm0
    movdqu 16(%rsp),%xmm1
    movdqu 32(%rsp),%xmm2
    movdqu 48(%rsp),%xmm3
    movdqu 64(%rsp),%xmm4
    movdqu 80(%rsp),%xmm5
    movdqu 96(%rsp),%xmm6
    movdqu 112(%rsp),%xmm7
    movdqu 128(%rsp),%xmm8
    movdqu 144(%rsp),%xmm9
    movdqu 160(%rsp),%xmm10
    movdqu 176(%rsp),%xmm11
    movdqu 192(%rsp),%xmm12
    movdqu 208(%rsp),%xmm13
    movdqu 224(%rsp),%xmm14
    movdqu 240(%rsp),%xmm15
    addq $264,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    popfq
    ret
.endm
BRIDGE SYM(t7o_inline_jump_cap),0,SYM(t7o_inline_dispatch)
BRIDGE SYM(t7o_inline_land_begin),1,SYM(t7o_inline_dispatch)
BRIDGE SYM(t7o_inline_land_end),2,SYM(t7o_inline_dispatch)
BRIDGE SYM(t7o_inline_traversal_input),3,SYM(t7o_mac_traversal_inline_dispatch)
BRIDGE SYM(t7o_inline_traversal_eligible),4,SYM(t7o_mac_traversal_inline_dispatch)
BRIDGE SYM(t7o_inline_traversal_surface),5,SYM(t7o_mac_traversal_inline_dispatch)
BRIDGE SYM(t7o_inline_viewmodel_object),6,SYM(t7o_mac_ui_inline_dispatch)
