# Reusable example component: add this directory to EXTRA_COMPONENT_DIRS in an
# existing activated device project. No app_main, WiFi config, or board driver.
idf_component_register(
    SRCS "role_management.c"
    INCLUDE_DIRS "."
    REQUIRES agentic_kit freertos
)
