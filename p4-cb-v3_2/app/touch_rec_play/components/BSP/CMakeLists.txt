set(src_dirs
            LED
            LCD
            MYES8311
            MYI2S
            MYIIC
            TOUCH)

set(include_dirs
            LED
            LCD
            MYES8311
            MYI2S
            MYIIC
            TOUCH)

set(requires
            esp_driver_gpio
            esp_driver_i2s
            esp_driver_i2c
            esp_lcd
            esp_common
            es8311
            esp_codec_dev)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
