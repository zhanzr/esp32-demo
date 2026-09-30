idf_component_register(
    SRCS "classifier.c" "main.c" "app_httpd.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash freertos esp_http_server esp_timer esp_driver_ledc
)
