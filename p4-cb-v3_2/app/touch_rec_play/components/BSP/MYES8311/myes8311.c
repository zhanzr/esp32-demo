/**
 ******************************************************************************
 * @file        myes8311.c
 * @version     V1.0
 * @brief       ES8311驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "myes8311.h"
#include "myiic.h"

const char* es8311_tag = "es8311";  /* ES8311日志标签 */


/**
 * @brief       ES8311初始化
 * @param       无
 * @retval      0,初始化正常
 *              其他,错误代码
 */
uint8_t myes8311_init(void)
{
	/* 初始化I2C总线 */
    /* Own I2C0 master bus for the codec. The GT911 touch (myiic) creates a
     * second bus object on the same physical wires (GPIO 28/29); contention
     * is avoided because the codec is controlled once at init while the
     * touch is not scanned during record/playback. */
    i2c_master_bus_handle_t i2c_bus_handle = NULL;
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = ES8311_IIC_NUM_PORT,
        .sda_io_num = ES8311_IIC_SDA_GPIO_PIN,
        .scl_io_num = ES8311_IIC_SCL_GPIO_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_cfg, &i2c_bus_handle));

    /* 使用I2C总线句柄创建控制接口 */
    audio_codec_i2c_cfg_t i2c_cfg = {          /* 音频编解码器I2C配置 */
        .port = ES8311_IIC_NUM_PORT,           /* I2C端口号 */
        .addr = ES8311_CODEC_DEFAULT_ADDR,     /* ES8311设备地址 */
        .bus_handle = i2c_bus_handle,          /* I2C总线句柄 */
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg); /* 创建I2C控制接口 */
    assert(ctrl_if);

    /* 使用I2S总线句柄创建数据接口 */
    audio_codec_i2s_cfg_t i2s_cfg = {          /* 音频编解码器I2S配置 */
        .port = I2S_NUM,                       /* I2S端口号 */
        .rx_handle = rx_handle,                /* I2S接收通道句柄 */
        .tx_handle = tx_handle,                /* I2S发送通道句柄 */
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg); /* 创建I2S数据接口 */
    assert(data_if);

    /* 创建ES8311接口句柄 */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio(); /* 创建GPIO接口 */
    assert(gpio_if);
    es8311_codec_cfg_t es8311_cfg = {               /* ES8311编解码器配置 */
        .ctrl_if = ctrl_if,                         /* 控制接口 */
        .gpio_if = gpio_if,                         /* GPIO接口 */
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH, /* 编解码器工作模式(录音和播放) */
        .master_mode = false,                       /* 从机模式 */
        .use_mclk = I2S_MCK_IO >= 0,                /* 是否使用MCLK主时钟 */
        .pa_pin = PA_CTRL_GPIO_PIN,                 /* 功放控制引脚 */
        .pa_reverted = false,                       /* 功放控制极性(不反转) */
        .hw_gain = {                                /* 硬件增益配置 */
            .pa_voltage = 5.0,                      /* 功放电压 */
            .codec_dac_voltage = 3.3,               /* 编解码器DAC电压 */
        },
        .mclk_div = I2S_MCLK_MULTIPLE,              /* MCLK分频系数 */
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg); /* 创建ES8311编解码器接口 */
    assert(es8311_if);

    /* 使用ES8311接口句柄和数据接口创建顶层编解码器句柄 */
    esp_codec_dev_cfg_t dev_cfg = {            /* 编解码器设备配置 */
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, /* 设备类型(输入输出) */
        .codec_if = es8311_if,                 /* 编解码器接口 */
        .data_if = data_if,                    /* 数据接口 */
    };
    esp_codec_dev_handle_t codec_handle = esp_codec_dev_new(&dev_cfg); /* 创建编解码器设备句柄 */
    assert(codec_handle);

    /* 指定采样配置并打开设备 */
    esp_codec_dev_sample_info_t sample_cfg = {       /* 采样信息配置 */
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT, /* 每采样位数 */
        .channel = 2,                                /* 声道数 */
        .channel_mask = 0x03,                        /* 声道掩码(左右声道) */
        .sample_rate = I2S_SAMPLE_RATE,              /* 采样率 */
    };
    if (esp_codec_dev_open(codec_handle, &sample_cfg) != ESP_CODEC_DEV_OK) { /* 打开编解码器设备 */
        ESP_LOGE(es8311_tag, "Open codec device failed"); /* 打开设备失败 */
        return ESP_FAIL;
    }

    /* 设置初始音量和增益 */
    if (esp_codec_dev_set_out_vol(codec_handle, EXAMPLE_VOICE_VOLUME) != ESP_CODEC_DEV_OK) { /* 设置输出音量 */
        ESP_LOGE(es8311_tag, "set output volume failed"); /* 设置输出音量失败 */
        return ESP_FAIL;
    }

	if (esp_codec_dev_set_in_gain(codec_handle, 30) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(es8311_tag, "set input gain failed");
        return ESP_FAIL;
    }

    return 0;
}