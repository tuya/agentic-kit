/* Included in the existing integration binary to share its independent peer
 * decoder and callback assertions without adding another fixture target. */
static mbedtls_x509_crt keyed_bundle;
static int keyed_bundle_calls;

static void keyed_attach_bundle(void *config)
{
    keyed_bundle_calls++;
    mbedtls_ssl_conf_ca_chain(config, &keyed_bundle, NULL);
}

static void keyed_check_handshake(tai_ctx_t *ctx, uint16_t first_seq)
{
    image_capture_reset(0);
    memcpy(image_capture.wire, keyed_peer.handshake, keyed_peer.handshake_len);
    image_capture.wire_len = keyed_peer.handshake_len;
    uint8_t app[8192];
    size_t offset = 0;
    captured_pkt_t pkt;
    int frames;
    int rc = image_peer_next(ctx, &offset, &first_seq, app, sizeof(app), &pkt, &frames);
    CHECK_EQ_INT(rc, 0);
    if (rc) return;
    CHECK_EQ_INT(frames, 1);
    CHECK_EQ_INT(pkt.pkt_type, 1);
    CHECK_EQ_INT(pkt.payload_len, 0);
    CHECK_EQ_INT(pkt.attr_count, 5);
    int suites = 0;
    for (int i = 0; i < pkt.attr_count; i++) {
        const tai_attr_t *a = &pkt.attrs[i];
        const uint16_t types[] = {11, 12, 10, 15, 20};
        if (i < 5) CHECK_EQ_INT(a->type, types[i]);
        if (a->type == 11) CHECK(a->len == 1 && a->value[0] == TAI_CLIENT_DEVICE);
        if (a->type == 12) CHECK(a->len == 12 && memcmp(a->value, "keyed-device", 12) == 0);
        if (a->type == 15) CHECK(a->len == 4 && image_wire_uint(a->value, 4) == AGENTIC_KIT_TAI_MAX_FRAGMENT_PAYLOAD);
        if (a->type == 20) CHECK(a->len == 4 && image_wire_uint(a->value, 4) == 60000);
        if (a->type != 10) continue;
        suites++;
        CHECK_EQ_INT(a->len, 36);
        if (a->len == 36) {
            CHECK_EQ_INT(a->value[0], 0);
            CHECK(memcmp(a->value + 1, ctx->encrypt_random, 32) == 0);
            const uint8_t tail[] = {4, 3, 3};
            CHECK(memcmp(a->value + 33, tail, 3) == 0);
            uint8_t zero[32] = {0};
            CHECK(memcmp(a->value + 1, zero, 32) != 0);
        }
    }
    CHECK_EQ_INT(suites, 1);
    rc = image_peer_next(ctx, &offset, &first_seq, app, sizeof(app), &pkt, &frames);
    CHECK_EQ_INT(rc, 0);
    if (rc) return;
    CHECK_EQ_INT(frames, 1);
    CHECK_EQ_INT(pkt.pkt_type, 7);
    const uint8_t ids[] = {0,6,0,1,0,3,0,5,0,6,0,2,0,4,0,6};
    CHECK_EQ_INT(pkt.payload_len, sizeof(ids));
    if (pkt.payload_len == sizeof(ids)) CHECK(memcmp(pkt.payload, ids, sizeof(ids)) == 0);
    CHECK_EQ_INT(offset, image_capture.wire_len);
}

static struct { int packets, error; } keyed_capture;

static void *keyed_capture_uplink(void *arg)
{
    (void)arg;
    for (int i = 0; i < keyed_capture.packets;) {
        size_t pos = image_capture.wire_len;
        int n = keyed_read_frame(&keyed_peer, image_capture.wire + pos,
                                sizeof(image_capture.wire) - pos);
        if (n < 0) { keyed_capture.error = -1; break; }
        unsigned frag = image_capture.wire[pos] >> 6;
        image_capture.wire_len += (size_t)n;
        if (frag == 0 || frag == 3) i++;
    }
    return NULL;
}

static void test_keyed_tls_connection(void)
{
    SECTION("keyed_verified_TLS_handshake_and_all_image_APIs");
    static tai_ctx_t storage;
    static uint8_t image[IMAGE_UPLINK_LARGE];
    for (size_t i = 0; i < sizeof(image); i++) image[i] = (uint8_t)(i * 37 + i / 251);
    uint8_t previous_random[32] = {0};
    for (int mode = 0; mode < 3; mode++) {
        int bundle = mode == 1;
        int started = keyed_peer_start(mode == 2 ? 1 : bundle ? 4 : 0);
        CHECK_EQ_INT(started, 0);
        if (started) return;
        mbedtls_x509_crt_init(&keyed_bundle);
        CHECK_EQ_INT(mbedtls_x509_crt_parse(&keyed_bundle,
                     (const uint8_t *)keyed_ca, strlen(keyed_ca) + 1), 0);
        keyed_bundle_calls = 0;
        tai_config_t cfg = {
            .pal = tai_pal_posix(), .host = "127.0.0.1", .tls_sni = "localhost",
            .port = keyed_peer.port, .device_id = "keyed-device",
            .image_key = image_uplink_key, .ca_cert = bundle ? NULL : keyed_ca,
            .cert_bundle_attach = bundle ? keyed_attach_bundle : NULL,
            .event_user_data_json = "{}", .on_disconnect = on_disconnect,
        };
        st_reset();
        tai_ctx_t *ctx = tai_ctx_init(&storage, &cfg);
        CHECK(ctx != NULL);
        int rc = ctx ? tai_connect(ctx) : TAI_ERR_MEM;
        pthread_join(keyed_peer.thread, NULL);
        CHECK_EQ_INT(rc, TAI_OK);
        CHECK_EQ_INT(keyed_peer.error, 0);
        CHECK_EQ_INT(keyed_bundle_calls, bundle);
        if (rc == TAI_OK) {
            CHECK(ctx->tls != NULL && ctx->raw_tcp == NULL && ctx->connected);
            CHECK_EQ_INT(ctx->frame_ver, 3);
            CHECK_EQ_INT(ctx->sig_len, 0);
            CHECK_EQ_INT(ctx->sign_level, 0);
            keyed_check_handshake(ctx, 1);
            CHECK(memcmp(previous_random, ctx->encrypt_random, 32) != 0);
            memcpy(previous_random, ctx->encrypt_random, 32);
            for (int api = 0; api < 3; api++) {
                image_capture_reset(0);
                keyed_capture.packets = api == 2 ? 2 : api == 1 ? 5 : 4;
                keyed_capture.error = 0;
                pthread_t reader;
                int trc = pthread_create(&reader, NULL, keyed_capture_uplink, NULL);
                CHECK_EQ_INT(trc, 0);
                if (trc) break;
                uint16_t seq = (uint16_t)(ctx->seq + 1);
                size_t len = api == 2 ? IMAGE_UPLINK_LARGE : 259;
                CHECK_EQ_INT(image_uplink_send(ctx, api, image, len), TAI_OK);
                pthread_join(reader, NULL);
                CHECK_EQ_INT(keyed_capture.error, 0);
                uint8_t iv[16];
                image_peer_check(ctx, api, image, len, seq, iv);
            }
            tai_disconnect(ctx);
            CHECK_EQ_INT(g_st.disconnect_calls, 0);
        }
        if (ctx) tai_ctx_deinit(ctx);
        keyed_peer_close();
        mbedtls_x509_crt_free(&keyed_bundle);
    }
}

static void test_keyed_tls_without_trust(void)
{
    SECTION("keyed_TLS_runs_unverified_when_no_CA_or_bundle");
    static tai_ctx_t storage;
    /* No CA and no bundle: TLS still runs, the peer is unverified, and connect
     * warns but proceeds -- the same rule the IoT path follows. */
    int started = keyed_peer_start(0);
    CHECK_EQ_INT(started, 0);
    if (started) return;
    st_reset();
    tai_config_t cfg = {
        .pal = tai_pal_posix(), .host = "127.0.0.1", .tls_sni = "localhost",
        .port = keyed_peer.port, .device_id = "keyed-device",
        .image_key = image_uplink_key, .ca_cert = NULL,
        .event_user_data_json = "{}", .on_disconnect = on_disconnect,
    };
    tai_ctx_t *ctx = tai_ctx_init(&storage, &cfg);
    CHECK(ctx != NULL);
    int rc = ctx ? tai_connect(ctx) : TAI_ERR_MEM;
    pthread_join(keyed_peer.thread, NULL);
    CHECK_EQ_INT(rc, TAI_OK);
    CHECK_EQ_INT(keyed_peer.error, 0);
    if (rc == TAI_OK) {
        CHECK(ctx->tls != NULL && ctx->raw_tcp == NULL && ctx->connected);
        keyed_check_handshake(ctx, 1);
        tai_disconnect(ctx);
        CHECK_EQ_INT(g_st.disconnect_calls, 0);
    }
    if (ctx) tai_ctx_deinit(ctx);
    keyed_peer_close();
}

static void test_keyed_tls_worker_unknown_version(void)
{
    SECTION("keyed_TLS_worker_rejects_unsupported_Frame_version");
    static tai_ctx_t storage;
    pal_t pal;
    tai_ctx_t *ctx = image_parser_setup(&storage, &pal, 1);
    if (!ctx) return;
    /* Version 2 and 3 are both valid v2.1 (the cloud sends its control/ack
     * Frames as version 2). An unsupported version nibble is fatal, and the
     * following valid Frame must not be delivered once the first has done so. */
    const uint8_t wire[] = {
        0x21,0,2,0,6, 0x44,0,4,0,0,'x',
        0x23,0,3,0,6, 0x44,0,4,0,0,'y'
    };
    CHECK_EQ_INT(keyed_peer_write(wire, sizeof(wire)), 0);
    image_parser_check_fatal(ctx, TAI_PROTO_ERR_BAD_VERSION, 0, 0);
    tai_ctx_deinit(ctx);
}

static void test_keyed_tls_rejections(void)
{
    SECTION("keyed_TLS_signed_ack_bad_suite_and_untrusted_peer");
    static tai_ctx_t storage;
    char wrong_ca[4096], path[4096];
    const char *slash = strrchr(__FILE__, '/');
    CHECK(slash != NULL);
    if (!slash) return;
    snprintf(path, sizeof(path), "%.*s/../../../third_party/mbedtls/framework/data_files/test-ca2.crt",
             (int)(slash - __FILE__), __FILE__);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f) return;
    size_t n = fread(wrong_ca, 1, sizeof(wrong_ca) - 1, f);
    CHECK(!ferror(f) && feof(f));
    fclose(f);
    wrong_ca[n] = 0;
    /* Modes 4/5 reject CA/hostname via ca_cert; 6/7 repeat via the bundle. */
    for (int mode = 2; mode <= 7; mode++) {
        int started = keyed_peer_start(mode <= 3 ? mode : 0);
        CHECK_EQ_INT(started, 0);
        if (started) return;
        int bundle = mode >= 6;
        const char *ca = mode == 4 || mode == 6 ? wrong_ca : keyed_ca;
        mbedtls_x509_crt_init(&keyed_bundle);
        if (bundle)
            CHECK_EQ_INT(mbedtls_x509_crt_parse(&keyed_bundle,
                         (const uint8_t *)ca, strlen(ca) + 1), 0);
        keyed_bundle_calls = 0;
        st_reset();
        tai_config_t cfg = {
            .pal = tai_pal_posix(), .host = "127.0.0.1", .port = keyed_peer.port,
            .tls_sni = mode == 5 || mode == 7 ? "wrong-host.invalid" : "localhost",
            .device_id = "keyed-device", .image_key = parser_image_key,
            .ca_cert = bundle ? NULL : ca,
            .cert_bundle_attach = bundle ? keyed_attach_bundle : NULL,
            .on_disconnect = on_disconnect,
        };
        tai_ctx_t *ctx = tai_ctx_init(&storage, &cfg);
        CHECK(ctx != NULL);
        int rc = ctx ? tai_connect(ctx) : TAI_ERR_MEM;
        pthread_join(keyed_peer.thread, NULL);
        CHECK_EQ_INT(rc, mode >= 4 ? TAI_ERR_TLS : TAI_ERR_PROTO);
        CHECK_EQ_INT(keyed_bundle_calls, bundle);
        if (mode >= 4) {
            CHECK(keyed_peer.error != 0);
            CHECK_EQ_INT(keyed_peer.handshake_len, 0);
        } else CHECK_EQ_INT(keyed_peer.error, 0);
        if (ctx) {
            CHECK(!ctx->connected && !ctx->running && !ctx->thread_handle && !ctx->tls);
            CHECK(memcmp(ctx->image_key, parser_image_key, 16) == 0);
            tai_ctx_deinit(ctx);
        }
        CHECK_EQ_INT(g_st.disconnect_calls, 0);
        keyed_peer_close();
        mbedtls_x509_crt_free(&keyed_bundle);
    }
}
