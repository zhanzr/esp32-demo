idf_component_register(
    SRCS "main.c" "dhry_1.c" "dhry_2.c" "utils.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio esp_timer esp_hw_support
)

target_compile_options(__idf_main PRIVATE -Ofast -ffp-contract=fast -funroll-loops)
