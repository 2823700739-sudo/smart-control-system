idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES APDS9960
                    REQUIRES CAMERA
                    REQUIRES led
                    REQUIRES driver
                    REQUIRES LCD
                    REQUIRES SPI
                    REQUIRES nvs_flash
                    REQUIRES esp_wifi
                    REQUIRES esp_netif
                    REQUIRES esp_timer
                    )