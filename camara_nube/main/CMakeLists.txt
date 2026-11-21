idf_component_register(SRCS "camara_nube_main.c" "camara.c" "imagen.c"
                            "informe.c" "nube.c" "pantalla.c" "red_wifi.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_spi esp_lcd
                                esp_psram esp_timer esp_wifi esp_netif
                                esp_event nvs_flash esp_http_client mbedtls)
