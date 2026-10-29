#include "t7o_mac_prompt.h"
#include "t7o_prompt.h"
#include "t7o_mac_mantle.h"
#include "t7o_mac_offsets.h"
#include "t7o_inline.h"
#include "prepatch.h"
#include "dobby.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
enum { PROMPTS=128 };
typedef struct { T7oPromptState state;const void *element; } Prompt;
static uint64_t base;
static bool installed;
static atomic_bool active;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Prompt prompts[PROMPTS];static unsigned count,next;
static bool (*original_mode)(int);
static uint16_t (*create_model)(uint16_t,const char *);
static void (*set_bool)(uint16_t,bool);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static bool prompt_mode(int mode,uintptr_t caller) {
    if (!atomic_load(&active) || mode!=2) return false;
    return caller==base+T7O_MAC_PROMPT_MODE_RETURN || caller==base+T7O_MAC_WIDGET_DISTANCE_MODE_RETURN ||
        caller==base+T7O_MAC_WIDGET_MODEL_MODE_RETURN;
}
static bool session_mode(int mode) {
    uintptr_t caller=(uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    if (prompt_mode(mode,caller)) return true;
    return t7o_mac_mantle_ui_mode(mode,caller,original_mode(mode));
}
/* The native component uses 128 bounded records and prefers inactive entries
 * when full. No engine allocation, element extension or stale VM is touched. */
static T7oPromptState *state_for(const void *element) {
    if (!element) return NULL;
    for (unsigned i=0;i<count;++i) if (prompts[i].element==element) return &prompts[i].state;
    unsigned index=count;
    if (count<PROMPTS) ++count;
    else {
        index=next;
        for (unsigned i=0;i<PROMPTS;++i) {
            unsigned candidate=(next+i)%PROMPTS;
            if (!prompts[candidate].state.valid && !prompts[candidate].state.pending) { index=candidate;break; }
        }
        next=(index+1)%PROMPTS;
    }
    prompts[index]=(Prompt){.element=element};return &prompts[index].state;
}
void t7o_mac_prompt_inline_dispatch(T7oInlineContext *context,unsigned operation) {
    if (!atomic_load(&active) || !context) return;
    if (operation==T7O_INLINE_PROMPT_CLEAR_MODEL) {
        const uint8_t *element=(const void *)(uintptr_t)context->rbx;if (!element) return;
        const uint8_t *parent;memcpy(&parent,element+0xe8,8);if (!parent) return;
        uint16_t root;memcpy(&root,parent+0x190,2);if (!root) return;
        uint16_t model=create_model(root,"clamped");set_bool(model,false);return;
    }
    if (operation!=T7O_INLINE_PROMPT_RESET && operation!=T7O_INLINE_PROMPT_POSITION) return;
    const uint8_t *element=(const void *)(uintptr_t)context->r14;if (!element) return;
    pthread_mutex_lock(&lock);T7oPromptState *state=state_for(element);
    if (operation==T7O_INLINE_PROMPT_RESET) t7o_prompt_reset(state);
    else if (context->rbp) {
        float bounds[4],current[2],point[2],transition,aspect;
        uint8_t *frame=(void *)(uintptr_t)context->rbp;
        memcpy(bounds,element+0x34,sizeof(bounds));memcpy(current,element+0x44,sizeof(current));
        memcpy(point,frame-0x70,sizeof(point));memcpy(&aspect,frame-0x54,4);memcpy(&transition,context->xmm[7],4);
        t7o_prompt_step(state,bounds,current,point,transition,(uint32_t)context->r15,aspect*720);
        memcpy(frame-0x70,point,sizeof(point));
    }
    pthread_mutex_unlock(&lock);
}
void t7o_mac_prompt_deactivate(void) {
    atomic_store(&active,false);
    if (installed) {
        prepatch_set_inline_callback(T7O_MAC_PROMPT_POSITION,NULL);prepatch_set_inline_callback(T7O_MAC_PROMPT_RESET,NULL);
        prepatch_set_inline_callback(T7O_MAC_PROMPT_CLEAR_MODEL,NULL);
    }
    pthread_mutex_lock(&lock);memset(prompts,0,sizeof(prompts));count=next=0;pthread_mutex_unlock(&lock);
}
bool t7o_mac_prompt_activate(void) {
    if (!installed) return false;
    if (atomic_load(&active)) return true;
    if (!prepatch_set_inline_callback(T7O_MAC_PROMPT_POSITION,t7o_inline_prompt_position) ||
        !prepatch_set_inline_callback(T7O_MAC_PROMPT_RESET,t7o_inline_prompt_reset) ||
        !prepatch_set_inline_callback(T7O_MAC_PROMPT_CLEAR_MODEL,t7o_inline_prompt_clear_model)) {
        t7o_mac_prompt_deactivate();return false;
    }
    atomic_store(&active,true);return true;
}
bool t7o_mac_prompt_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t mode[]={0x55,0x48,0x89,0xe5,0x8b,0x05,0xaa,0xea,0x6b,0x05};
    if (memcmp(prepatch_original_code(address(T7O_MAC_SESSION_IS_MODE),sizeof(mode)),mode,sizeof(mode)) ||
        !prepatch_native_site_present(T7O_MAC_PROMPT_POSITION) || !prepatch_native_site_present(T7O_MAC_PROMPT_RESET) ||
        !prepatch_native_site_present(T7O_MAC_PROMPT_CLEAR_MODEL)) return false;
    create_model=address(T7O_MAC_UI_CREATE_MODEL);set_bool=address(T7O_MAC_UI_MODEL_SET_BOOL);
    if (DobbyHook(address(T7O_MAC_SESSION_IS_MODE),(void *)session_mode,(void **)&original_mode)) return false;
    installed=true;return true;
}
