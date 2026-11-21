/* Conexión y gráficas adaptadas del panel de Entrega6. */
'use strict';
const $ = id => document.getElementById(id);
const BROKERS = {
  hivemq: 'wss://broker.hivemq.com:8884/mqtt',
  emqx: 'wss://broker.emqx.io:8084/mqtt',
};
const VENTANA_MS = 10 * 60 * 1000;
const MAX_FOTOS = 60;
const muestras = [];
const fotos = new Map();
const registros = [];
let cliente = null;
let ultimaMuestra = 0;
let inicioSesion = new Date().toISOString();
let ultimaSecuencia = null;
let demoTimer = null;
const DEMO = new URLSearchParams(location.search).has('demo');

function anotar(texto) {
  registros.push(new Date().toISOString() + ' ' + texto);
  if (registros.length > 300) registros.shift();
  $('registro').textContent = registros.join('\n');
}
function elementoSVG(tipo, atributos) {
  const el = document.createElementNS('http://www.w3.org/2000/svg', tipo);
  for (const [k, v] of Object.entries(atributos)) el.setAttribute(k, v);
  return el;
}
function dibujar(id, campo, unidad) {
  const svg = $(id);
  svg.replaceChildren();
  const ahora = Date.now(), izq = 50, der = 448, arriba = 12, abajo = 155;
  const datos = muestras.filter(p => p.recibido_ms >= ahora - VENTANA_MS);
  const valores = datos.map(p => p[campo]).filter(v => v !== null);
  if (!valores.length) {
    const t = elementoSVG('text', {x: 230, y: 90, 'text-anchor': 'middle'});
    t.textContent = 'Esperando muestras'; svg.append(t); return;
  }
  let min = Math.min(...valores), max = Math.max(...valores);
  if (max - min < 2) { const centro = (max + min) / 2; min = centro - 1; max = centro + 1; }
  const margen = (max - min) * .1; min -= margen; max += margen;
  for (let i = 0; i <= 2; i++) {
    const y = arriba + (abajo - arriba) * i / 2;
    svg.append(elementoSVG('line', {x1: izq, x2: der, y1: y, y2: y, stroke: '#d5dde8'}));
    const t = elementoSVG('text', {x: izq - 7, y: y + 4, 'text-anchor': 'end'});
    t.textContent = (max - (max - min) * i / 2).toFixed(1); svg.append(t);
  }
  let segmento = [];
  const trazar = () => {
    if (segmento.length) svg.append(elementoSVG('polyline', {
      points: segmento.join(' '), fill: 'none', stroke: '#1a3c6e', 'stroke-width': 2,
    }));
    segmento = [];
  };
  let anterior = null;
  for (const p of datos) {
    if (p[campo] === null || (anterior && p.recibido_ms - anterior > 25000)) trazar();
    if (p[campo] !== null) {
      const x = izq + (p.recibido_ms - (ahora - VENTANA_MS)) / VENTANA_MS * (der - izq);
      const y = abajo - (p[campo] - min) / (max - min) * (abajo - arriba);
      segmento.push(x.toFixed(1) + ',' + y.toFixed(1));
      svg.append(elementoSVG('circle', {cx: x, cy: y, r: 2, fill: '#1a3c6e'}));
    }
    anterior = p.recibido_ms;
  }
  trazar();
  for (const [x, texto, lado] of [[izq, '−10 min', 'start'], [der, 'Ahora · ' + unidad, 'end']]) {
    const t = elementoSVG('text', {x, y: 180, 'text-anchor': lado});
    t.textContent = texto; svg.append(t);
  }
}
function mostrarFoto(d, recibido) {
  const tarjeta = document.createElement('article'); tarjeta.className = 'foto';
  const titulo = document.createElement('h3');
  titulo.textContent = 'Foto ' + d.numero + ' · ' + ({boton: 'Botón', periodica: 'Periódica', movimiento: 'Movimiento'})[d.motivo];
  const lista = document.createElement('dl');
  for (const [nombre, valor] of [
    ['Aviso recibido', new Date(recibido).toLocaleTimeString('es-UY')],
    ['Arranque', d.arranque], ['Resolución', d.ancho + ' × ' + d.alto],
    ['JPEG', d.bytes + ' B'], ['RSSI al capturar', d.rssi_dbm === null ? 'Sin dato' : d.rssi_dbm + ' dBm'],
    ['Chip al capturar', d.temperatura_c === null ? 'Sin dato' : d.temperatura_c + ' °C'],
    ['Edad al encolar MQTT', (d.edad_ms / 1000).toFixed(2) + ' s'],
  ]) {
    const dt = document.createElement('dt'); dt.textContent = nombre;
    const dd = document.createElement('dd'); dd.textContent = valor; lista.append(dt, dd);
  }
  const enlace = document.createElement('a');
  enlace.href = 'https://drive.google.com/file/d/' + d.id_drive + '/view';
  enlace.target = '_blank'; enlace.rel = 'noopener noreferrer'; enlace.textContent = 'Abrir en Drive';
  const boton = document.createElement('button'); boton.type = 'button'; boton.textContent = 'Mostrar foto';
  boton.addEventListener('click', () => {
    const marco = document.createElement('iframe'); marco.loading = 'lazy';
    marco.title = 'Foto ' + d.numero + ' en Google Drive';
    marco.referrerPolicy = 'no-referrer';
    marco.src = 'https://drive.google.com/file/d/' + d.id_drive + '/preview';
    tarjeta.append(marco); boton.remove();
  }, {once: true});
  tarjeta.append(titulo, lista, enlace, document.createElement('br'), boton);
  $('fotos').prepend(tarjeta); $('sin-fotos').hidden = true;
  while ($('fotos').children.length > MAX_FOTOS) $('fotos').lastElementChild.remove();
}
function recibir(tema, carga, paquete = {}) {
  if (carga.length > 1024) { anotar('Mensaje demasiado largo descartado'); return; }
  const mensaje = DatosPanel.validar(tema, carga.toString(), $('raiz').value, $('dispositivo').value);
  if (!mensaje) { anotar('Mensaje inválido descartado en ' + tema.slice(0, 160)); return; }
  const {tipo, datos: d} = mensaje;
  const recibido = Date.now();
  // No tratamos un retenido de otro publicador como una muestra recién tomada.
  if (paquete.retain) { anotar('Mensaje retenido omitido: ' + tipo); return; }
  if (tipo === 'telemetria') {
    if (ultimaSecuencia && ultimaSecuencia.arranque === d.arranque && d.secuencia <= ultimaSecuencia.numero) return;
    ultimaSecuencia = {arranque: d.arranque, numero: d.secuencia};
    muestras.push({...d, recibido_ms: recibido});
    while (muestras.length > 180) muestras.shift();
    ultimaMuestra = recibido;
    $('rssi').textContent = d.rssi_dbm === null ? 'Sin dato' : d.rssi_dbm + ' dBm';
    $('temperatura').textContent = d.temperatura_c === null ? 'Sin dato' : d.temperatura_c.toFixed(1) + ' °C';
  } else {
    const clave = d.dispositivo + '/' + d.arranque + '/' + d.numero;
    if (fotos.has(clave)) return; // QoS 1 puede repetir el aviso.
    fotos.set(clave, {...d, recibido_ms: recibido});
    if (fotos.size > MAX_FOTOS) fotos.delete(fotos.keys().next().value);
    mostrarFoto(d, recibido);
    anotar('Foto ' + d.numero + ' recibida; motivo ' + d.motivo + '; edad declarada ' + d.edad_ms + ' ms');
  }
}
function cerrar() {
  clearInterval(demoTimer); demoTimer = null;
  if (cliente) { const c = cliente; cliente = null; c.end(true); }
  $('estado').textContent = 'Desconectado'; $('conectar').disabled = false;
  $('desconectar').disabled = true;
  for (const id of ['broker', 'raiz', 'dispositivo']) $(id).disabled = false;
}
$('conexion').addEventListener('submit', evento => {
  evento.preventDefault(); cerrar();
  muestras.length = 0; fotos.clear(); registros.length = 0; ultimaSecuencia = null; ultimaMuestra = 0;
  inicioSesion = new Date().toISOString(); $('fotos').replaceChildren(); $('sin-fotos').hidden = false;
  $('rssi').textContent = '—'; $('temperatura').textContent = '—'; $('registro').textContent = '';
  for (const id of ['broker', 'raiz', 'dispositivo']) $(id).disabled = true;
  $('conectar').disabled = true; $('desconectar').disabled = false;
  if (DEMO) {
    $('estado').textContent = 'Demostración con datos simulados (sin broker)';
    let secuencia = 0;
    const tick = () => recibir($('raiz').value + '/' + $('dispositivo').value + '/telemetria', JSON.stringify({
      v: 1, dispositivo: $('dispositivo').value, arranque: 'demo123', secuencia: ++secuencia,
      uptime_ms: secuencia * 1000, rssi_dbm: -55 + Math.round(Math.sin(secuencia) * 3),
      temperatura_c: 35 + Math.sin(secuencia / 5),
    }));
    tick(); demoTimer = setInterval(tick, 1000); return;
  }
  if (typeof mqtt === 'undefined') { cerrar(); $('estado').textContent = 'No se pudo cargar MQTT.js; revisá Internet.'; return; }
  $('estado').textContent = 'Conectando…';
  const c = mqtt.connect(BROKERS[$('broker').value], {
    clientId: 'cam-panel-' + Math.random().toString(16).slice(2), protocolVersion: 4,
    clean: true, keepalive: 30, reconnectPeriod: 10000, connectTimeout: 15000,
  });
  cliente = c;
  c.on('connect', () => {
    if (cliente !== c) return;
    const prefijo = $('raiz').value + '/' + $('dispositivo').value;
    c.subscribe([prefijo + '/telemetria', prefijo + '/foto'], {qos: 1}, (err, temas) => {
      if (cliente !== c) return;
      $('estado').textContent = err || temas.some(t => t.qos === 128) ? 'Suscripción rechazada' : 'Conectado y suscrito';
    });
  });
  c.on('message', (tema, carga, paquete) => { if (cliente === c) recibir(tema, carga, paquete); });
  c.on('reconnect', () => { if (cliente === c) $('estado').textContent = 'Reconectando…'; });
  c.on('close', () => { if (cliente === c) $('estado').textContent = 'Sin conexión; reintentando…'; });
  c.on('error', err => { if (cliente === c) anotar('Error MQTT: ' + err.message); });
});
$('desconectar').addEventListener('click', cerrar);
$('exportar').addEventListener('click', () => {
  const contenido = {inicio_sesion: inicioSesion, exportado: new Date().toISOString(),
    simulado: DEMO, broker: $('broker').value, raiz: $('raiz').value,
    dispositivo: $('dispositivo').value, muestras, fotos: [...fotos.values()], registros};
  const url = URL.createObjectURL(new Blob([JSON.stringify(contenido, null, 2)], {type: 'application/json'}));
  const enlace = document.createElement('a'); enlace.href = url; enlace.download = 'sesion-camara.json'; enlace.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});
setInterval(() => {
  dibujar('grafica-rssi', 'rssi_dbm', 'dBm');
  dibujar('grafica-temperatura', 'temperatura_c', '°C');
  $('ultima').textContent = ultimaMuestra ? 'Última muestra hace ' +
    Math.floor((Date.now() - ultimaMuestra) / 1000) + ' s' : 'Todavía no hay muestras.';
}, 1000);
window.addEventListener('beforeunload', cerrar);
