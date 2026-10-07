idf_component_register(
                        SRC_DIRS
                            "."
                            "APP/MIPI_CAM"
                        INCLUDE_DIRS
                            "."
                            "APP/MIPI_CAM"
                        REQUIRES
                            esp_wifi esp_netif esp_event nvs_flash freertos
                            esp_http_server esp_timer esp_driver_gpio
                            esp_driver_tsens esp_driver_jpeg esp_driver_ppa driver
                            esp_wifi_remote BSP
                        )
