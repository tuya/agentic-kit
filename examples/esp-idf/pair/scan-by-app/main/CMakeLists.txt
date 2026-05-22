idf_component_register(
    SRCS
        "main.c"
        "scan_by_app_pair_demo.c"
        "qrcodegen.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_event agentic_kit
)
