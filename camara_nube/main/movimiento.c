#include "movimiento.h"

bool movimiento_comparar(movimiento_t *detector, const uint16_t *pixeles,
                         unsigned diferencia_minima, unsigned porcentaje_minimo,
                         movimiento_resultado_t *resultado)
{
    resultado->cambiados = 0;
    resultado->diferencia_media_x100 = 0;
    unsigned suma = 0;
    const uint8_t *bytes = (const uint8_t *)pixeles;
    for (unsigned i = 0; i < MOVIMIENTO_PIXELES; i++) {
        /* Los bytes están en orden de pantalla, no de entero de la CPU. */
        const uint16_t color = ((uint16_t)bytes[2 * i] << 8) | bytes[2 * i + 1];
        const unsigned r = ((color >> 11) & 31) * 255 / 31;
        const unsigned g = ((color >> 5) & 63) * 255 / 63;
        const unsigned b = (color & 31) * 255 / 31;
        /* Aproximación entera de brillo: el verde pesa más que el azul. */
        const unsigned brillo = (77 * r + 150 * g + 29 * b) >> 8;
        if (detector->tiene_anterior) {
            const int cambio = (int)brillo - detector->anterior[i];
            const unsigned diferencia = cambio < 0 ? -cambio : cambio;
            suma += diferencia;
            if (diferencia >= diferencia_minima) {
                resultado->cambiados++;
            }
        }
        detector->anterior[i] = (uint8_t)brillo;
    }
    const bool comparar = detector->tiene_anterior;
    detector->tiene_anterior = true;
    resultado->diferencia_media_x100 = suma * 100 / MOVIMIENTO_PIXELES;
    return comparar && resultado->cambiados * 100 >=
        porcentaje_minimo * MOVIMIENTO_PIXELES;
}
