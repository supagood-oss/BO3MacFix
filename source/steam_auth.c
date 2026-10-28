#include "steam_auth.h"
#include "steam/CCallback.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
/* SteamUser019, already used by the supported Mac game. Ticket bytes are
 * retained in memory only after callback 163 reports success. */
typedef struct { uint32_t handle; int result; } TicketResponse;
static ISteamUser **steam_user;
static pthread_mutex_t auth_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned char ticket[8192];
static uint32_t ticket_size,handle;
static bool ready,registered;
static void result(CCallback *self,void *data) {
    (void)self;
    const TicketResponse *response=data;
    pthread_mutex_lock(&auth_lock);
    if (response && handle && response->handle==handle) {
        ready=response->result==1;
        if (!ready) { memset(ticket,0,sizeof(ticket)); ticket_size=0; }
    }
    pthread_mutex_unlock(&auth_lock);
}
static void result_call(CCallback *self,void *data,bool failed,SteamAPICall_t call) {
    (void)call;
    if (!failed) result(self,data);
}
static int result_size(CCallback *self) { (void)self; return sizeof(TicketResponse); }
typedef struct {
    void (*run)(CCallback *,void *);
    void (*run_call)(CCallback *,void *,bool,SteamAPICall_t);
    int (*size)(CCallback *);
} AuthVtable;
static AuthVtable vtable={result,result_call,result_size};
static CCallback callback={.vtable=&vtable,.m_iCallback=163};
void steam_http_auth_context(ISteamUser **user) { steam_user=user; }
void steam_http_auth_begin(bool enabled) {
    uint32_t previous;
    pthread_mutex_lock(&auth_lock);
    previous=handle; handle=0; ticket_size=0; ready=false;
    memset(ticket,0,sizeof(ticket));
    pthread_mutex_unlock(&auth_lock);
    if (!steam_user || !*steam_user) return;
    if (previous) (*steam_user)->CancelAuthTicket(steam_user,(int)previous);
    if (!enabled || !(*steam_user)->BLoggedOn(steam_user)) return;
    if (!registered) { SteamAPI_RegisterCallback(&callback,163); registered=true; }
    unsigned char bytes[sizeof(ticket)]={0}; uint32_t size=0;
    uint32_t next=(uint32_t)(*steam_user)->GetAuthSessionTicket(steam_user,bytes,sizeof(bytes),&size);
    pthread_mutex_lock(&auth_lock);
    if (next && size && size<=sizeof(ticket)) { handle=next; ticket_size=size; memcpy(ticket,bytes,size); }
    pthread_mutex_unlock(&auth_lock);
    memset(bytes,0,sizeof(bytes));
    if (next && (!size || size>sizeof(ticket))) (*steam_user)->CancelAuthTicket(steam_user,(int)next);
}
char *steam_http_ticket_copy(void) {
    static const char digits[]="0123456789abcdef";
    char *out=NULL;
    pthread_mutex_lock(&auth_lock);
    if (ready && ticket_size) {
        out=malloc((size_t)ticket_size*2+1);
        if (out) {
            for (uint32_t i=0;i<ticket_size;++i) { out[2*i]=digits[ticket[i]>>4]; out[2*i+1]=digits[ticket[i]&15]; }
            out[ticket_size*2]=0;
        }
    }
    pthread_mutex_unlock(&auth_lock);
    return out;
}
