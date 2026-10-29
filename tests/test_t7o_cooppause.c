#include "../source/t7o_cooppause.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned sends,sets;
static char last[32];
static bool send_mock(void *user,unsigned index,const char *text,uint32_t *sequence) {
    (void)user;assert(index==1);++sends;snprintf(last,sizeof(last),"%s",text);++*sequence;return true;
}
static void set_mock(void *user,const char *name,const char *value) {
    (void)user;assert(!strcmp(name,"cl_paused") || !strcmp(name,"aae_coop_paused"));assert(*value=='0' || *value=='1');++sets;
}
static T7oCoopContext host(void) {
    T7oCoopContext c={.in_game=true,.remote_enabled=true,.server_role=1,.total=2,.remote_connected=1,.remote_active=1,
        .max_clients=2,.argument_state=1,.now=1000,.send=send_mock,.set=set_mock};
    c.clients[0]=(T7oCoopClient){.state=5};c.clients[1]=(T7oCoopClient){.state=5,.remote=true};return c;
}
int main(void) {
    T7oCoopPause s;t7o_coop_pause_reset(&s);T7oCoopContext c=host();
    t7o_coop_pause_server_tick(&s,&c);assert(!s.negotiating && !sends);
    c.paused=1;t7o_coop_pause_server_tick(&s,&c);
    assert(s.negotiating && s.participants==2 && s.required[1]==1 && !strcmp(last,"q @ 1"));
    assert(!s.host_frozen && !t7o_coop_pause_allow_server(&s,&c));
    c.now+=10;c.clients[1].ack=1;t7o_coop_pause_server_tick(&s,&c);
    assert(s.acknowledged && s.host_frozen && t7o_coop_pause_client_tick(&s,&c));
    c.now+=800;t7o_coop_pause_server_tick(&s,&c);assert(!strcmp(last,"q @ 1 0001") && c.clients[1].sequence==2);
    c.paused=0;t7o_coop_pause_server_tick(&s,&c);assert(!s.host_frozen && !s.negotiating && !strcmp(last,"q @ 0"));
    t7o_coop_pause_reset(&s);c=host();c.paused=1;t7o_coop_pause_server_tick(&s,&c);
    c.now+=3500;t7o_coop_pause_server_tick(&s,&c);assert(s.aborted && !s.host_frozen && !strcmp(last,"q @ 0"));
    t7o_coop_pause_reset(&s);c=host();c.server_role=0;c.now=1000;
    assert(!t7o_coop_pause_client_tick(&s,&c));
    assert(!strcmp(t7o_coop_pause_receive(&s,&c,"q @ 1 ab12"),"q cl_paused \"1\""));
    assert(s.received && t7o_coop_pause_client_tick(&s,&c));
    c.now+=120000;assert(!t7o_coop_pause_client_tick(&s,&c) && !s.remote_paused);
    c.now+=1;t7o_coop_pause_receive(&s,&c,"q @ 1");assert(t7o_coop_pause_client_tick(&s,&c));
    const char *invalid="q @ 0 ABCD";assert(t7o_coop_pause_receive(&s,&c,invalid)==invalid && s.remote_paused);
    t7o_coop_pause_receive(&s,&c,"q @ 0");assert(!t7o_coop_pause_client_tick(&s,&c));
    t7o_coop_pause_receive(&s,&c,"q @ 1");c.argument_state=0;assert(!t7o_coop_pause_client_tick(&s,&c));
    c.in_game=false;t7o_coop_pause_server_tick(&s,&c);assert(!s.received && !s.host_frozen);
    assert(sets>0);puts("AAE co-op pause core: reliable ACK barrier, nonce heartbeat, host/guest unpause, timeout and malformed-command preservation passed");
}
