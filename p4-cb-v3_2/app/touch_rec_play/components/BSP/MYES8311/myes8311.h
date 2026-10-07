/**
 ******************************************************************************
 * @file        myes8311.h
 * @version     V1.0
 * @brief       ES8311驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#ifndef __MYES8311_H_
#define __MYES8311_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "hal/gpio_types.h"
#include "math.h"
#include "string.h"
#include "es8311.h"
#include "myi2s.h"
#include "esp_err.h" 
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_vol.h"


/* 引脚定义 */
#define PA_CTRL_GPIO_PIN          GPIO_NUM_11                /* 功放使能引脚连接的GPIO端口 */
#define ES8311_IIC_NUM_PORT       I2C_NUM_0                  /* IIC0 */
#define ES8311_IIC_SDA_GPIO_PIN   GPIO_NUM_28                /* IIC0_SDA引脚 */
#define ES8311_IIC_SCL_GPIO_PIN   GPIO_NUM_29                /* IIC0_SCL引脚 */

/* 声明函数 */
uint8_t myes8311_init(void);                                 /* ES8311初始化 */

#endif
