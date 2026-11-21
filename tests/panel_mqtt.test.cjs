// Pruebas sin broker: mensajes inválidos, duplicados y recorrido hasta el panel.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const DatosPanel = require('../panel_web/datos_panel.js');
const raiz = 'prueba-camara';
const dispositivo = 'kaluga-prueba';
const base = {v: 1, dispositivo, arranque: 'abc123', rssi_dbm: -55, temperatura_c: 30};
const telemetria = {...base, secuencia: 1, uptime_ms: 1000};
const foto = {...base, numero: 1, motivo: 'movimiento', id_drive: 'archivo_123',
  ancho: 640, alto: 480, bytes: 10224, edad_ms: 16000};
const tema = tipo => raiz + '/' + dispositivo + '/' + tipo;
const validar = (tipo, datos) => DatosPanel.validar(tema(tipo), JSON.stringify(datos), raiz, dispositivo);

test('valida datos reales y conserva ceros y lecturas ausentes', () => {
  assert.equal(validar('telemetria', telemetria).datos.rssi_dbm, -55);
  assert.equal(validar('foto', foto).datos.motivo, 'movimiento');
  assert.equal(validar('telemetria', {...telemetria, rssi_dbm: null, temperatura_c: 0}).datos.temperatura_c, 0);
  assert.equal(validar('foto', {...foto, rssi_dbm: null}).datos.rssi_dbm, null);
});
test('rechaza formatos, temas y referencias peligrosas', () => {
  for (const extra of [{v: 2}, {arranque: undefined}, {arranque: 123}, {rssi_dbm: '-55'},
    {rssi_dbm: -128}, {temperatura_c: 126}, {motivo: 'otro'}, {edad_ms: -1},
    {id_drive: 'https://otro.ejemplo'}, {id_drive: '"><script>'}]) {
    assert.equal(validar('foto', {...foto, ...extra}), null);
  }
  assert.equal(DatosPanel.validar(tema('foto'), '{', raiz, dispositivo), null);
  assert.equal(DatosPanel.validar(tema('foto'), 'x'.repeat(1025), raiz, dispositivo), null);
  assert.equal(DatosPanel.validar('otra/kaluga-prueba/foto', JSON.stringify(foto), raiz, dispositivo), null);
  assert.equal(validar('comando', foto), null);
});

test('separa subida, respuesta, fila y recepción corregida del panel', () => {
  const d = {...foto, captura_epoch_ms: 1790000000000, fila_epoch_ms: 1790000009000,
    subida_ms: 15000, captura_a_respuesta_ms: 15500, intentos: 1};
  const medida = DatosPanel.medirFoto(d, 1790000018000, 1000);
  assert.equal(medida.captura_a_fila_ms, 9000);
  assert.equal(medida.captura_a_panel_ms, 17000);
  assert.equal(DatosPanel.medirFoto({...d, captura_epoch_ms: 0}, 1790000018000, 1000), null);
  assert.equal(DatosPanel.medirFoto(d, 1790000018000, NaN), null);
  const resumen = DatosPanel.resumir([medida, {...medida, subida_ms: 17000}]);
  assert.equal(resumen.subida_ms.media, 16000);
  assert.equal(resumen.subida_ms.maxima, 17000);
});

class Nodo {
  constructor() { this.children = []; this.listeners = {}; this.value = ''; this.textContent = ''; }
  append(...nodos) { this.children.push(...nodos); nodos.forEach(n => { n.parent = this; }); }
  prepend(nodo) { this.children.unshift(nodo); nodo.parent = this; }
  replaceChildren(...nodos) { this.children = []; this.append(...nodos); }
  addEventListener(tipo, fn) { this.listeners[tipo] = fn; }
  setAttribute(clave, valor) { this[clave] = valor; }
  remove() { if (this.parent) this.parent.children = this.parent.children.filter(n => n !== this); }
  get lastElementChild() { return this.children.at(-1); }
}
test('conecta, grafica, deduplica fotos y genera solo enlaces a Drive', () => {
  const nodos = new Map();
  const obtener = id => { if (!nodos.has(id)) nodos.set(id, new Nodo()); return nodos.get(id); };
  obtener('raiz').value = raiz; obtener('dispositivo').value = dispositivo; obtener('broker').value = 'hivemq';
  const eventos = {};
  let temasSuscritos;
  const cliente = {on: (tipo, fn) => { eventos[tipo] = fn; },
    subscribe(temas, opciones, callback) { temasSuscritos = temas; callback(null, temas.map(topic => ({topic, qos: 1}))); },
    end() { eventos.close(); }};
  const timers = [];
  const contexto = vm.createContext({console, DatosPanel, URLSearchParams, Date, Blob, URL,
    location: {search: ''}, window: {addEventListener() {}},
    document: {getElementById: obtener, createElement: () => new Nodo(), createElementNS: () => new Nodo()},
    mqtt: {connect: () => cliente}, setInterval: fn => { timers.push(fn); return timers.length; },
    clearInterval() {}, setTimeout() {},
  });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../panel_web/panel.js'), 'utf8'), contexto);
  obtener('conexion').listeners.submit({preventDefault() {}});
  eventos.connect();
  assert.deepEqual(Array.from(temasSuscritos), [tema('telemetria'), tema('foto')]);
  assert.equal(obtener('estado').textContent, 'Conectado y suscrito');
  const enviar = (tipo, datos, retain = false) => eventos.message(tema(tipo), Buffer.from(JSON.stringify(datos)), {retain});
  enviar('telemetria', telemetria);
  enviar('telemetria', telemetria); // Repetido: no duplicar en la gráfica.
  enviar('telemetria', {...telemetria, secuencia: 2}, true); // Retenido: no es muestra viva.
  assert.equal(vm.runInContext('muestras.length', contexto), 1);
  assert.equal(obtener('rssi').textContent, '-55 dBm');
  assert.equal(obtener('temperatura').textContent, '30.0 °C');
  timers[0]();
  assert.ok(obtener('grafica-rssi').children.length > 0);
  enviar('foto', foto); enviar('foto', foto);
  assert.equal(obtener('fotos').children.length, 1);
  const tarjeta = obtener('fotos').children[0];
  assert.equal(tarjeta.children[2].href, 'https://drive.google.com/file/d/archivo_123/view');
  tarjeta.children[4].listeners.click();
  assert.equal(tarjeta.children.at(-1).src, 'https://drive.google.com/file/d/archivo_123/preview');
  obtener('desconectar').listeners.click();
  assert.equal(obtener('estado').textContent, 'Desconectado');
});
