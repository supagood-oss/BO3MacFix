#include "../source/t7o_aae_pause_channel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const T7oAaePauseAddress host={.ip=1,.port=1,.type=4,.socket=4};
static const T7oAaePauseAddress guest={.ip=2,.port=2,.type=4,.socket=0};
static const T7oAaePauseAddress other_guest={.ip=3,.port=3,.type=4,.socket=0};

static void deliver(T7oAaePauseChannel *to, uint64_t now, T7oAaePauseAddress from,
    const uint8_t *bytes, size_t size) {
    assert(t7o_aae_pause_receive(to,now,to->socket,from,bytes,size,0x33));
}

int main(void) {
    T7oAaePauseChannel server,client;
    T7oAaePauseAddress roster[T7O_AAE_PAUSE_PEERS]={[1]=guest};
    T7oAaePauseSend out[T7O_AAE_PAUSE_PEERS];
    const uint8_t windows_hello[T7O_AAE_PAUSE_MESSAGE_SIZE] = {
        0xe1,0,'A','A','E','C','P','1', [8]=1,[12]=1,[24]=0x22
    };
    const uint8_t windows_pause[T7O_AAE_PAUSE_MESSAGE_SIZE] = {
        0xe1,0,'A','A','E','C','P','1', [8]=1,[12]=2,[16]=0x11,[24]=0x22,[32]=2,[40]=1
    };
    const uint8_t windows_ack[T7O_AAE_PAUSE_MESSAGE_SIZE] = {
        0xe1,0,'A','A','E','C','P','1', [8]=1,[12]=3,[16]=0x11,[24]=0x22,[32]=2,[40]=1
    };

    t7o_aae_pause_begin(&server,T7O_AAE_PAUSE_HOST,4,0x11);
    t7o_aae_pause_begin(&client,T7O_AAE_PAUSE_GUEST,0,0x22);
    assert(!t7o_aae_pause_host_frame(&server,1000,true,roster,1));
    assert(!t7o_aae_pause_guest_frame(&client,1000,host));
    assert(t7o_aae_pause_outbound(&client,1000,out)==1);
    assert(!memcmp(out[0].message,windows_hello,sizeof(windows_hello)));
    assert(!t7o_aae_pause_receive(&server,1000,4,guest,"normal game traffic",19,0));
    deliver(&server,1000,other_guest,windows_hello,sizeof(windows_hello));
    assert(t7o_aae_pause_outbound(&server,1000,out)==0);
    deliver(&server,1000,guest,windows_hello,47);
    assert(t7o_aae_pause_outbound(&server,1000,out)==0);
    assert(t7o_aae_pause_receive(&server,1000,0,guest,windows_hello,sizeof(windows_hello),0));
    assert(t7o_aae_pause_outbound(&server,1000,out)==0);
    deliver(&server,1000,guest,windows_hello,sizeof(windows_hello));
    assert(t7o_aae_pause_outbound(&server,1000,out)==1);
    assert(!memcmp(out[0].message,windows_pause,sizeof(windows_pause)));
    assert(t7o_aae_pause_outbound(&server,1069,out)==0);
    assert(t7o_aae_pause_outbound(&server,1070,out)==1); // retransmit a dropped state

    deliver(&client,1070,other_guest,windows_pause,sizeof(windows_pause));
    assert(!t7o_aae_pause_guest_frame(&client,1070,host));
    deliver(&client,1071,host,windows_pause,sizeof(windows_pause));
    assert(t7o_aae_pause_outbound(&client,1071,out)==0); // received but not applied
    assert(t7o_aae_pause_guest_frame(&client,1071,host));
    client.applied=client.generation;
    assert(t7o_aae_pause_outbound(&client,1071,out)==1);
    assert(!memcmp(out[0].message,windows_ack,sizeof(windows_ack)));
    assert(!server.frozen);
    deliver(&server,1072,guest,windows_ack,sizeof(windows_ack));
    assert(!t7o_aae_pause_host_frame(&server,1072,true,roster,1) && server.frozen);

    assert(!t7o_aae_pause_host_frame(&server,1100,false,roster,1) && !server.frozen);
    assert(t7o_aae_pause_outbound(&server,1100,out)==1);
    deliver(&client,1100,host,out[0].message,sizeof(out[0].message));
    deliver(&client,1101,host,windows_pause,sizeof(windows_pause)); // previous pause
    assert(!t7o_aae_pause_guest_frame(&client,1101,host));

    // No acknowledgement, or a roster change, must never leave the host frozen.
    t7o_aae_pause_begin(&server,T7O_AAE_PAUSE_HOST,4,0x11);
    assert(!t7o_aae_pause_host_frame(&server,2000,true,roster,1));
    assert(t7o_aae_pause_host_frame(&server,5500,true,roster,1) && !server.frozen);
    t7o_aae_pause_begin(&server,T7O_AAE_PAUSE_HOST,4,0x11);
    assert(!t7o_aae_pause_host_frame(&server,6000,true,roster,1));
    deliver(&server,6000,guest,windows_hello,sizeof(windows_hello));
    deliver(&server,6000,guest,windows_ack,sizeof(windows_ack));
    assert(!t7o_aae_pause_host_frame(&server,6000,true,roster,1) && server.frozen);
    roster[2]=other_guest;
    assert(t7o_aae_pause_host_frame(&server,6010,true,roster,2) && !server.frozen);

    t7o_aae_pause_begin(&client,T7O_AAE_PAUSE_GUEST,0,0x22);
    assert(!t7o_aae_pause_guest_frame(&client,1000,host));
    deliver(&client,1000,host,windows_pause,sizeof(windows_pause));
    assert(t7o_aae_pause_guest_frame(&client,10999,host));
    assert(!t7o_aae_pause_guest_frame(&client,11000,host) && client.role==T7O_AAE_PAUSE_IDLE);
    t7o_aae_pause_begin(&client,T7O_AAE_PAUSE_GUEST,0,0x33);
    assert(!t7o_aae_pause_guest_frame(&client,11001,host));
    deliver(&client,11001,host,windows_pause,sizeof(windows_pause));
    assert(!client.paused); // previous guest challenge
    puts("AAE channel: Windows-format hello/state/ack, retransmission, applied ACK, peer/socket scope and recovery passed");
}
