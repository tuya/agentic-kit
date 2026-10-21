#include "role_demo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *load_ca(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *pem = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long n = ftell(f);
        if (n > 0 && n <= 128 * 1024 && !fseek(f, 0, SEEK_SET)) {
            pem = malloc((size_t)n + 1);
            if (pem && fread(pem, 1, (size_t)n, f) == (size_t)n) pem[n] = 0;
            else { free(pem); pem = NULL; }
        }
    }
    fclose(f);
    return pem;
}
static bool credential(char *out, size_t size, const char *key)
{
    const char *s = getenv(key);
    if (!s || !s[0] || strlen(s) >= size) return false;
    strcpy(out, s);
    return true;
}
static bool region(const char *name, iot_region_t *out)
{
    const char *names[] = {"AY", "AZ", "UE", "EU", "WE", "IN", "SG"};
    const iot_region_t values[] = {AY, AZ, UEAZ, EU, WEAZ, IN, SG};
    if (!name) name = "AY";
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        if (!strcmp(name, names[i])) { *out = values[i]; return true; }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "--help")) { role_demo_usage(); return argc < 2 ? 1 : 0; }
    /* This tool operates an existing device identity; it must not overwrite
     * that device's real firmware/SDK version metadata while merely querying. */
    iot_client_config_t cfg = { .env = PROD, .mqtt_disable_auto_connect = true,
        .skip_version_report = true };
    const char *ca_file = getenv("TUYA_CA_FILE");
    if (!credential(cfg.devid, sizeof(cfg.devid), "TUYA_DEVICE_ID") ||
        !credential(cfg.secret_key, sizeof(cfg.secret_key), "TUYA_SECRET_KEY") ||
        !credential(cfg.local_key, sizeof(cfg.local_key), "TUYA_LOCAL_KEY") ||
        !ca_file || !region(getenv("TUYA_REGION"), &cfg.region)) {
        fprintf(stderr, "Provide valid device credentials, TUYA_REGION and TUYA_CA_FILE in the environment.\n");
        return 1;
    }
    char *ca = load_ca(ca_file);
    if (!ca) { fprintf(stderr, "Cannot read TUYA_CA_FILE.\n"); return 1; }
    cfg.cacert = ca;
    int rc = iot_init_default();
    iot_client_t *iot = rc ? NULL : iot_client_init(&cfg);
    if (!iot) { free(ca); return 1; }
    tuya_biz_client_t *biz = NULL;
    rc = tuya_biz_client_create(iot, "zh", &biz);
    if (!rc) rc = role_demo_execute(biz, argc - 1, (const char *const *)(argv + 1));
    tuya_biz_client_destroy(biz);
    iot_client_deinit(iot);
    free(ca);
    return rc ? 1 : 0;
}
