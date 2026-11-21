/*
 * nube.h — Clase 7 (Sistemas Embebidos, UCU 2026)
 *
 * Sube una foto a Google Drive a través de un Apps Script, por HTTPS.
 *
 *   placa ──POST https://script.google.com/macros/s/<ID>/exec──▶ Apps Script
 *          {"token":"...","dispositivo":"kaluga-0ab2c3",          │ comprueba
 *           "arranque":"5f3a9c01","numero":3,"edad_ms":850,       │ el token
 *           "ancho":640,"alto":480,"jpeg":"/9j/4AAQSkZJRgAB..."}   ▼
 *                                                       Drive: el archivo
 *        ◀──302 Location: https://script.googleusercontent.com/...  Hoja: una fila
 *   placa ──GET esa dirección──▶
 *        ◀──200 {"ok":true,"nombre":"kaluga-0ab2c3_2026-10-24_09-15-32_3.jpg",...}
 *
 * El 302 no es un error: Google sirve SIEMPRE la respuesta del script desde
 * otra dirección, de un solo uso. Cuando llega el 302, el script ya corrió.
 *
 * nube_subir() bloquea la tarea que la llama durante toda la subida (uno o
 * varios segundos) y no es reentrante: la llama UNA sola tarea.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *url;            /* https://script.google.com/macros/s/.../exec */
    const char *token;          /* el que generó configurar() en el script     */
    const char *dispositivo;    /* "kaluga-0ab2c3": va en el nombre del archivo */
    const char *arranque;       /* 8 hexadecimales al azar, nuevos en cada     */
                                /* arranque: con el número de foto, identifica */
                                /* una foto aunque se suba dos veces           */
} nube_config_t;

typedef struct {
    const char *motivo;         /* literal fijo: boton, periodica o movimiento */
    int         rssi_dbm;
    bool        rssi_valido;    /* sin WiFi no hay una lectura disponible     */
    float       temperatura_c;
    bool        temperatura_valida;
} nube_metadatos_t;

typedef struct {
    const uint8_t *jpeg;
    size_t         largo;
    uint16_t       ancho;
    uint16_t       alto;
    uint32_t       numero;      /* foto n.º desde el arranque                 */
    uint32_t       edad_ms;     /* cuánto hace que se capturó: la nube calcula */
                                /* la hora de captura con su propio reloj      */
    nube_metadatos_t metadatos;
    int64_t        captura_epoch_ms; /* hora SNTP; 0 si aún no se sincronizó */
} nube_foto_t;

typedef enum {
    NUBE_OK = 0,
    NUBE_ERROR_RED,             /* no conecta, se corta, se agota el plazo:    */
                                /* reintentar mientras haga falta              */
    NUBE_ERROR_SERVIDOR,        /* 5xx, 429, respuesta inesperada: reintentar  */
                                /* unas pocas veces                            */
    NUBE_RECHAZADA,             /* token, URL, acceso o foto inválida: insistir */
                                /* no sirve de nada                            */
} nube_resultado_t;

typedef struct {
    int      http;              /* último código HTTP; 0 si no hubo respuesta */
    uint32_t ms_envio;          /* conectar + mandar la foto + el 302          */
    uint32_t ms_respuesta;      /* el GET de la respuesta                      */
    size_t   bytes;             /* el cuerpo del POST: JSON con la foto        */
    bool     repetida;          /* la nube ya la tenía de un intento anterior: */
                                /* no la guardó otra vez                       */
    char     nombre[96];        /* NUBE_OK: el nombre del archivo en Drive     */
    char     id_drive[96];      /* referencia al archivo, no da permisos       */
    int64_t  fila_epoch_ms;     /* Google confirmó la escritura de la fila     */
    char     error[96];         /* lo demás: por qué no                        */
} nube_informe_t;

/* Comprueba la configuración (URL de un Apps Script, token con forma de
 * token) y la guarda. ESP_ERR_INVALID_ARG, con el motivo en el log, si algo
 * está claramente mal: mejor ahora que en la primera foto. */
esp_err_t nube_iniciar(const nube_config_t *cfg);

/* Sube una foto. Rellena el informe en todos los casos. */
nube_resultado_t nube_subir(const nube_foto_t *foto, nube_informe_t *inf);

/* "OK", "ERROR DE RED"...: para el registro. */
const char *nube_nombre_resultado(nube_resultado_t r);

/* Bytes del cuerpo del POST para una foto de `largo` bytes: el JPEG en
 * base64 (4 caracteres por cada 3 bytes) más el JSON que lo envuelve. */
size_t nube_bytes_a_enviar(const nube_foto_t *foto);
