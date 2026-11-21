/* Adaptación del cliente MQTT de Entrega6: la red corre fuera del visor. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "red_wifi.h"
#include "avisos_mqtt.h"

#define EV_MQTT_OK BIT0
#define TELEMETRIA_CADA_US (10LL * 1000000)
#define COLA_AVISOS 8

typedef struct {
    uint32_t numero;
    uint16_t ancho;
    uint16_t alto;
    size_t bytes;
    int64_t captura_us;
    int64_t captura_epoch_ms;
    int64_t fila_epoch_ms;
    uint32_t subida_ms;
    uint32_t captura_a_respuesta_ms;
    uint32_t intentos;
    nube_metadatos_t datos;
    char id_drive[96];
} aviso_foto_t;

static const char *TAG = "avisos_mqtt";
static avisos_mqtt_config_t s_cfg;
static esp_mqtt_client_handle_t s_cliente;
static EventGroupHandle_t s_eventos;
static QueueHandle_t s_fotos;
static char s_tema_telemetria[128];
static char s_tema_foto[128];

static void al_evento(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    esp_mqtt_event_handle_t evento = datos;
    if (id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(s_eventos, EV_MQTT_OK);
        ESP_LOGI(TAG, "conectado; temas %s y %s", s_tema_telemetria, s_tema_foto);
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(s_eventos, EV_MQTT_OK);
        ESP_LOGW(TAG, "desconectado; el cliente reintentara");
    } else if (id == MQTT_EVENT_PUBLISHED) {
        ESP_LOGI(TAG, "broker confirmo mensaje %d (no confirma recepcion del panel)",
                 evento->msg_id);
    } else if (id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "error de conexion MQTT; revisar red y broker");
    }
}

static void valores(const nube_metadatos_t *datos, char *rssi, size_t nr,
                    char *temperatura, size_t nt)
{
    strlcpy(rssi, "null", nr);
    strlcpy(temperatura, "null", nt);
    if (datos->rssi_valido) {
        snprintf(rssi, nr, "%d", datos->rssi_dbm);
    }
    if (datos->temperatura_valida && isfinite(datos->temperatura_c)) {
        snprintf(temperatura, nt, "%.1f", datos->temperatura_c);
    }
}

static bool identificador(const char *texto)
{
    if (texto == NULL || *texto == '\0') return false;
    for (const char *p = texto; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    }
    return true;
}

static void tarea_avisos(void *arg)
{
    aviso_foto_t foto;
    bool pendiente = false;
    uint32_t secuencia = 0;
    int64_t ultima_telemetria_us = -TELEMETRIA_CADA_US;
    while (1) {
        const bool wifi = (xEventGroupGetBits(s_cfg.eventos_wifi) & EV_WIFI_OK) != 0;
        const bool mqtt = (xEventGroupGetBits(s_eventos) & EV_MQTT_OK) != 0;
        if (!wifi || !mqtt) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        char json[896];
        char rssi[16], temperatura[16];
        if (!pendiente) pendiente = xQueueReceive(s_fotos, &foto, 0) == pdTRUE;
        if (pendiente) {
            valores(&foto.datos, rssi, sizeof(rssi), temperatura, sizeof(temperatura));
            const int64_t edad_ms = (esp_timer_get_time() - foto.captura_us) / 1000;
            const int n = snprintf(json, sizeof(json),
                "{\"v\":1,\"dispositivo\":\"%s\",\"arranque\":\"%s\","
                "\"numero\":%" PRIu32 ",\"motivo\":\"%s\",\"rssi_dbm\":%s,"
                "\"temperatura_c\":%s,\"id_drive\":\"%s\",\"ancho\":%u,\"alto\":%u,"
                "\"bytes\":%u,\"edad_ms\":%" PRIi64 ","
                "\"captura_epoch_ms\":%" PRIi64 ",\"fila_epoch_ms\":%" PRIi64 ","
                "\"subida_ms\":%" PRIu32 ",\"captura_a_respuesta_ms\":%" PRIu32 ","
                "\"intentos\":%" PRIu32 "}",
                s_cfg.dispositivo, s_cfg.arranque, foto.numero, foto.datos.motivo,
                rssi, temperatura, foto.id_drive, foto.ancho, foto.alto,
                (unsigned)foto.bytes, edad_ms, foto.captura_epoch_ms, foto.fila_epoch_ms,
                foto.subida_ms, foto.captura_a_respuesta_ms, foto.intentos);
            if (n > 0 && n < sizeof(json)) {
                const int id = esp_mqtt_client_enqueue(s_cliente, s_tema_foto, json, n, 1, 0, true);
                if (id >= 0) {
                    ESP_LOGI(TAG, "foto %" PRIu32 ": aviso encolado id %d, edad %" PRIi64 " ms",
                             foto.numero, id, edad_ms);
                    pendiente = false;
                }
            } else {
                ESP_LOGE(TAG, "aviso demasiado largo, descartado");
                pendiente = false;
            }
        }
        const int64_t ahora = esp_timer_get_time();
        if (ahora - ultima_telemetria_us >= TELEMETRIA_CADA_US) {
            nube_metadatos_t datos = {0};
            s_cfg.leer_mediciones(&datos);
            valores(&datos, rssi, sizeof(rssi), temperatura, sizeof(temperatura));
            const int n = snprintf(json, sizeof(json),
                "{\"v\":1,\"dispositivo\":\"%s\",\"arranque\":\"%s\","
                "\"secuencia\":%" PRIu32 ",\"uptime_ms\":%" PRIi64 ","
                "\"rssi_dbm\":%s,\"temperatura_c\":%s}",
                s_cfg.dispositivo, s_cfg.arranque, ++secuencia, ahora / 1000, rssi, temperatura);
            if (n > 0 && n < sizeof(json)) {
                /* Telemetría QoS 0: no acumular un historial viejo sin red.
                 * enqueue deja el envío al cliente MQTT, no a esta tarea. */
                const int id = esp_mqtt_client_enqueue(s_cliente, s_tema_telemetria,
                                                       json, n, 0, 0, true);
                if (id < 0) ESP_LOGW(TAG, "telemetria descartada: buzon MQTT lleno");
            }
            ultima_telemetria_us = ahora;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t avisos_mqtt_iniciar(const avisos_mqtt_config_t *config)
{
    if (!config || !config->broker || !config->raiz || !config->eventos_wifi ||
        !config->leer_mediciones || !identificador(config->dispositivo) ||
        !identificador(config->arranque) || !identificador(config->raiz)) return ESP_ERR_INVALID_ARG;
    s_cfg = *config;
    if (snprintf(s_tema_foto, sizeof(s_tema_foto), "%s/%s/foto",
                 s_cfg.raiz, s_cfg.dispositivo) >= sizeof(s_tema_foto) ||
        snprintf(s_tema_telemetria, sizeof(s_tema_telemetria), "%s/%s/telemetria",
                 s_cfg.raiz, s_cfg.dispositivo) >= sizeof(s_tema_telemetria)) return ESP_ERR_INVALID_SIZE;
    s_eventos = xEventGroupCreate();
    s_fotos = xQueueCreate(COLA_AVISOS, sizeof(aviso_foto_t));
    if (!s_eventos || !s_fotos) return ESP_ERR_NO_MEM;
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_cfg.broker,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = s_cfg.dispositivo,
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 10000 + esp_random() % 5000,
        .task.priority = 4,
        .task.stack_size = 6144,
        .buffer.size = 1024,
        .outbox.limit = 4096,
    };
    s_cliente = esp_mqtt_client_init(&cfg);
    if (!s_cliente) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_cliente, MQTT_EVENT_ANY, al_evento, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_cliente));
    if (xTaskCreate(tarea_avisos, "avisos", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

bool avisos_mqtt_foto(const nube_foto_t *foto, const nube_informe_t *informe,
                     int64_t captura_us, uint32_t subida_ms,
                     uint32_t captura_a_respuesta_ms, uint32_t intentos)
{
    if (!s_fotos || !identificador(informe->id_drive) ||
        !foto->metadatos.motivo ||
        (strcmp(foto->metadatos.motivo, "boton") &&
         strcmp(foto->metadatos.motivo, "periodica") &&
         strcmp(foto->metadatos.motivo, "movimiento"))) {
        ESP_LOGW(TAG, "sin referencia valida para avisar de la foto");
        return false;
    }
    aviso_foto_t aviso = {
        .numero = foto->numero, .ancho = foto->ancho, .alto = foto->alto,
        .bytes = foto->largo, .captura_us = captura_us, .datos = foto->metadatos,
        .captura_epoch_ms = foto->captura_epoch_ms, .fila_epoch_ms = informe->fila_epoch_ms,
        .subida_ms = subida_ms, .captura_a_respuesta_ms = captura_a_respuesta_ms,
        .intentos = intentos,
    };
    strlcpy(aviso.id_drive, informe->id_drive, sizeof(aviso.id_drive));
    if (xQueueSend(s_fotos, &aviso, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cola de avisos llena; foto %" PRIu32 " sigue guardada en Drive", foto->numero);
        return false;
    }
    return true;
}

void avisos_mqtt_informe(void)
{
    ESP_LOGI(TAG, "MQTT %s; avisos en cola %u/%d; buzon %d B",
        (xEventGroupGetBits(s_eventos) & EV_MQTT_OK) ? "SI" : "NO",
        (unsigned)uxQueueMessagesWaiting(s_fotos), COLA_AVISOS,
        esp_mqtt_client_get_outbox_size(s_cliente));
}
