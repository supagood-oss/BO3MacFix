/* Policy/fallback fixture with substituted engine calls. This does not install
 * hooks, execute the Mac game or qualify network pause synchronization. */
#include <assert.h>
#include <stdio.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_pause.c"
static int mode,pending,client_calls,server_calls;
static bool demo,paused,client_result,server_result;
static const char *setting;
static bool mode_mock(int value) { assert(value==3);return mode==3; }
static bool demo_mock(void) { return demo; }
static int pending_mock(void) { return pending; }
static bool paused_mock(void) { return paused; }
static bool client_mock(void) { ++client_calls;return client_result; }
static bool server_mock(void) { ++server_calls;return server_result; }
static void *find_mock(const char *name) { assert(!strcmp(name,"hook_pausegame"));return setting ? &setting : NULL; }
static const char *string_mock(const void *variable) { assert(variable==&setting);return setting; }
const void *prepatch_original_code(const void *address,size_t size) { (void)address;(void)size;abort(); }
int BO3MacFix_Hook(void *address,void *replacement,void **original) { (void)address;(void)replacement;(void)original;abort(); }
int main(void) {
    original_client=client_mock;original_server=server_mock;
    session_mode=mode_mock;demo_playing=demo_mock;pending_count=pending_mock;server_paused=paused_mock;
    dvar_find=find_mock;dvar_string=string_mock;
    setting="1";client_result=true;assert(client_can_pause() && client_calls==1);
    assert(!t7o_mac_pause_activate());installed=true;assert(t7o_mac_pause_activate());
    const char *off[]={NULL,"0","2","-1","invalid"};
    for (unsigned i=0;i<sizeof(off)/sizeof(*off);++i) {
        setting=off[i];client_result=(i&1)!=0;server_result=!client_result;
        assert(client_can_pause()==client_result && server_can_pause()==server_result);
    }
    int cc=client_calls,sc=server_calls;setting="1";
    for (mode=0;mode<=3;++mode) for (int d=0;d<2;++d) {
        demo=d;assert(client_can_pause()==(mode!=3 && !demo));
        for (pending=0;pending<2;++pending) for (int p=0;p<2;++p) {
            paused=p;assert(server_can_pause()==(mode!=3 && !pending && paused));
        }
    }
    assert(client_calls==cc && server_calls==sc);
    assert(!t7o_mac_pause_install(0,false));
    puts("Mac pause policy and original fallback contracts passed (substituted engine)");
}
