#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MOVIMIENTO_ANCHO 80
#define MOVIMIENTO_ALTO  60
#define MOVIMIENTO_PIXELES (MOVIMIENTO_ANCHO * MOVIMIENTO_ALTO)

typedef struct {
    uint8_t *anterior;       /* 4800 bytes, reservados por el visor en PSRAM */
    bool tiene_anterior;
} movimiento_t;

typedef struct {
    unsigned cambiados;
    unsigned diferencia_media_x100;
} movimiento_resultado_t;

/* RGB565 con el byte alto primero, como lo entrega imagen.c al panel.
 * La primera llamada solo guarda la referencia y nunca dispara. */
bool movimiento_comparar(movimiento_t *detector, const uint16_t *pixeles,
                         unsigned diferencia_minima, unsigned porcentaje_minimo,
                         movimiento_resultado_t *resultado);
