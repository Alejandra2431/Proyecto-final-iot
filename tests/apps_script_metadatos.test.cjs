// Pruebas locales: Drive y Sheets se simulan, no se usa la cuenta de Google.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const encabezadoAnterior = ['Captura', 'Recibida', 'Dispositivo', 'Foto n.º',
  'Archivo', 'Bytes', 'Resolución', 'Espera en la placa (s)', 'Enlace'];
const filas = [encabezadoAnterior.slice(), ['fila anterior sin modificar']];
const filaAnterior = filas[1].slice();
const cache = new Map();
let archivos = 0;
const hoja = {
  getRange(fila, columna, alto, ancho) {
    assert.equal(alto, 1);
    return {
      getValues: () => [Array.from({ length: ancho }, (_, i) =>
        filas[fila - 1][columna - 1 + i] ?? '')],
      setValues(valores) {
        valores[0].forEach((valor, i) => { filas[fila - 1][columna - 1 + i] = valor; });
      },
    };
  },
  appendRow(fila) { filas.push(fila); },
  getLastRow() { return filas.length; },
};
const contexto = vm.createContext({
  console,
  PropertiesService: { getScriptProperties: () => ({
    getProperty: clave => ({ TOKEN: 'token-de-prueba', CARPETA_ID: 'carpeta', HOJA_ID: 'hoja' })[clave],
  }) },
  LockService: { getScriptLock: () => ({ tryLock: () => true, releaseLock() {} }) },
  CacheService: { getScriptCache: () => ({
    get: clave => cache.get(clave) ?? null,
    put: (clave, valor) => cache.set(clave, valor),
  }) },
  Session: { getScriptTimeZone: () => 'America/Montevideo' },
  Utilities: {
    base64Decode: texto => Array.from(Buffer.from(texto, 'base64'), b => b > 127 ? b - 256 : b),
    formatDate: () => '2026-11-21_12-00-00',
    newBlob: bytes => bytes,
  },
  DriveApp: { getFolderById: () => ({ createFile() {
    const id = ++archivos;
    return { setDescription() {}, getId: () => String(id), getUrl: () => 'foto/' + id };
  } }) },
  SpreadsheetApp: { openById: () => ({ getSheets: () => [hoja] }), flush() {} },
});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../apps_script/Codigo.gs'), 'utf8'), contexto);

let numero = 0;
function recibir(extra = {}) {
  const datos = { token: 'token-de-prueba', dispositivo: 'kaluga-prueba', arranque: 'abc123',
    numero: ++numero, edad_ms: 3000, ancho: 640, alto: 480,
    jpeg: vm.runInContext('JPEG_DE_PRUEBA', contexto), ...extra };
  const contents = JSON.stringify(datos);
  return contexto.recibir({ postData: { contents, length: contents.length } });
}

assert.equal(recibir({ motivo: 'boton', rssi_dbm: -62, temperatura_c: 43.2 }).ok, true);
assert.deepEqual(filas[0].slice(0, 9), encabezadoAnterior);
assert.deepEqual(filas[0].slice(9, 12), ['Motivo', 'RSSI (dBm)', 'Temperatura chip (°C)']);
assert.deepEqual(filas[1], filaAnterior);
assert.deepEqual(Array.from(filas.at(-1).slice(9, 12)), ['boton', -62, 43.2]);
assert.equal(filas.at(-1)[7], 3);
assert.equal(recibir({ numero: 1, motivo: 'boton', rssi_dbm: -62, temperatura_c: 43.2 }).repetida, true);
assert.equal(archivos, 1);
assert.equal(filas.length, 3);

assert.equal(recibir({ motivo: 'periodica', rssi_dbm: null, temperatura_c: null }).ok, true);
assert.deepEqual(Array.from(filas.at(-1).slice(9, 12)), ['periodica', '', '']);
assert.equal(recibir().ok, true); // Firmware de clase sin metadatos.
assert.deepEqual(Array.from(filas.at(-1).slice(9, 12)), ['', '', '']);
assert.equal(recibir({ motivo: 'movimiento', rssi_dbm: -127, temperatura_c: 0 }).ok, true);
assert.deepEqual(Array.from(filas.at(-1).slice(9, 12)), ['movimiento', -127, 0]);
const capturaEpoch = Date.now() - 1500;
assert.equal(recibir({ motivo: 'boton', captura_epoch_ms: capturaEpoch }).ok, true);
assert.equal(filas.at(-1)[13], capturaEpoch);
assert.ok(filas.at(-1)[14] >= capturaEpoch);
assert.equal(filas.at(-1)[15], filas.at(-1)[14] - capturaEpoch);
assert.equal(filas.at(-1)[16], 'SNTP');
assert.equal(recibir({ captura_epoch_ms: 'ayer' }).error, 'formato');

const antes = archivos;
for (const extra of [{ motivo: 'otro' }, { motivo: '' }, { rssi_dbm: '-60' },
  { rssi_dbm: 1 }, { rssi_dbm: -128 }, { temperatura_c: '40' },
  { temperatura_c: 126 }, { temperatura_c: -41 }]) {
  assert.equal(recibir(extra).error, 'formato');
}
assert.equal(archivos, antes);
filas[0][9] = 'Columna ajena';
assert.throws(() => recibir({ motivo: 'boton' }), /Encabezado inesperado/);
assert.equal(archivos, antes);
assert.equal(filas[0][9], 'Columna ajena');
console.log('OK: metadatos, valores ausentes, formato, migración sin pérdida e idempotencia.');
