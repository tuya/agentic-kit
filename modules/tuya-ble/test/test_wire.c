/* Independent peer harness: builds wire Frames through public GATT RX and
 * decrypts captured notifications. Test credentials are synthetic. */
#include "tuya_ble_prov.h"
#include "tuya_ble_bigdata.h"
#include "log.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
static const uint8_t server_iv[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const char auth[] = "0123456789abcdef0123456789abcdef";
static const char uuid[] = "abcdefghijklmnop";
static unsigned callbacks;
static void credentials(const tuya_ble_wifi_creds_t *c) {
    if (!strcmp(c->ssid,"router") && !strcmp(c->password,"password") && !strcmp(c->token,"1234567890123456")) callbacks++;
}
typedef struct {
    uint8_t packet[8][1024];
    size_t sizes[8], total, used;
    unsigned complete, next, notifications;
    int busy, fail;
    size_t budget;
    unsigned entropy_seed;
    uint16_t requested_count;
    char requested_ccode[3];
    uint32_t scan_token;
    int scan_rc;
} peer_t;
static int deterministic_random(uint8_t *out, size_t len, void *ctx)
{
    peer_t *peer = ctx;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(peer->entropy_seed + i);
    peer->entropy_seed += 17;
    return (int)len;
}
static int scan_request(uint16_t count, const char *ccode, uint32_t token, void *ctx)
{
    peer_t *peer = ctx;
    peer->requested_count = count;
    peer->requested_ccode[0] = ccode[0];
    peer->requested_ccode[1] = ccode[1];
    peer->requested_ccode[2] = '\0';
    peer->scan_token = token;
    return peer->scan_rc;
}

static uint32_t read_var(const uint8_t *p, size_t *o) {
    uint32_t v=0; unsigned shift=0;
    do { v |= (uint32_t)(p[*o]&127)<<shift; shift+=7; } while(p[(*o)++]&128);
    return v;
}
static size_t write_var(uint8_t *p,uint32_t v) {
    size_t n=0; do { p[n++]=(v&127)|(v>127?128:0); v>>=7; } while(v); return n;
}
static int capture(const uint8_t *p,uint16_t n,void *ctx) {
    peer_t *r=ctx;
    if(r->busy) return TUYA_BLE_SEND_BUSY;
    if(r->fail) return -9;
    CHECK(n<=r->budget && r->complete<8);
    size_t o=0; uint32_t part=read_var(p,&o);
    if(!part) { CHECK(!r->used); r->total=read_var(p,&o); CHECK((p[o++]>>4)==4); r->next=0; }
    CHECK(part==r->next++ && o<n && r->used+n-o<=r->total && r->total<=1024);
    memcpy(r->packet[r->complete]+r->used,p+o,n-o); r->used+=n-o; r->notifications++;
    if(r->used==r->total) { r->sizes[r->complete++]=r->used; r->used=0; }
    return 0;
}
static uint16_t crc(const uint8_t *p,size_t n) {
    uint16_t c=65535; while(n--) { c^=*p++; for(unsigned j=0;j<8;j++) c=(c&1)?(c>>1)^0xa001:c>>1; } return c;
}
static void put32(uint8_t *p,uint32_t v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static int crypt(int enc,const uint8_t *key,const uint8_t *iv,const uint8_t *in,size_t n,uint8_t *out) {
    mbedtls_aes_context aes; uint8_t v[16]; memcpy(v,iv,16); mbedtls_aes_init(&aes);
    int r=enc?mbedtls_aes_setkey_enc(&aes,key,128):mbedtls_aes_setkey_dec(&aes,key,128);
    if(!r) r=mbedtls_aes_crypt_cbc(&aes,enc?MBEDTLS_AES_ENCRYPT:MBEDTLS_AES_DECRYPT,n,v,in,out);
    mbedtls_aes_free(&aes); return r;
}
static size_t packet(uint8_t *out,uint16_t cmd,uint32_t sn,const void *data,size_t n,const uint8_t *key,int corrupt) {
    uint8_t plain[512]={0};put32(plain,sn);plain[8]=cmd>>8;plain[9]=cmd;plain[10]=n>>8;plain[11]=n;
    if(n)memcpy(plain+12,data,n);uint16_t c=crc(plain,12+n);plain[12+n]=c>>8;plain[13+n]=c^(corrupt?1:0);
    size_t len=14+n;
    if(!key){out[0]=0;memcpy(out+1,plain,len);return len+1;}
    size_t padded=(len+15)&~(size_t)15;memset(plain+len,padded-len,padded-len);
    out[0]=cmd==0?11:12;memcpy(out+1,server_iv,16);
    if(crypt(1,key,server_iv,plain,padded,out+17))return 0;return padded+17;
}
/* Mode byte + IV + one CBC block-cipher buffer, as a ready-to-deliver packet. */
static size_t out_pkt(uint8_t *out,uint8_t mode,const uint8_t *key,const uint8_t *plain,size_t n) {
    out[0]=mode;memcpy(out+1,server_iv,16);
    CHECK(!crypt(1,key,server_iv,plain,n,out+17));return n+17;
}
static int deliver(tuya_ble_prov_state_t *s,const uint8_t *p,size_t n,size_t budget) {
    size_t sent=0;unsigned part=0;
    while(sent<n){uint8_t out[512];size_t o=write_var(out,part);
        if(!part){o+=write_var(out+o,n);out[o++]=0x40;}
        size_t count=n-sent;if(count>budget-o)count=budget-o;
        memcpy(out+o,p+sent,count);CHECK(tuya_ble_prov_on_data(s,out,o+count)==0);sent+=count;part++;
    }return 0;
}
static int setup(tuya_ble_prov_state_t *s,peer_t *p,uint8_t key11[16]) {
    static unsigned seed = 1;
    memset(p,0,sizeof(*p));p->budget=20;p->entropy_seed=seed++;callbacks=0;
    tuya_ble_prov_cfg_ext_t cfg={.device_name="test",.product_key=uuid,.uuid=uuid,.auth_key=auth,.cb=credentials,.send_fn=capture,.send_ctx=p,.random_fn=deterministic_random,.random_ctx=p,.wifi_scan_request=scan_request,.wifi_scan_ctx=p};
    CHECK(!tuya_ble_prov_init(s,&cfg));uint8_t input[64];memcpy(input,auth,32);memcpy(input+32,uuid,16);memcpy(input+48,server_iv,16);
    CHECK(!mbedtls_md5(input,64,key11));return 0;
}
static int handshake(tuya_ble_prov_state_t *s,peer_t *p,uint8_t key12[16],size_t budget) {
    uint8_t k[16],wire[512],plain[512],input[22];CHECK(!setup(s,p,k));p->budget=budget;
    CHECK(!tuya_ble_prov_set_gatt_payload(s,budget));uint8_t size[2]={0,244};
    CHECK(!deliver(s,wire,packet(wire,0,1,size,2,k,0),20));CHECK(p->complete==1 && s->handshake_ready);
    CHECK(!crypt(0,k,p->packet[0]+1,p->packet[0]+17,p->sizes[0]-17,plain));
    CHECK(plain[8]==0 && plain[9]==0 && plain[10]==0 && plain[11]==135);
    CHECK(plain[144] == 0 && plain[145] == 1 && plain[146] == 3);
    CHECK(crc(plain,147)==((uint16_t)plain[147]<<8|plain[148]));
    CHECK(plain[12+2]==4 && plain[12+3]==4);
    memcpy(input,k,16);memcpy(input+16,plain+18,6);CHECK(!mbedtls_md5(input,sizeof(input),key12));
    CHECK(!deliver(s,wire,packet(wire,1,2,uuid,16,key12,0),20));
    CHECK(s->authenticated && s->paired && p->complete==3);
    CHECK(!crypt(0,key12,p->packet[1]+1,p->packet[1]+17,p->sizes[1]-17,plain));
    CHECK(plain[9]==1 && plain[11]==1 && plain[12]==0);return 0;
}
static int legacy_roundtrip(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t key[16],wire[512];CHECK(!handshake(&s,&p,key,20));CHECK(p.notifications>10);
    const char json[]="\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"1234567890123456\"}";
    size_t n=packet(wire,0x801b,3,json,sizeof(json)-1,key,0);CHECK(!deliver(&s,wire,n,20));CHECK(callbacks==1 && p.complete==4);
    CHECK(!deliver(&s,wire,n,20));CHECK(callbacks==1 && p.complete==4);
    tuya_ble_prov_close(&s);CHECK(!s.paired&&!s.authenticated&&!s.handshake_ready&&s.connection_generation==1);
    CHECK(!deliver(&s,wire,n,20));CHECK(callbacks==1);return 0;
}
static int unauthorized_and_invalid(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t k[16],wire[512];CHECK(!setup(&s,&p,k));
    const char json[]="\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"1234567890123456\"}";
    tuya_ble_prov_set_paired(&s,true);CHECK(!deliver(&s,wire,packet(wire,0x801b,10,json,sizeof(json)-1,NULL,0),20));
    CHECK(callbacks==0&&s.last_rx_sn==0&&!s.authenticated);
    uint8_t size[2]={0,244};CHECK(!deliver(&s,wire,packet(wire,0,11,size,2,k,1),20));
    uint8_t zero[16]={0};CHECK(!memcmp(s.key_11,zero,16)&&s.last_rx_sn==0&&s.peer_pkt_len==20);
    CHECK(!deliver(&s,wire,packet(wire,1,12,NULL,0,NULL,0),20));CHECK(!s.authenticated&&p.complete==0);return 0;
}
static int device_info_requery_restarts_pairing(void) {
    tuya_ble_prov_state_t state;
    peer_t peer;
    uint8_t key11[16], key12[16], wire[512], plain[512], input[64];
    uint8_t size[2] = {0, 253};

    CHECK(!handshake(&state, &peer, key12, 244));
    memcpy(input, auth, 32);
    memcpy(input + 32, uuid, 16);
    memcpy(input + 48, server_iv, 16);
    CHECK(!mbedtls_md5(input, sizeof(input), key11));
    CHECK(!deliver(&state, wire, packet(wire, 0, 3, size, sizeof(size), key11, 0), 20));
    CHECK(state.handshake_ready && !state.authenticated && !state.paired);
    CHECK(state.last_rx_sn == 3 && peer.complete == 4);
    CHECK(!crypt(0, key11, peer.packet[3] + 1, peer.packet[3] + 17,
                 peer.sizes[3] - 17, plain));
    CHECK(plain[8] == 0 && plain[9] == 0 && plain[10] == 0 && plain[11] == 135);
    memcpy(input, key11, 16);
    memcpy(input + 16, plain + 18, 6);
    CHECK(!mbedtls_md5(input, 22, key12));
    CHECK(!deliver(&state, wire, packet(wire, 1, 4, uuid, 16, key12, 0), 20));
    CHECK(state.authenticated && state.paired && peer.complete == 6);
    return 0;
}

static int malformed_credentials(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t k[16],wire[512];CHECK(!handshake(&s,&p,k,244));
    const char json[]="\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"12345678901234567\"}";
    CHECK(!deliver(&s,wire,packet(wire,0x801b,3,json,sizeof(json)-1,k,0),20));CHECK(!callbacks&&p.complete==3&&s.creds.ssid[0]==0);return 0;
}
static int transport_errors_and_timeout(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t k[16];CHECK(!setup(&s,&p,k));
    uint8_t continuation[]={1,0xaa};CHECK(tuya_ble_prov_on_data(&s,continuation,2)<0);
    uint8_t first[]={0,10,0x40,0xaa};CHECK(!tuya_ble_prov_on_data(&s,first,4));
    uint8_t skipped[]={2,0xbb};CHECK(tuya_ble_prov_on_data(&s,skipped,2)<0&&s.rx_total_len==0);
    CHECK(!tuya_ble_prov_on_data(&s,first,4));CHECK(!tuya_ble_prov_tick(&s,10000)&&s.rx_total_len==0);
    CHECK(tuya_ble_prov_on_data(&s,continuation,2)<0);
    uint8_t overshoot[]={0,1,0x40,1,2};CHECK(tuya_ble_prov_on_data(&s,overshoot,5)<0);
    uint8_t varint[]={0x80,0x80,0x80,0x80};CHECK(tuya_ble_prov_on_data(&s,varint,4)<0);return 0;
}
static int backpressure(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t k[16],wire[512],size[2]={0,244};CHECK(!setup(&s,&p,k));
    p.busy=1;CHECK(!deliver(&s,wire,packet(wire,0,1,size,2,k,0),20));CHECK(s.tx_count==1&&p.complete==0);
    CHECK(tuya_ble_prov_tx_ready(&s)==TUYA_BLE_SEND_BUSY&&s.tx_offset==0);
    p.busy=0;CHECK(!tuya_ble_prov_tx_ready(&s)&&p.complete==1&&s.tx_count==0);
    p.busy=1;CHECK(!deliver(&s,wire,packet(wire,0,2,size,2,k,0),20));
    CHECK(tuya_ble_prov_tick(&s,10000)<0&&!s.handshake_ready&&!s.tx_count);return 0;
}
static int aligned_crc_padding(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t k[16],wire[512],size[2]={0};CHECK(!setup(&s,&p,k));
    /* A 2-byte device-info request has a 16-byte Frame, without padding.
       Find one whose CRC low byte looks like padding. */
    unsigned chosen=0;
    for(unsigned v=20;v<=512;v++){uint8_t f[14]={0};f[3]=1;f[11]=2;f[12]=v>>8;f[13]=v;
        uint8_t c=crc(f,sizeof(f));if(c>0&&c<=16){chosen=v;break;}}
    CHECK(chosen);size[0]=chosen>>8;size[1]=chosen;
    CHECK(!deliver(&s,wire,packet(wire,0,1,size,2,k,0),20));CHECK(s.handshake_ready&&p.complete==1);return 0;
}
/* Strict PKCS#7: a block-aligned Frame still gets a full extra 16-byte padding
 * block. That is what the real Tuya app sends for every dev-info request —
 * its 16-byte Frame arrives as 32 bytes of ciphertext (52 on the wire). The
 * padding bytes here are pure junk, so accepting this must not depend on
 * them being valid PKCS#7 (with data_len=2 the value would be 0x0e, not 16). */
static int strict_pkcs7_full_padding_block(void) {
    tuya_ble_prov_state_t s;peer_t p;uint8_t k[16],wire[512],plain[512]={0};
    CHECK(!setup(&s,&p,k));
    put32(plain,1);plain[8]=0;plain[9]=0;plain[10]=0;plain[11]=2;
    plain[12]=0;plain[13]=244; /* PacketMaxSize=244 */
    uint16_t c=crc(plain,14);plain[14]=c>>8;plain[15]=c;
    memset(plain+16,0x99,16); /* junk, not 0x10 */
    size_t pkt=out_pkt(wire,11,k,plain,32);
    CHECK(!deliver(&s,wire,pkt,20));
    CHECK(s.handshake_ready && p.complete==1 && s.peer_pkt_len==244);
    uint8_t zero[16]={0};CHECK(memcmp(s.key_11,zero,16)!=0);
    /* Derive key_12 from the device's pair_rand and finish the handshake. */
    uint8_t resp[512],key12[16],input[22];
    CHECK(!crypt(0,k,p.packet[0]+1,p.packet[0]+17,p.sizes[0]-17,resp));
    memcpy(input,k,16);memcpy(input+16,resp+18,6);CHECK(!mbedtls_md5(input,sizeof(input),key12));
    CHECK(!deliver(&s,wire,packet(wire,1,2,uuid,16,key12,0),20));
    CHECK(s.paired && s.authenticated);return 0;
}

static int short_random(uint8_t *out, size_t n, void *ctx)
{
    (void)ctx;
    memset(out, 0x5a, n);
    return (int)n - 1;
}

static int entropy_failure(void)
{
    tuya_ble_prov_state_t state;
    peer_t peer;
    uint8_t key[16], wire[512], size[2] = {0, 244};
    CHECK(!setup(&state, &peer, key));
    state.cfg.random_fn = short_random;
    CHECK(!deliver(&state, wire, packet(wire, 0, 1, size, 2, key, 0), 20));
    CHECK(!state.handshake_ready && !peer.complete);
    uint8_t zero[6] = {0};
    CHECK(!memcmp(state.pair_rand, zero, sizeof(zero)));
    return 0;
}

static int isolated_connections(void)
{
    tuya_ble_prov_state_t first, second;
    peer_t a, b;
    uint8_t k1[16], k2[16], wire[512];
    CHECK(!handshake(&first, &a, k1, 20));
    CHECK(!handshake(&second, &b, k2, 244));
    CHECK(memcmp(k1, k2, sizeof(k1)) != 0);
    tuya_ble_prov_close(&first);
    CHECK(second.authenticated && second.last_rx_sn == 2);
    const char json[] = "\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"1234567890123456\"}";
    CHECK(!deliver(&second, wire, packet(wire, 0x801b, 3, json, sizeof(json)-1, k2, 0), 20));
    CHECK(callbacks == 1 && !first.authenticated);
    return 0;
}

static int credential_ack_backpressure(void)
{
    tuya_ble_prov_state_t state;
    peer_t peer;
    uint8_t key[16], wire[512];
    CHECK(!handshake(&state, &peer, key, 20));
    peer.busy = 1;
    const char json[] = "\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"1234567890123456\"}";
    CHECK(!deliver(&state, wire, packet(wire, 0x801b, 3, json, sizeof(json)-1, key, 0), 20));
    CHECK(callbacks == 0 && state.credentials_pending);
    peer.busy = 0;
    CHECK(!tuya_ble_prov_tx_ready(&state));
    CHECK(callbacks == 1 && !state.credentials_pending);
    CHECK(!tuya_ble_prov_tx_ready(&state) && callbacks == 1);
    return 0;
}

static int pending_credentials_are_not_replaced(void)
{
    for (unsigned queued = 0; queued < TUYA_BLE_TX_QUEUE_DEPTH; queued++) {
        tuya_ble_prov_state_t state;
        peer_t peer;
        uint8_t key[16], wire[512], plain[512];
        CHECK(!handshake(&state, &peer, key, 244));
        peer.busy = 1;
        const char first[] = "\0\0\0\1{\"ssid\":\"router\",\"pwd\":\"password\",\"token\":\"1234567890123456\"}";
        const char second[] = "\0\0\0\1{\"ssid\":\"other\",\"pwd\":\"different\",\"token\":\"6543210987654321\"}";
        const uint8_t status[] = {0, 0, 0, 4};
        CHECK(!deliver(&state, wire, packet(wire, 0x801b, 3, first, sizeof(first)-1, key, 0), 244));
        for (unsigned i = 0; i < queued; i++) {
            CHECK(!deliver(&state, wire, packet(wire, 0x801e, 4+i, status, sizeof(status), key, 0), 244));
        }
        CHECK(state.credentials_pending && !callbacks);
        uint32_t outgoing_sn = state.sn;
        CHECK(!deliver(&state, wire, packet(wire, 0x801b, 4+queued, second, sizeof(second)-1, key, 0), 244));
        CHECK(state.sn == outgoing_sn && state.tx_count == queued+1);
        CHECK(!strcmp(state.creds.ssid, "router"));
        peer.busy = 0;
        CHECK(!tuya_ble_prov_tx_ready(&state));
        CHECK(callbacks == 1 && !state.credentials_pending && peer.complete == 4+queued);
        CHECK(!crypt(0, key, peer.packet[3]+1, peer.packet[3]+17, peer.sizes[3]-17, plain));
        CHECK(plain[8] == 0x80 && plain[9] == 0x1c && plain[7] == 3);
        CHECK(!tuya_ble_prov_tx_ready(&state) && callbacks == 1);
    }
    return 0;
}

static unsigned transport_log_flags;
static void capture_transport_log(log_level_t level, const char *fmt, va_list args)
{
    char message[256];
    vsnprintf(message, sizeof(message), fmt, args);
    if (level != LOG_WARN || !strstr(message, "[ble] [TRSMITR]")) return;
    if (strstr(message, "out-of-order")) transport_log_flags |= 1;
    if (strstr(message, "RX transfer expired")) transport_log_flags |= 2;
    if (strstr(message, "TX stalled")) transport_log_flags |= 4;
}

static int transport_warnings_reach_log_facade(void)
{
    tuya_ble_prov_state_t state;
    peer_t peer;
    uint8_t key[16];
    CHECK(!setup(&state, &peer, key));
    uint8_t first[] = {0, 10, 0x40, 0xaa};
    uint8_t skipped[] = {2, 0xbb};
    log_level_t saved_level = log_get_level();
    transport_log_flags = 0;
    log_set_level(LOG_WARN);
    log_set_handler(capture_transport_log);
    int start = tuya_ble_prov_on_data(&state, first, sizeof(first));
    int invalid = tuya_ble_prov_on_data(&state, skipped, sizeof(skipped));
    int restart = tuya_ble_prov_on_data(&state, first, sizeof(first));
    int expired = tuya_ble_prov_tick(&state, 10000);
    peer.busy = 1;
    int queued = tuya_ble_prov_send_frame(&state, 0x001e, NULL, 0, TUYA_BLE_ENCRYPTION_MODE_NONE);
    int stalled = tuya_ble_prov_tick(&state, 20000);
    log_set_handler(NULL);
    log_set_level(saved_level);
    CHECK(!start && invalid < 0 && !restart && !expired && !queued && stalled < 0);
    CHECK(transport_log_flags == 7);
    return 0;
}

static int permanent_send_failure(void)
{
    tuya_ble_prov_state_t state;
    peer_t peer;
    uint8_t key[16], wire[512], size[2] = {0, 244};
    CHECK(!setup(&state, &peer, key));
    peer.fail = 1;
    CHECK(!deliver(&state, wire, packet(wire, 0, 1, size, 2, key, 0), 20));
    CHECK(!state.handshake_ready && !state.authenticated && !state.tx_count);
    CHECK(state.connection_generation == 1);
    return 0;
}

static int bigdata_netcfg_status(void) {
    tuya_ble_prov_state_t state;
    peer_t peer;
    uint8_t key[16], wire[512], plain[512];
    const uint8_t request[] = {0, 0, 0, 4};

    CHECK(!handshake(&state, &peer, key, 244));
    CHECK(!deliver(&state, wire, packet(wire, 0x801e, 3, request, sizeof(request), key, 0), 244));
    CHECK(peer.complete == 4);
    CHECK(!crypt(0, key, peer.packet[3] + 1, peer.packet[3] + 17,
                 peer.sizes[3] - 17, plain));
    uint16_t len = ((uint16_t)plain[10] << 8) | plain[11];
    CHECK(len == 4 + sizeof("{\"type\":1,\"stage\":0,\"status\":0}") - 1);
    plain[12 + len] = '\0';
    CHECK(plain[8] == 0x80 && plain[9] == 0x1f);
    CHECK(plain[12] == 0 && plain[13] == 1 && plain[14] == 0 && plain[15] == 4);
    CHECK(!strcmp((char *)plain + 16, "{\"type\":1,\"stage\":0,\"status\":0}"));
    return 0;
}

static int bigdata_wifi_list(void) {
    tuya_ble_prov_state_t s; peer_t p; uint8_t key[16], wire[512], plain[1024];
    CHECK(!handshake(&s, &p, key, 244));
    const char request[] = "\0\0\0\3{\"cnt\":99,\"ccode\":\"US\"}";
    CHECK(!deliver(&s, wire, packet(wire, 0x801e, 3, request, sizeof(request)-1, key, 0), 244));
    CHECK(s.wifi_scan_pending && p.requested_count == 20 && !strcmp(p.requested_ccode, "US"));
    tuya_ble_wifi_ap_t aps[] = {
        {.ssid="weak", .rssi=-80, .sec=1}, {.ssid="best\\\"ap", .rssi=-22, .sec=0},
        {.ssid="middle", .rssi=-55, .sec=1},
    };
    CHECK(!tuya_ble_bigdata_wifi_list_complete(&s, p.scan_token, aps, 3));
    CHECK(!crypt(0, key, p.packet[3]+1, p.packet[3]+17, p.sizes[3]-17, plain));
    uint16_t first_len = ((uint16_t)plain[10] << 8) | plain[11];
    plain[12 + first_len] = '\0';
    CHECK(plain[8] == 0x80 && plain[9] == 0x1f && plain[12] == 0 && plain[13] == 1);
    CHECK(plain[14] == 0 && plain[15] == 3);
    CHECK(strstr((char *)plain + 16, "best\\\\\\\"ap") != NULL);
    CHECK(strstr((char *)plain + 16, "best") < strstr((char *)plain + 16, "middle"));
    CHECK(strstr((char *)plain + 16, "middle") < strstr((char *)plain + 16, "weak"));
    CHECK(!s.wifi_scan_pending);
    const char limited[] = "\0\0\0\3{\"cnt\":2}";
    CHECK(!deliver(&s, wire, packet(wire, 0x801e, 4, limited, sizeof(limited)-1, key, 0), 244));
    CHECK(s.wifi_scan_count == 2);
    CHECK(!tuya_ble_bigdata_wifi_list_complete(&s, p.scan_token, aps, 3));
    CHECK(!crypt(0, key, p.packet[4]+1, p.packet[4]+17, p.sizes[4]-17, plain));
    uint16_t limited_len = ((uint16_t)plain[10] << 8) | plain[11];
    CHECK(limited_len < sizeof(plain) - 12);
    plain[12 + limited_len] = '\0';
    CHECK(p.requested_count == 2);
    CHECK(strstr((char *)plain + 16, "weak") == NULL);
    CHECK(!s.wifi_scan_pending);
    return 0;
}

static int bigdata_rejects_stale_and_unpaired(void) {
    tuya_ble_prov_state_t s; peer_t p; uint8_t key[16], wire[512];
    CHECK(!setup(&s, &p, key));
    const char request[] = "\0\0\0\3{}";
    CHECK(!deliver(&s, wire, packet(wire, 0x801e, 1, request, sizeof(request)-1, NULL, 0), 20));
    CHECK(!s.wifi_scan_pending && !p.complete);
    CHECK(!handshake(&s, &p, key, 244));
    CHECK(!deliver(&s, wire, packet(wire, 0x801e, 3, request, sizeof(request)-1, key, 0), 244));
    CHECK(s.wifi_scan_pending);
    tuya_ble_wifi_ap_t ap = {.ssid="router", .rssi=-40, .sec=1};
    CHECK(tuya_ble_bigdata_wifi_list_complete(&s, p.scan_token + 1, &ap, 1) != 0);
    CHECK(s.wifi_scan_pending);
    tuya_ble_prov_close(&s);
    CHECK(tuya_ble_bigdata_wifi_list_complete(&s, p.scan_token, &ap, 1) != 0);
    return 0;
}

static int bigdata_budget_and_failure(void) {
    tuya_ble_prov_state_t s; peer_t p; uint8_t key[16], wire[512], plain[1024];
    CHECK(!handshake(&s, &p, key, 244));
    const char request[] = "\0\0\0\3{\"cnt\":20}";
    CHECK(!deliver(&s, wire, packet(wire, 0x801e, 3, request, sizeof(request)-1, key, 0), 244));
    tuya_ble_wifi_ap_t aps[20];
    memset(aps, 0, sizeof(aps));
    for (unsigned i = 0; i < 20; i++) {
        memset(aps[i].ssid, 'A' + (i % 26), 32); aps[i].ssid[32] = '\0';
        aps[i].rssi = (int8_t)(-20 - i); aps[i].sec = 1;
    }
    CHECK(!tuya_ble_bigdata_wifi_list_complete(&s, p.scan_token, aps, 20));
    CHECK(p.sizes[3] <= TUYA_BLE_TX_BUF_SIZE);
    CHECK(!crypt(0, key, p.packet[3]+1, p.packet[3]+17, p.sizes[3]-17, plain));
    uint16_t len = ((uint16_t)plain[10] << 8) | plain[11];
    CHECK(len <= 4 + TUYA_BLE_WIFI_LIST_JSON_MAX);
    p.scan_rc = -1;
    CHECK(!deliver(&s, wire, packet(wire, 0x801e, 4, request, sizeof(request)-1, key, 0), 244));
    CHECK(!crypt(0, key, p.packet[4]+1, p.packet[4]+17, p.sizes[4]-17, plain));
    uint16_t empty_len = ((uint16_t)plain[10] << 8) | plain[11];
    plain[12 + empty_len] = '\0';
    CHECK(strstr((char *)plain + 16, "{\"wifi_list\":[]}") != NULL);
    return 0;
}

int test_wire(void)
{
    unsigned passed = 0;
#define RUN(fn) do { CHECK(fn() == 0); passed++; } while (0)
    RUN(legacy_roundtrip);
    RUN(device_info_requery_restarts_pairing);
    RUN(unauthorized_and_invalid);
    RUN(malformed_credentials);
    RUN(transport_errors_and_timeout);
    RUN(backpressure);
    RUN(aligned_crc_padding);
    RUN(strict_pkcs7_full_padding_block);
    RUN(entropy_failure);
    RUN(isolated_connections);
    RUN(credential_ack_backpressure);
    RUN(pending_credentials_are_not_replaced);
    RUN(transport_warnings_reach_log_facade);
    RUN(permanent_send_failure);
    RUN(bigdata_netcfg_status);
    RUN(bigdata_wifi_list);
    RUN(bigdata_rejects_stale_and_unpaired);
    RUN(bigdata_budget_and_failure);
#undef RUN
    printf("PASS %u BLE wire scenarios (state storage: %zu bytes)\n", passed, sizeof(tuya_ble_prov_state_t));
    return 0;
}
