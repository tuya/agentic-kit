idf_component_register(
    SRCS
        "main.c"
        "tuya_ble_nimble.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt json mbedtls nvs_flash esp_wifi esp_event esp_netif agentic_kit
    PRIV_REQUIRES
        spi_flash
)
