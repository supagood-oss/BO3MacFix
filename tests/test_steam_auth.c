#include "../source/steam_auth.h"
#include "../source/steam/CCallback.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static CCallback *response_callback;
static unsigned requested,cancelled;
void SteamAPI_RegisterCallback(CCallback *callback,int id) { assert(id==163); response_callback=callback; }
static bool online(void *user) { (void)user; return true; }
static int request(void *user,void *out,int capacity,uint32_t *size) {
    (void)user; assert(capacity>=3); memcpy(out,"\x01\x23\xff",3); *size=3; return ++requested;
}
static void cancel(void *user,int handle) { (void)user; assert(handle>0); ++cancelled; }
static void respond(uint32_t handle,int result) {
    struct { uint32_t handle; int result; } data={handle,result};
    typedef struct { void (*run)(CCallback *,void *); } Vtable;
    ((Vtable *)response_callback->vtable)->run(response_callback,&data);
}
int main(void) {
    ISteamUser api={.BLoggedOn=online,.GetAuthSessionTicket=request,.CancelAuthTicket=cancel};
    ISteamUser *pointer=&api;
    steam_http_auth_context(&pointer);
    steam_http_auth_begin(true);
    assert(requested==1 && !steam_http_ticket_copy());
    respond(999,1); assert(!steam_http_ticket_copy());
    respond(1,1); char *ticket=steam_http_ticket_copy(); assert(ticket && !strcmp(ticket,"0123ff")); free(ticket);
    steam_http_auth_begin(true); assert(cancelled==1 && !steam_http_ticket_copy());
    respond(1,1); assert(!steam_http_ticket_copy());
    respond(2,2); assert(!steam_http_ticket_copy());
    steam_http_auth_begin(true); respond(3,1);
    steam_http_auth_begin(false); assert(cancelled==3 && !steam_http_ticket_copy());
    puts("Steam ticket callback, generation, cancellation and failure checks passed"); return 0;
}
