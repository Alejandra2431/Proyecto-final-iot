#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nube.h"

typedef struct {
    const char *broker;
    const char *raiz;
    const char *dispositivo;
    const char *arranque;
    EventGroupHandle_t eventos_wifi;
    void (*leer_mediciones)(nube_metadatos_t *datos);
} avisos_mqtt_config_t;

/* Inicializa el cliente y una tarea de telemetría. No guarda fotos JPEG. */
esp_err_t avisos_mqtt_iniciar(const avisos_mqtt_config_t *config);

/* Copia el aviso sin esperar a la red. Llamar solo después de NUBE_OK.
 * Si la cola se llena, se pierde el aviso, no la foto que ya está en Drive. */
bool avisos_mqtt_foto(const nube_foto_t *foto, const nube_informe_t *informe,
                     int64_t captura_us);

void avisos_mqtt_informe(void);
