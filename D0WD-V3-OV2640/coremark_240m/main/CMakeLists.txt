idf_component_register(SRCS "main.c" "utils.c" "core_portme.c"
                            "../coremark_1_0_1/core_list_join.c"
                            "../coremark_1_0_1/core_main.c"
                            "../coremark_1_0_1/core_matrix.c"
                            "../coremark_1_0_1/core_state.c"
                            "../coremark_1_0_1/core_util.c"
                    PRIV_INCLUDE_DIRS "." "../coremark_1_0_1"
                    REQUIRES driver esp_timer esp_hw_support)

target_compile_options(__idf_main PRIVATE -Ofast -ffp-contract=fast -funroll-all-loops -Wno-maybe-uninitialized)
