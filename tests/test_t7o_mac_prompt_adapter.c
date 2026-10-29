#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_prompt.c"
static unsigned registered,cleared;static uint32_t failed_site;
static uint16_t create_mock(uint16_t parent,const char *name) { assert(parent==42 && !strcmp(name,"clamped"));return 43; }
static void bool_mock(uint16_t model,bool value) { assert(model==43 && !value);++cleared; }
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *f,void **o) { (void)p;(void)f;(void)o;abort(); }
bool prepatch_native_site_present(uint32_t site) { (void)site;return true; }
bool prepatch_set_inline_callback(uint32_t site,void (*callback)(void)) {
    assert(site==T7O_MAC_PROMPT_POSITION || site==T7O_MAC_PROMPT_RESET || site==T7O_MAC_PROMPT_CLEAR_MODEL);
    if (callback) ++registered;
    return !callback || site!=failed_site;
}
void t7o_inline_prompt_position(void) {}
void t7o_inline_prompt_reset(void) {}
void t7o_inline_prompt_clear_model(void) {}
bool t7o_mac_mantle_ui_mode(int mode,uintptr_t caller,bool original) { (void)mode;(void)caller;return original; }
int main(void) {
    assert(sizeof(T7oPromptState)==36);
    assert(!t7o_mac_prompt_install(0,false) && !t7o_mac_prompt_activate());
    base=0x100000000;installed=true;create_model=create_mock;set_bool=bool_mock;
    failed_site=T7O_MAC_PROMPT_RESET;assert(!t7o_mac_prompt_activate() && !atomic_load(&active));failed_site=0;
    registered=0;assert(t7o_mac_prompt_activate() && registered==3);
    assert(t7o_mac_prompt_activate() && registered==3);
    const uintptr_t returns[]={T7O_MAC_PROMPT_MODE_RETURN,T7O_MAC_WIDGET_DISTANCE_MODE_RETURN,T7O_MAC_WIDGET_MODEL_MODE_RETURN};
    for (unsigned i=0;i<3;++i) {
        assert(prompt_mode(2,base+returns[i]));assert(!prompt_mode(1,base+returns[i]));assert(!prompt_mode(2,base+returns[i]+1));
    }
    uint8_t element[0x200]={0},parent[0x200]={0},frame[0x100]={0};
    float bounds[4]={-20,-10,20,10},current[2]={100,100},point[2]={900,600},aspect=16.0f/9,transition=1000;
    memcpy(element+0x34,bounds,16);memcpy(element+0x44,current,8);uint8_t *bp=frame+0x90;
    memcpy(bp-0x70,point,8);memcpy(bp-0x54,&aspect,4);
    T7oInlineContext context={.r14=(uintptr_t)element,.rbp=(uintptr_t)bp,.r15=16};memcpy(context.xmm[7],&transition,4);
    T7oInlineContext saved=context;
    t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_POSITION);
    assert(!memcmp(bp-0x70,point,8)); /* No fallback has requested a transition. */
    t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_RESET);
    t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_POSITION);
    float moved[2];memcpy(moved,bp-0x70,8);
    assert(moved[0]>120 && moved[0]<900 && moved[1]>110 && moved[1]<600);
    assert(!memcmp(&saved,&context,sizeof(context)));assert(!memcmp(element+0x44,current,8));
    float edge[2]={INFINITY,NAN};T7oPromptState state={0};t7o_prompt_reset(&state);
    t7o_prompt_step(&state,bounds,current,edge,transition,0,INFINITY);
    assert(isfinite(edge[0]) && isfinite(edge[1]) && state.valid);
    t7o_prompt_step(&state,bounds,current,edge,0,16,1280);assert(!state.valid && !state.pending);
    t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_CLEAR_MODEL);assert(!cleared);
    context.rbx=(uintptr_t)element;t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_CLEAR_MODEL);assert(!cleared);
    uint8_t *p=parent;memcpy(element+0xe8,&p,8);uint16_t model=42;memcpy(parent+0x190,&model,2);
    t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_CLEAR_MODEL);assert(cleared==1);
    pthread_mutex_lock(&lock);
    for (unsigned i=1;i<PROMPTS;++i) { T7oPromptState *s=state_for((void *)(uintptr_t)(0x1000+i));s->valid=1; }
    prompts[31].state.valid=0;prompts[31].state.pending=0;
    assert(state_for((void *)0x88888)==&prompts[31].state && count==PROMPTS);
    pthread_mutex_unlock(&lock);
    t7o_mac_prompt_deactivate();assert(!count && !next && !prompt_mode(2,base+returns[0]));
    t7o_mac_prompt_inline_dispatch(&context,T7O_INLINE_PROMPT_CLEAR_MODEL);assert(cleared==1);
    puts("Mac prompt smoothing, scoped modes, inline frames, UI model clearing and lifecycle passed (substituted engine)");
}
