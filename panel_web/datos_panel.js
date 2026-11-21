/* Validación compartida por el navegador y las pruebas locales. */
(function (destino) {
  'use strict';
  const ID = /^[A-Za-z0-9_-]{1,32}$/;
  const DRIVE = /^[A-Za-z0-9_-]{1,95}$/;
  const entero = (v, min, max) => Number.isSafeInteger(v) && v >= min && v <= max;
  const lectura = (v, min, max) => v === null || (typeof v === 'number' && Number.isFinite(v) && v >= min && v <= max);
  function validar(tema, texto, raiz, dispositivo) {
    if (typeof texto !== 'string' || texto.length > 1024 || !ID.test(dispositivo)) return null;
    const prefijo = raiz + '/' + dispositivo + '/';
    if (!tema.startsWith(prefijo)) return null;
    const tipo = tema.slice(prefijo.length);
    let d;
    try { d = JSON.parse(texto); } catch (_) { return null; }
    if (!d || typeof d !== 'object' || Array.isArray(d) || d.v !== 1 ||
        d.dispositivo !== dispositivo || typeof d.arranque !== 'string' ||
        !/^[A-Za-z0-9]{1,16}$/.test(d.arranque) ||
        !lectura(d.rssi_dbm, -127, 0) ||
        (d.rssi_dbm !== null && !Number.isInteger(d.rssi_dbm)) ||
        !lectura(d.temperatura_c, -40, 125)) return null;
    if (tipo === 'telemetria') {
      if (!entero(d.secuencia, 1, 0xffffffff) || !entero(d.uptime_ms, 0, Number.MAX_SAFE_INTEGER)) return null;
    } else if (tipo === 'foto') {
      if (!entero(d.numero, 1, 1e9) || !['boton', 'periodica', 'movimiento'].includes(d.motivo) ||
          typeof d.id_drive !== 'string' || !DRIVE.test(d.id_drive) ||
          !entero(d.ancho, 1, 10000) || !entero(d.alto, 1, 10000) ||
          !entero(d.bytes, 4, 2 * 1024 * 1024) ||
          !entero(d.edad_ms, 0, Number.MAX_SAFE_INTEGER)) return null;
      for (const campo of ['captura_epoch_ms', 'fila_epoch_ms']) {
        if (d[campo] !== undefined && !entero(d[campo], 0, 4102444800000)) return null;
      }
      for (const campo of ['subida_ms', 'captura_a_respuesta_ms', 'intentos']) {
        if (d[campo] !== undefined && !entero(d[campo], 0, 0xffffffff)) return null;
      }
    } else return null;
    return { tipo, datos: d };
  }
  function medirFoto(d, recibidoMs, desfaseMs) {
    if (!Number.isFinite(desfaseMs) || !entero(d.captura_epoch_ms, 1577836800000, 4102444800000) ||
        !entero(d.fila_epoch_ms, d.captura_epoch_ms, 4102444800000) ||
        !entero(d.subida_ms, 1, 0xffffffff) || !entero(d.captura_a_respuesta_ms, 1, 0xffffffff) ||
        !entero(d.intentos, 1, 0xffffffff)) return null;
    const demora = recibidoMs - desfaseMs - d.captura_epoch_ms;
    if (!Number.isFinite(demora) || demora < 0) return null;
    return {...d, recibido_ms: recibidoMs,
      captura_a_fila_ms: d.fila_epoch_ms - d.captura_epoch_ms,
      captura_a_panel_ms: Math.round(demora)};
  }
  function resumir(fotos) {
    const resultado = {cantidad: fotos.length};
    for (const campo of ['subida_ms', 'captura_a_fila_ms', 'captura_a_panel_ms', 'captura_a_respuesta_ms']) {
      const valores = fotos.map(f => f[campo]);
      resultado[campo] = valores.length ? {
        media: valores.reduce((a, b) => a + b, 0) / valores.length,
        maxima: Math.max(...valores),
      } : null;
    }
    return resultado;
  }
  const api = { validar, medirFoto, resumir };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else destino.DatosPanel = api;
})(globalThis);
