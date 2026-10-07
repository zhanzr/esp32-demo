/**
 ******************************************************************************
 * @file        main.c
 * @version     V1.0
 * @brief       MIPI CAMERA实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "led.h"
#include "myiic.h"
#include "lcd.h"
#include "mipi_cam.h"


void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();     /* 初始化NVS */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    led_init();                 /* LED初始化 */
    myiic_init();               /* MYIIC初始化 */
    lcd_init();                 /* LCD屏初始化 */

    lcd_show_string(30, 50,  200, 16, 16, "ESP32-P4", RED);
    lcd_show_string(30, 70,  200, 16, 16, "OV5647 LCD PREVIEW", RED);
    lcd_show_string(30, 90,  200, 16, 16, "WKS SMART", RED);
    vTaskDelay(pdMS_TO_TICKS(500));
    lcd_clear(WHITE);
    
    mipi_cam_init();            /* 摄像头初始化 */

    while (1)
    {
        LED0_TOGGLE();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}