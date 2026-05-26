idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES apds9960)  # 只需要原生驱动，无其他依赖)