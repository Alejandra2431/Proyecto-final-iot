idf_component_register(SRCS "movimiento.c" "camara_nube_main.c" "camara.c" "imagen.c"
                            "informe.c" "nube.c" "pantalla.c" "red_wifi.c" "movimiento.c"
                            "avisos_mqtt.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_spi esp_driver_tsens esp_lcd
                                esp_psram esp_timer esp_wifi esp_netif
                                esp_event nvs_flash esp_http_client mbedtls mqtt)
