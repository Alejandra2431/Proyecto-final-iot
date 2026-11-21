/*
 * camara_nube — Clase 7 (Sistemas Embebidos, UCU 2026)
 *
 * Ejemplo 2 y base de la App 7. La cámara del ejemplo 1, conectada: cada
 * foto se guarda en Google Drive, a través de un Apps Script, por HTTPS, y
 * queda anotada en una hoja de cálculo.
 *
 *   [cam_task p23]  (driver) cuadros JPEG en PSRAM, sin parar
 *        │ esp_camera_fb_get()
 *        ▼
 *   [visor p2] ──foto: copia en PSRAM──▶ cola de fotos ──▶ [subida p5] ──HTTPS──▶ Apps Script
 *    el visor en pantalla;               4 huecos           espera EV_WIFI_OK;       ├ Drive
 *    BOOT o el temporizador                                 reintenta con espera     └ Hoja
 *    piden una foto                                         creciente
 *
 *   [conexion p3]  (red_wifi.c) reconexión WiFi con espera creciente
 *   [timer]        informe cada 30 s; una foto cada FOTO_CADA_S si no es 0
 *
 * Hardware: el del ejemplo 1 (placa principal + cámara + pantalla, sin placa
 * de audio y sin el jumper RGB) y una red WiFi de 2,4 GHz con Internet.
 * La nube: el Apps Script de ../apps_script/, instalado con tu cuenta.
 */

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_attr.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "camara.h"
#include "avisos_mqtt.h"
#include "imagen.h"
#include "movimiento.h"
#include "informe.h"
#include "nube.h"
#include "pantalla.h"
#include "red_wifi.h"

/* ======================================================================== */
/*  CONFIGURACIÓN: editar antes de compilar                                  */
/* ======================================================================== */

/* --- La red WiFi, como en la Clase 6 ------------------------------------ */
/* Las claves quedan en un archivo local que no se sube a Git.
 * Para configurar otra copia, usar config_local.h.example como modelo. */
#include "config_local.h"

/* Mismo broker del ejemplo de clase, con TLS. La raíz también va en el panel.
 * Son datos públicos; las claves WiFi y de Google siguen en config_local.h. */
#ifndef MQTT_BROKER
#define MQTT_BROKER "mqtts://broker.hivemq.com:8883"
#endif
#ifndef MQTT_RAIZ
#define MQTT_RAIZ "ucu-camara-2026-sosadias"
#endif

/* Seguridad de la red: descomenta UNA sola línea. Es el MÍNIMO que se acepta. */
//#define WIFI_SEGURIDAD_WPA        /* WPA  (TKIP): routers muy antiguos   */
#define WIFI_SEGURIDAD_WPA2         /* WPA2 (AES):  la gran mayoría        */
//#define WIFI_SEGURIDAD_WPA3       /* WPA3 (SAE):  redes WPA3 o WPA2/WPA3 */

/* --- La nube: lo que dio el Apps Script al instalarlo -------------------- */
/* La URL de la implementación, la que termina en /exec. */

/* El token que imprimió configurar(). Es la llave de tu Drive: como la clave
 * del WiFi, no se publica ni se sube al repositorio. */

/* --- La placa principal y la pantalla, como en el ejemplo 1 -------------- */
#define KALUGA_V1_2                 /* "ESP32-S2-Kaluga-1 V1.2"            */
//#define KALUGA_V1_3

#define PANTALLA_CON_ST7789         /* LCD32 v1.1 (kit v1.2): la habitual  */
//#define PANTALLA_CON_ILI9341

/* --- La foto ------------------------------------------------------------- */
#define FOTO_TAMANO         FRAMESIZE_VGA   /* 640x480: unos 30-60 KB      */
#define FOTO_CALIDAD        12              /* 0-63: MENOS es MEJOR        */
#define CAMARA_VOLTEAR      true
#define CAMARA_ESPEJO       false

/* Además del botón BOOT, una foto automática cada tantos segundos.
 * 0: solo con el botón. */
#define FOTO_CADA_S         120

/* Evita fotos seguidas, aunque las pidan disparadores distintos. */
#define FOTO_TIEMPO_MUERTO_S 5

/* Para medir el visor sin detector, poner MOVIMIENTO_ACTIVO en 0.
 * La cámara debe quedar fija. Los umbrales se ajustan con pruebas reales. */
#define MOVIMIENTO_ACTIVO             1
#define MOVIMIENTO_DIFERENCIA_MINIMA  20  /* brillo de 0 a 255 */
#define MOVIMIENTO_PORCENTAJE_MINIMO   8  /* porcentaje de píxeles cambiados */
#define MOVIMIENTO_CUADROS_INICIALES   5  /* referencia tras arranque o foto */

#if MOVIMIENTO_DIFERENCIA_MINIMA < 1 || MOVIMIENTO_DIFERENCIA_MINIMA > 255 || \
    MOVIMIENTO_PORCENTAJE_MINIMO < 1 || MOVIMIENTO_PORCENTAJE_MINIMO > 100
#error "Revisar los umbrales de movimiento"
#endif

#if FOTO_CADA_S < 0 || FOTO_TIEMPO_MUERTO_S < 0
#error "Los tiempos de las fotos no pueden ser negativos"
#endif

/* ======================================================================== */

#if (defined(WIFI_SEGURIDAD_WPA) + defined(WIFI_SEGURIDAD_WPA2) + \
     defined(WIFI_SEGURIDAD_WPA3)) != 1
#error "Descomenta exactamente una: WIFI_SEGURIDAD_WPA, _WPA2 o _WPA3"
#endif
#if (defined(KALUGA_V1_2) + defined(KALUGA_V1_3)) != 1
#error "Descomenta exactamente una: KALUGA_V1_2 o KALUGA_V1_3"
#endif
#if (defined(PANTALLA_CON_ST7789) + defined(PANTALLA_CON_ILI9341)) != 1
#error "Descomenta exactamente una: PANTALLA_CON_ST7789 o PANTALLA_CON_ILI9341"
#endif

#if defined(WIFI_SEGURIDAD_WPA)
#define WIFI_SEGURIDAD      RED_WPA
#elif defined(WIFI_SEGURIDAD_WPA2)
#define WIFI_SEGURIDAD      RED_WPA2
#else
#define WIFI_SEGURIDAD      RED_WPA3
#endif
#ifdef KALUGA_V1_2
#define KIT_CAMARA          CAMARA_KALUGA_V1_2
#else
#define KIT_CAMARA          CAMARA_KALUGA_V1_3
#endif
#ifdef PANTALLA_CON_ST7789
#define CONTROLADOR         PANTALLA_ST7789
#else
#define CONTROLADOR         PANTALLA_ILI9341
#endif

#define BOTON_BOOT          GPIO_NUM_0
#define ANTIRREBOTE_MS      300

/* --- Prioridades ---------------------------------------------------------
 *
 * La pila de red de ESP-IDF va por encima de todo lo nuestro (wifi 23,
 * sys_evt 20, tcpip 18). El driver de la cámara también: cam_task 23.
 *
 *   5  subida   casi siempre bloqueada en la red; cuando le toca la CPU
 *               (base64, el saludo TLS) es poco rato y no debe esperar al visor
 *   3  conexion (red_wifi.c) solo decide cuándo reintentar el WiFi
 *   2  visor    decodificar JPEG es CPU pura: abajo del todo, para que use
 *               lo que sobra y no lo que necesitan los demás
 */
#define PRIO_SUBIDA         5
#define PRIO_VISOR          2

/* En bytes. La de subida es grande: el saludo TLS (mbedTLS) corre en la
 * tarea que llama a esp_http_client. Se ajustan con el informe. */
#define PILA_SUBIDA         8192
#define PILA_VISOR          4096

#define PAUSA_VISOR_MS      5
#define FOTO_EN_PANTALLA_MS 2000

/* --- La cola de fotos ------------------------------------------------------
 * Cada hueco es un puntero a una foto en PSRAM: 4 fotos VGA son unos 200 KB.
 * Con la cola llena, la foto NUEVA se descarta y se avisa en pantalla: quien
 * pulsó el botón lo ve, y las que ya estaban esperando no se pierden. */
#define COLA_FOTOS_LARGO    4

/* --- Reintentos ------------------------------------------------------------ */
#define ESPERA_MIN_MS       2000
#define ESPERA_MAX_MS       60000
#define INTENTOS_SERVIDOR   5       /* errores del servidor, por foto        */

#define INFORME_PERIODO_MS  30000

#define AVISO_BOTON         (1u << 0)
#define AVISO_PERIODICA     (1u << 1)
#define AVISO_MOVIMIENTO    (1u << 2)
#define AVISOS_FOTO         (AVISO_BOTON | AVISO_PERIODICA | AVISO_MOVIMIENTO)

#define NEGRO               0x0000
#define BLANCO              0xFFFF

static const char *TAG = "camara_nube";

/* ======================================================================== */
/*  Tipos                                                                    */
/* ======================================================================== */

/* Lo que viaja por la cola, POR COPIA: el puntero y los datos de la foto.
 * La memoria del JPEG es de quien tiene la foto en la mano: del visor hasta
 * que la encola, de "subida" desde que la saca. Quien la tiene, la libera. */
typedef struct {
    uint8_t *jpeg;
    size_t   largo;
    int64_t  captura_us;
    uint32_t numero;
    uint16_t ancho;
    uint16_t alto;
    nube_metadatos_t metadatos;
} foto_t;

/* Lo que se cuenta y lo que enseña la barra de estado. Lo escriben el visor
 * y "subida"; se toca con el mutex. */
typedef struct {
    uint32_t tomadas;
    uint32_t cola_llena;        /* descartadas al tomarlas                  */
    uint32_t sin_memoria;
    uint32_t subidas;
    uint32_t repetidas;         /* el script ya la tenía: no se guardó dos veces */
    uint32_t rechazadas;        /* la nube dijo que no, o demasiados errores */
    uint32_t reintentos;
    uint64_t bytes;             /* enviados en las subidas buenas           */
    uint32_t subida_ms_max;     /* de empezar el POST a tener la respuesta  */
    uint64_t subida_ms_suma;
    uint32_t extremo_ms_max;    /* de la captura a tenerla en la nube       */
    bool     en_mano;           /* "subida" tiene una foto (subiendo o esperando
                                 * para reintentar)                          */
    bool     subiendo;          /* dentro de nube_subir() ahora mismo        */
    char     ultimo[24];        /* para la pantalla: "OK 2.7 S", "ERROR RED" */
} estado_t;

/* Tiempos de pared: incluyen esperas y las interrupciones de otras tareas.
 * No son tiempo de CPU; ese dato lo da informe_tareas(). */
enum {
    VISOR_ADQUIRIR, VISOR_JPEG_1_8, VISOR_COMPARAR, VISOR_JPEG_PANTALLA,
    VISOR_BARRA, VISOR_PANTALLA, VISOR_PAUSA, VISOR_ETAPAS
};
typedef struct {
    uint32_t ciclos;
    uint32_t mostrados;
    uint32_t sin_cuadro;
    uint32_t errores_jpeg;
    uint32_t errores_detector;
    uint32_t fotos;
    uint64_t foto_us;
    uint64_t etapas_us[VISOR_ETAPAS];
} medida_visor_t;

/* ======================================================================== */
/*  Objetos del kernel y estado                                              */
/* ======================================================================== */

static EventGroupHandle_t s_eventos;        /* EV_WIFI_OK (de red_wifi.c)   */
static QueueHandle_t      s_cola_fotos;     /* visor -> subida              */
static SemaphoreHandle_t  s_mutex_estado;
static SemaphoreHandle_t  s_mutex_mediciones;
static TaskHandle_t       s_tarea_visor;
static TimerHandle_t      s_timer_informe;
static TimerHandle_t      s_timer_foto;
static temperature_sensor_handle_t s_temperatura;
static movimiento_t s_movimiento;

static estado_t s_estado = { .ultimo = "LISTA" };
static medida_visor_t s_medida_visor;
static int64_t s_inicio_medida_visor_us;
static bool     s_nube_lista;

static uint8_t *s_jpeg;                     /* el cuadro del visor          */

static char s_id[24];                       /* "kaluga-0ab2c3", de la MAC   */
static char s_arranque[12];                 /* al azar en cada arranque     */

/* ======================================================================== */
/*  Utilidades                                                               */
/* ======================================================================== */

static inline uint32_t us_a_ms(int64_t us)
{
    return (uint32_t)(us / 1000);
}

static void anotar(const char *ultimo)
{
    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    strlcpy(s_estado.ultimo, ultimo, sizeof(s_estado.ultimo));
    xSemaphoreGive(s_mutex_estado);
}

static void barra_de_estado(imagen_t *img, const char *texto)
{
    const int escala = (img->ancho >= 240) ? 2 : 1;
    const int alto = 8 * escala + 4;
    const int y = img->alto - alto;

    pantalla_rect_en_imagen(img->pixeles, img->ancho, img->alto,
                            0, y, img->ancho, alto, NEGRO);
    pantalla_texto_en_imagen(img->pixeles, img->ancho, img->alto,
                             4, y + 2, texto, escala, BLANCO, NEGRO);
}

static void marco(imagen_t *img, int grosor)
{
    uint16_t *p = img->pixeles;
    const int w = img->ancho;
    const int h = img->alto;
    pantalla_rect_en_imagen(p, w, h, 0, 0, w, grosor, BLANCO);
    pantalla_rect_en_imagen(p, w, h, 0, h - grosor, w, grosor, BLANCO);
    pantalla_rect_en_imagen(p, w, h, 0, 0, grosor, h, BLANCO);
    pantalla_rect_en_imagen(p, w, h, w - grosor, 0, grosor, h, BLANCO);
}

/* La línea del visor: red, fotos pendientes y lo último que pasó. */
static void texto_de_estado(char *dst, size_t n)
{
    const bool red = (xEventGroupGetBits(s_eventos) & EV_WIFI_OK) != 0;

    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    const unsigned pendientes = (unsigned)uxQueueMessagesWaiting(s_cola_fotos)
                                + (s_estado.en_mano ? 1u : 0u);
    char ultimo[sizeof(s_estado.ultimo)];
    strlcpy(ultimo, s_estado.subiendo ? "SUBIENDO" : s_estado.ultimo, sizeof(ultimo));
    xSemaphoreGive(s_mutex_estado);

    snprintf(dst, n, "%s COLA %u %s", red ? "RED" : "SIN RED", pendientes, ultimo);
}

/* ======================================================================== */
/*  El botón BOOT y la foto periódica                                        */
/* ======================================================================== */

static void IRAM_ATTR al_pulsar_boot(void *arg)
{
    const uint32_t ahora_ms = (uint32_t)(esp_timer_get_time() / 1000);
    static uint32_t anterior_ms;

    if ((uint32_t)(ahora_ms - anterior_ms) < ANTIRREBOTE_MS) {
        return;
    }
    anterior_ms = ahora_ms;

    BaseType_t despertar = pdFALSE;
    xTaskNotifyFromISR(s_tarea_visor, AVISO_BOTON, eSetBits, &despertar);
    portYIELD_FROM_ISR(despertar);
}

/* Corre en Tmr Svc: solo avisa, no captura ni espera a la red. */
static void foto_periodica(TimerHandle_t timer)
{
    xTaskNotify(s_tarea_visor, AVISO_PERIODICA, eSetBits);
}

static void configurar_boton(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOTON_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    /* El servicio ya lo instaló el driver de la cámara: ESP_ERR_INVALID_STATE
     * es lo esperado. */
    const esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(BOTON_BOOT, al_pulsar_boot, NULL));
}

/* ======================================================================== */
/*  La foto: del driver a la cola                                            */
/* ======================================================================== */

static void leer_mediciones(nube_metadatos_t *datos)
{
    datos->rssi_valido = false;
    datos->temperatura_valida = false;
    /* El visor y telemetría comparten el sensor de temperatura. */
    xSemaphoreTake(s_mutex_mediciones, portMAX_DELAY);
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        datos->rssi_dbm = ap.rssi;
        datos->rssi_valido = true;
    }
    if (s_temperatura != NULL &&
            temperature_sensor_get_celsius(s_temperatura, &datos->temperatura_c) == ESP_OK &&
            isfinite(datos->temperatura_c)) {
        datos->temperatura_valida = true;
    }
    xSemaphoreGive(s_mutex_mediciones);
}

static void tomar_foto(uint32_t pulsacion_ms, const char *motivo)
{

    /* El primer cuadro que empezó DESPUÉS de pedir la foto (ejemplo 1). */
    camera_fb_t *fb = esp_camera_fb_get();
    for (int intento = 0; fb != NULL && intento < 4; intento++) {
        if ((int32_t)(us_a_ms(camara_marca_us(fb)) - pulsacion_ms) >= 0) {
            break;
        }
        esp_camera_fb_return(fb);
        fb = esp_camera_fb_get();
    }
    if (fb == NULL) {
        ESP_LOGW(TAG, "[foto] la camara no entrego ningun cuadro");
        return;
    }

    /* La copia, del tamaño exacto de ESTE JPEG: cada foto ocupa lo que
     * ocupa. El buffer del driver vuelve enseguida. */
    foto_t f = {
        .jpeg = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM),
        .largo = fb->len,
        .captura_us = camara_marca_us(fb),
        .ancho = (uint16_t)fb->width,
        .alto = (uint16_t)fb->height,
        .metadatos = { .motivo = motivo },
    };
    if (f.jpeg != NULL) {
        memcpy(f.jpeg, fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);

    /* Leemos cerca de la captura y guardamos los valores con la foto.
     * No se vuelven a leer al subir: podría haber esperado sin WiFi. */
    leer_mediciones(&f.metadatos);
    char rssi[16] = "sin dato";
    char temperatura[16] = "sin dato";
    if (f.metadatos.rssi_valido) {
        snprintf(rssi, sizeof(rssi), "%d", f.metadatos.rssi_dbm);
    }
    if (f.metadatos.temperatura_valida) {
        snprintf(temperatura, sizeof(temperatura), "%.1f", f.metadatos.temperatura_c);
    }
    ESP_LOGI(TAG, "[metadatos] %s; RSSI %s dBm; chip %s C", motivo, rssi, temperatura);

    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    f.numero = ++s_estado.tomadas;
    if (f.jpeg == NULL) {
        s_estado.sin_memoria++;
    }
    xSemaphoreGive(s_mutex_estado);

    if (f.jpeg == NULL) {
        ESP_LOGW(TAG, "[foto %" PRIu32 "] sin PSRAM para guardarla (%u B)",
                 f.numero, (unsigned)f.largo);
        anotar("SIN MEMORIA");
        return;
    }

    /* Se decodifica ANTES de encolarla: una vez en la cola, la foto es de
     * "subida", que la puede liberar en cualquier momento. */
    imagen_t img;
    const esp_err_t err_img = imagen_decodificar(f.jpeg, f.largo, &img);

    const char *destino;
    if (!s_nube_lista) {
        destino = "SIN NUBE";
        free(f.jpeg);
    } else if (xQueueSend(s_cola_fotos, &f, 0) == pdTRUE) {
        destino = "EN COLA";
    } else {
        destino = "COLA LLENA";
        free(f.jpeg);
        xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
        s_estado.cola_llena++;
        xSemaphoreGive(s_mutex_estado);
    }
    /* A partir de aquí f.jpeg ya no es nuestro: no se toca. */

    ESP_LOGI(TAG, "[foto %" PRIu32 "] %u B, %ux%u: %s", f.numero,
             (unsigned)f.largo, f.ancho, f.alto, destino);

    if (err_img == ESP_OK) {
        char texto[40];
        snprintf(texto, sizeof(texto), "FOTO %" PRIu32 " %s", f.numero, destino);
        marco(&img, 4);
        barra_de_estado(&img, texto);
        imagen_mostrar(&img);
    }
}

/* ======================================================================== */
/*  TAREA visor                                                              */
/* ======================================================================== */

static void registrar_ciclo_visor(const medida_visor_t *medida)
{
    /* Un único registro por ciclo, sin imprimir una línea por cuadro. */
    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    s_medida_visor.ciclos += medida->ciclos;
    s_medida_visor.mostrados += medida->mostrados;
    s_medida_visor.sin_cuadro += medida->sin_cuadro;
    s_medida_visor.errores_jpeg += medida->errores_jpeg;
    s_medida_visor.errores_detector += medida->errores_detector;
    for (int i = 0; i < VISOR_ETAPAS; i++) {
        s_medida_visor.etapas_us[i] += medida->etapas_us[i];
    }
    xSemaphoreGive(s_mutex_estado);
}

static void tarea_visor(void *arg)
{
    uint32_t pendientes = 0;
    int64_t ultima_solicitud_us = 0;
    bool hubo_solicitud = false;
    unsigned cuadros_iniciales = MOVIMIENTO_CUADROS_INICIALES;
    bool error_movimiento_avisado = false;

    while (1) {
        /* Como en el ejemplo 1: «avisos» solo vale si se devolvió pdTRUE. */
        uint32_t avisos = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &avisos, 0) == pdTRUE) {
            pendientes |= avisos & AVISOS_FOTO;
        }
        if (pendientes != 0) {
            /* Si hay varios avisos: BOOT, periódica y luego movimiento.
             * Solo el visor decide: no hay dos tareas tomando fotos a la vez. */
            const char *motivo = (pendientes & AVISO_BOTON) ? "boton" :
                (pendientes & AVISO_PERIODICA) ? "periodica" : "movimiento";
            const int64_t ahora_us = esp_timer_get_time();
            pendientes = 0;
            if (hubo_solicitud && ahora_us - ultima_solicitud_us <
                    (int64_t)FOTO_TIEMPO_MUERTO_S * 1000000) {
                ESP_LOGI(TAG, "[disparo] %s descartado: tiempo muerto", motivo);
            } else {
                /* El plazo empieza al aceptar el pedido, incluso si falla
                 * la captura. Los pedidos descartados no quedan para después. */
                hubo_solicitud = true;
                ultima_solicitud_us = ahora_us;
                ESP_LOGI(TAG, "[disparo] %s aceptado a %" PRIi64 " ms",
                         motivo, ahora_us / 1000);
                const int64_t inicio_foto_us = esp_timer_get_time();
                tomar_foto(us_a_ms(ahora_us), motivo);
                /* Durante la foto fija no se comparan cuadros. Al volver,
                 * hacemos otra referencia para no comparar con una escena vieja. */
                s_movimiento.tiene_anterior = false;
                cuadros_iniciales = MOVIMIENTO_CUADROS_INICIALES;
                if (xTaskNotifyWait(0, UINT32_MAX, &avisos,
                        pdMS_TO_TICKS(FOTO_EN_PANTALLA_MS)) == pdTRUE) {
                    pendientes = avisos & AVISOS_FOTO;
                }
                const int64_t foto_us = esp_timer_get_time() - inicio_foto_us;
                xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
                s_medida_visor.fotos++;
                s_medida_visor.foto_us += foto_us;
                xSemaphoreGive(s_mutex_estado);
                continue;
            }
        }

        medida_visor_t medida = { .ciclos = 1 };
        int64_t inicio_etapa_us = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            medida.etapas_us[VISOR_ADQUIRIR] = esp_timer_get_time() - inicio_etapa_us;
            medida.sin_cuadro = 1;
            inicio_etapa_us = esp_timer_get_time();
            vTaskDelay(pdMS_TO_TICKS(100));
            medida.etapas_us[VISOR_PAUSA] = esp_timer_get_time() - inicio_etapa_us;
            registrar_ciclo_visor(&medida);
            continue;
        }
        const size_t largo = fb->len;
        memcpy(s_jpeg, fb->buf, largo);
        esp_camera_fb_return(fb);
        medida.etapas_us[VISOR_ADQUIRIR] = esp_timer_get_time() - inicio_etapa_us;

        imagen_t img;
        if (MOVIMIENTO_ACTIVO) {
            inicio_etapa_us = esp_timer_get_time();
            const esp_err_t err = imagen_decodificar_movimiento(s_jpeg, largo, &img);
            medida.etapas_us[VISOR_JPEG_1_8] = esp_timer_get_time() - inicio_etapa_us;
            if (err == ESP_OK && img.ancho == MOVIMIENTO_ANCHO &&
                    img.alto == MOVIMIENTO_ALTO) {
                movimiento_resultado_t resultado;
                inicio_etapa_us = esp_timer_get_time();
                const bool cambio = movimiento_comparar(&s_movimiento, img.pixeles,
                    MOVIMIENTO_DIFERENCIA_MINIMA, MOVIMIENTO_PORCENTAJE_MINIMO,
                    &resultado);
                medida.etapas_us[VISOR_COMPARAR] = esp_timer_get_time() - inicio_etapa_us;
                if (cuadros_iniciales > 0) {
                    cuadros_iniciales--;
                } else if (cambio && (!hubo_solicitud ||
                        esp_timer_get_time() - ultima_solicitud_us >=
                            (int64_t)FOTO_TIEMPO_MUERTO_S * 1000000)) {
                    ESP_LOGI(TAG, "[movimiento] %u/%u pixeles; diferencia media %u.%02u",
                             resultado.cambiados, MOVIMIENTO_PIXELES,
                             resultado.diferencia_media_x100 / 100,
                             resultado.diferencia_media_x100 % 100);
                    pendientes |= AVISO_MOVIMIENTO;
                }
                error_movimiento_avisado = false;
            } else {
                medida.errores_detector = 1;
                s_movimiento.tiene_anterior = false;
                cuadros_iniciales = MOVIMIENTO_CUADROS_INICIALES;
                if (!error_movimiento_avisado) {
                    ESP_LOGW(TAG, "no se pudo obtener el cuadro de movimiento 80x60");
                    error_movimiento_avisado = true;
                }
            }
        }
        inicio_etapa_us = esp_timer_get_time();
        const esp_err_t err_img = imagen_decodificar(s_jpeg, largo, &img);
        medida.etapas_us[VISOR_JPEG_PANTALLA] = esp_timer_get_time() - inicio_etapa_us;
        if (err_img == ESP_OK) {
            inicio_etapa_us = esp_timer_get_time();
            char texto[40];
            texto_de_estado(texto, sizeof(texto));
            barra_de_estado(&img, texto);
            medida.etapas_us[VISOR_BARRA] = esp_timer_get_time() - inicio_etapa_us;
            inicio_etapa_us = esp_timer_get_time();
            imagen_mostrar(&img);
            medida.etapas_us[VISOR_PANTALLA] = esp_timer_get_time() - inicio_etapa_us;
            medida.mostrados = 1;
        } else {
            medida.errores_jpeg = 1;
        }
        inicio_etapa_us = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(PAUSA_VISOR_MS));
        medida.etapas_us[VISOR_PAUSA] = esp_timer_get_time() - inicio_etapa_us;
        registrar_ciclo_visor(&medida);
    }
}

/* ======================================================================== */
/*  TAREA subida:  de la cola a la nube                                      */
/* ======================================================================== */

/* El texto corto de la pantalla para cada resultado. */
static void resumen(nube_resultado_t r, const nube_informe_t *inf, char *dst, size_t n)
{
    if (r == NUBE_OK) {
        const uint32_t ms = inf->ms_envio + inf->ms_respuesta;
        snprintf(dst, n, "OK %" PRIu32 ".%" PRIu32 " S", ms / 1000, (ms % 1000) / 100);
    } else if (r == NUBE_ERROR_RED) {
        strlcpy(dst, "ERROR RED", n);
    } else if (r == NUBE_ERROR_SERVIDOR) {
        strlcpy(dst, "ERROR NUBE", n);
    } else if (strstr(inf->error, "token") != NULL) {
        strlcpy(dst, "TOKEN MAL", n);
    } else if (strstr(inf->error, "404") != NULL) {
        strlcpy(dst, "URL MAL", n);
    } else if (strstr(inf->error, "sesion") != NULL) {
        strlcpy(dst, "ACCESO MAL", n);
    } else {
        strlcpy(dst, "RECHAZADA", n);
    }
}

static void tarea_subida(void *arg)
{
    foto_t f;

    while (xQueueReceive(s_cola_fotos, &f, portMAX_DELAY) == pdTRUE) {
        uint32_t espera_ms = ESPERA_MIN_MS;
        int errores_servidor = 0;
        bool terminada = false;

        xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
        s_estado.en_mano = true;
        xSemaphoreGive(s_mutex_estado);

        while (!terminada) {
            /* Sin red no se intenta: se espera el bit, sin gastar CPU. */
            xEventGroupWaitBits(s_eventos, EV_WIFI_OK, pdFALSE, pdTRUE, portMAX_DELAY);

            xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
            s_estado.subiendo = true;
            xSemaphoreGive(s_mutex_estado);

            const nube_foto_t nf = {
                .jpeg = f.jpeg,
                .largo = f.largo,
                .ancho = f.ancho,
                .alto = f.alto,
                .numero = f.numero,
                .metadatos = f.metadatos,
                /* La «edad» de la Clase 6: la nube resta este número a su
                 * propia hora para saber cuándo se sacó la foto. */
                .edad_ms = us_a_ms(esp_timer_get_time() - f.captura_us),
            };
            nube_informe_t inf;
            const nube_resultado_t r = nube_subir(&nf, &inf);

            char corto[24];
            resumen(r, &inf, corto, sizeof(corto));
            const uint32_t subida_ms = inf.ms_envio + inf.ms_respuesta;

            xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
            s_estado.subiendo = false;
            strlcpy(s_estado.ultimo, corto, sizeof(s_estado.ultimo));
            if (r == NUBE_OK) {
                s_estado.subidas++;
                s_estado.repetidas += inf.repetida ? 1 : 0;
                s_estado.bytes += inf.bytes;
                s_estado.subida_ms_suma += subida_ms;
                if (subida_ms > s_estado.subida_ms_max) {
                    s_estado.subida_ms_max = subida_ms;
                }
                const uint32_t extremo = us_a_ms(esp_timer_get_time() - f.captura_us);
                if (extremo > s_estado.extremo_ms_max) {
                    s_estado.extremo_ms_max = extremo;
                }
            }
            xSemaphoreGive(s_mutex_estado);

            if (r == NUBE_OK) {
                ESP_LOGI(TAG, "[subida] foto %" PRIu32 " en Drive: %s (%" PRIu32
                              " ms + %" PRIu32 " ms, %u B enviados)%s",
                         f.numero, inf.nombre, inf.ms_envio, inf.ms_respuesta,
                         (unsigned)inf.bytes,
                         inf.repetida ? "; ya estaba de un intento anterior" : "");
                avisos_mqtt_foto(&nf, &inf, f.captura_us);
                terminada = true;
            } else if (r == NUBE_RECHAZADA) {
                ESP_LOGE(TAG, "[subida] foto %" PRIu32 " rechazada, se descarta: %s",
                         f.numero, inf.error);
                terminada = true;
            } else if (r == NUBE_ERROR_SERVIDOR && ++errores_servidor >= INTENTOS_SERVIDOR) {
                ESP_LOGE(TAG, "[subida] foto %" PRIu32 ": %d errores del servidor "
                              "seguidos, se descarta: %s", f.numero, errores_servidor,
                         inf.error);
                terminada = true;
            } else {
                /* Reintento con espera creciente y azar, como el WiFi de la
                 * Clase 6: veinte placas no vuelven a llamar a la vez. */
                const uint32_t azar = esp_random() % (espera_ms / 4 + 1);
                ESP_LOGW(TAG, "[subida] foto %" PRIu32 ": %s (%s); otra vez en %"
                              PRIu32 " ms", f.numero, nube_nombre_resultado(r),
                         inf.error, espera_ms + azar);
                xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
                s_estado.reintentos++;
                xSemaphoreGive(s_mutex_estado);
                vTaskDelay(pdMS_TO_TICKS(espera_ms + azar));
                espera_ms = (espera_ms >= ESPERA_MAX_MS / 2) ? ESPERA_MAX_MS
                                                             : espera_ms * 2;
            }

            if (terminada && r != NUBE_OK) {
                xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
                s_estado.rechazadas++;
                xSemaphoreGive(s_mutex_estado);
            }
        }

        free(f.jpeg);                       /* la foto era nuestra */
        xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
        s_estado.en_mano = false;
        xSemaphoreGive(s_mutex_estado);
    }
}

/* ======================================================================== */
/*  TEMPORIZADOR SOFTWARE:  informe                                          */
/* ======================================================================== */

static void informe_visor(void)
{
    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    const int64_t ahora_us = esp_timer_get_time();
    const int64_t intervalo_us = ahora_us - s_inicio_medida_visor_us;
    const medida_visor_t medida = s_medida_visor;
    s_medida_visor = (medida_visor_t){0};
    s_inicio_medida_visor_us = ahora_us;
    xSemaphoreGive(s_mutex_estado);
    if (intervalo_us <= 0) return;
    ESP_LOGI(TAG, "[visor] detector %s; ventana %.3f s; cuadros %" PRIu32 "; FPS %.2f",
             MOVIMIENTO_ACTIVO ? "SI" : "NO", intervalo_us / 1000000.0,
             medida.mostrados, medida.mostrados * 1000000.0 / intervalo_us);
    ESP_LOGI(TAG, "[visor] ciclos %" PRIu32 "; sin cuadro %" PRIu32
                  "; errores JPEG %" PRIu32 "; errores detector %" PRIu32,
             medida.ciclos, medida.sin_cuadro, medida.errores_jpeg, medida.errores_detector);
    if (medida.ciclos > 0) {
        const char *nombres[VISOR_ETAPAS] = {
            "adquirir/copiar", "JPEG 1/8", "comparar", "JPEG pantalla",
            "barra", "pantalla", "pausa"
        };
        for (int i = 0; i < VISOR_ETAPAS; i++) {
            ESP_LOGI(TAG, "[visor] %-15s media %.3f ms/ciclo", nombres[i],
                     medida.etapas_us[i] / (1000.0 * medida.ciclos));
        }
    }
    ESP_LOGI(TAG, "[visor] pedidos de foto atendidos %" PRIu32
                  "; captura y espera de foto fija %.3f s (incluidos en ventana FPS)",
             medida.fotos, medida.foto_us / 1000000.0);
}

static void informe(TimerHandle_t timer)
{
    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    const estado_t e = s_estado;
    xSemaphoreGive(s_mutex_estado);

    red_estado_t w;
    red_wifi_estado(&w);

    ESP_LOGI(TAG, "--- Informe de %s (cada %d s) -------------------------",
             s_id, INFORME_PERIODO_MS / 1000);
    informe_visor();
    informe_tareas(TAG);
    informe_memoria(TAG);
    avisos_mqtt_informe();
    ESP_LOGI(TAG, "  WiFi %s;  conexiones %" PRIu32 ", desconexiones %" PRIu32,
             w.conectada ? "SI" : "no", w.conexiones, w.desconexiones);
    ESP_LOGI(TAG, "  fotos: tomadas %" PRIu32 ", subidas %" PRIu32 " (repetidas %"
                  PRIu32 "), en cola %u/%d, cola llena %" PRIu32 ", sin memoria %"
                  PRIu32 ", rechazadas %" PRIu32 ", reintentos %" PRIu32,
             e.tomadas, e.subidas, e.repetidas,
             (unsigned)uxQueueMessagesWaiting(s_cola_fotos), COLA_FOTOS_LARGO,
             e.cola_llena, e.sin_memoria, e.rechazadas, e.reintentos);
    if (e.subidas > 0) {
        ESP_LOGI(TAG, "  subida: media %" PRIu32 " ms, maxima %" PRIu32 " ms;  "
                      "de la captura a la nube, maximo %" PRIu32 " ms;  %" PRIu32
                      " KB enviados",
                 (uint32_t)(e.subida_ms_suma / e.subidas), e.subida_ms_max,
                 e.extremo_ms_max, (uint32_t)(e.bytes / 1024));
    }
}

/* ======================================================================== */
/*  Arranque                                                                 */
/* ======================================================================== */

static void identidad(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_id, sizeof(s_id), "kaluga-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_arranque, sizeof(s_arranque), "%08" PRIx32, esp_random());
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== camara_nube: fotos de la Kaluga-1 a Google Drive ===");

    if (!pantalla_iniciar(CONTROLADOR)) {
        ESP_LOGW(TAG, "sin pantalla: el ejemplo sigue, pero solo por consola");
    }
    pantalla_limpiar(NEGRO);
    pantalla_texto(16, 16, "CAMARA NUBE", 3, BLANCO, NEGRO);
    pantalla_texto(16, 56, "INICIANDO...", 2, BLANCO, NEGRO);

    const camara_config_t cam = {
        .kit = KIT_CAMARA,
        .tamano = FOTO_TAMANO,
        .calidad = FOTO_CALIDAD,
        .voltear = CAMARA_VOLTEAR,
        .espejo = CAMARA_ESPEJO,
        .patron = false,
    };
    if (camara_iniciar(&cam) != ESP_OK) {
        pantalla_texto(16, 96, "SIN CAMARA", 2, pantalla_color(255, 80, 80), NEGRO);
        pantalla_texto(16, 124, "VER LA CONSOLA", 2, BLANCO, NEGRO);
        return;
    }
    s_jpeg = heap_caps_malloc(camara_tamano_buffer(), MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(s_jpeg != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(imagen_iniciar());
    if (MOVIMIENTO_ACTIVO) {
        if (FOTO_TAMANO != FRAMESIZE_VGA) {
            ESP_LOGE(TAG, "el detector 80x60 requiere FOTO_TAMANO FRAMESIZE_VGA");
            ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
        }
        s_movimiento.anterior = heap_caps_malloc(MOVIMIENTO_PIXELES,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_ERROR_CHECK(s_movimiento.anterior != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    }

    /* Es la temperatura del chip, no la del ambiente. Si falla el sensor,
     * seguimos tomando fotos y enviamos null en lugar de inventar un cero. */
    const temperature_sensor_config_t temp_cfg =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    esp_err_t temp_err = temperature_sensor_install(&temp_cfg, &s_temperatura);
    if (temp_err == ESP_OK) {
        temp_err = temperature_sensor_enable(s_temperatura);
        if (temp_err != ESP_OK) {
            temperature_sensor_uninstall(s_temperatura);
            s_temperatura = NULL;
        }
    }
    if (temp_err != ESP_OK) {
        ESP_LOGW(TAG, "temperatura no disponible: %s", esp_err_to_name(temp_err));
    }

    /* --- Objetos del kernel, ANTES de las tareas que los usan ------------ */
    s_eventos      = xEventGroupCreate();
    s_cola_fotos   = xQueueCreate(COLA_FOTOS_LARGO, sizeof(foto_t));
    s_mutex_estado = xSemaphoreCreateMutex();
    s_mutex_mediciones = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK((s_eventos && s_cola_fotos && s_mutex_estado && s_mutex_mediciones) ? ESP_OK
                                                                  : ESP_ERR_NO_MEM);

    /* --- Red, identidad y nube --------------------------------------------- */
    const red_config_t red = {
        .ssid = WIFI_SSID,
        .clave = WIFI_CLAVE,
        .seguridad = WIFI_SEGURIDAD,
    };
    ESP_ERROR_CHECK(red_wifi_iniciar(&red, s_eventos));
    identidad();
    ESP_LOGI(TAG, "ID de esta placa: %s (arranque %s)", s_id, s_arranque);

    const nube_config_t nube = {
        .url = NUBE_URL,
        .token = NUBE_TOKEN,
        .dispositivo = s_id,
        .arranque = s_arranque,
    };
    if (strstr(NUBE_URL, "PEGA-AQUI") != NULL || strstr(NUBE_TOKEN, "pega-aqui") != NULL) {
        /* Con los valores de ejemplo la cámara funciona igual; las fotos se
         * ven en pantalla pero no se suben. */
        ESP_LOGW(TAG, "falta configurar NUBE_URL y NUBE_TOKEN: instala el Apps "
                      "Script (ver ../apps_script/README.md); las fotos no se suben");
        s_nube_lista = false;
    } else {
        s_nube_lista = (nube_iniciar(&nube) == ESP_OK);
    }
    if (!s_nube_lista) {
        anotar("SIN NUBE");                 /* el log dice qué falta */
    }

    const avisos_mqtt_config_t mqtt = {
        .broker = MQTT_BROKER, .raiz = MQTT_RAIZ,
        .dispositivo = s_id, .arranque = s_arranque,
        .eventos_wifi = s_eventos, .leer_mediciones = leer_mediciones,
    };
    ESP_ERROR_CHECK(avisos_mqtt_iniciar(&mqtt));

    /* --- Las tareas -------------------------------------------------------- */
    s_inicio_medida_visor_us = esp_timer_get_time();
    xTaskCreate(tarea_visor, "visor", PILA_VISOR, NULL, PRIO_VISOR, &s_tarea_visor);
    ESP_ERROR_CHECK(s_tarea_visor != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    if (s_nube_lista) {
        ESP_ERROR_CHECK(xTaskCreate(tarea_subida, "subida", PILA_SUBIDA, NULL,
                                    PRIO_SUBIDA, NULL) == pdPASS ? ESP_OK
                                                                 : ESP_ERR_NO_MEM);
    }
    configurar_boton();

    s_timer_informe = xTimerCreate("informe", pdMS_TO_TICKS(INFORME_PERIODO_MS),
                                   pdTRUE, NULL, informe);
    ESP_ERROR_CHECK(s_timer_informe != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTimerStart(s_timer_informe, pdMS_TO_TICKS(100)) == pdPASS
                        ? ESP_OK : ESP_FAIL);
#if FOTO_CADA_S > 0
    s_timer_foto = xTimerCreate("foto", pdMS_TO_TICKS(FOTO_CADA_S * 1000),
                                pdTRUE, NULL, foto_periodica);
    ESP_ERROR_CHECK(s_timer_foto != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTimerStart(s_timer_foto, pdMS_TO_TICKS(100)) == pdPASS
                        ? ESP_OK : ESP_FAIL);
#else
    (void)s_timer_foto;
    (void)foto_periodica;
#endif

    /* --- A conectar --------------------------------------------------------
     * La subida espera el bit EV_WIFI_OK: mientras no haya red, las fotos
     * esperan en la cola. */
    ESP_ERROR_CHECK(red_wifi_conectar());
    ESP_LOGI(TAG, "Listo: pulsa BOOT para sacar una foto. app_main termina.");
}
