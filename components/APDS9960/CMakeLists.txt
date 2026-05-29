idf_component_register(SRCS "apds9960.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver espressif__apds9960 esp_timer)
