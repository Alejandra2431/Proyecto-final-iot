/**
 * Codigo.gs — Clase 7 (Sistemas Embebidos, UCU 2026)
 *
 * La nube de la cámara: recibe las fotos de la Kaluga-1, las guarda en una
 * carpeta de Google Drive y anota cada una en una hoja de cálculo.
 *
 *   placa ──HTTPS POST: JSON con la foto en base64──▶ doPost()
 *                                                       1. ¿el token es el bueno?
 *                                                       2. ¿ya se guardó esta foto?
 *                                                       3. ¿es un JPEG razonable?
 *                                                       4. Drive: crea el archivo
 *                                                       5. Hoja: añade una fila
 *         ◀── {"ok":true,"nombre":"...","id":"..."} ───┘
 *
 * Instalación, paso a paso, en el README.md de esta carpeta:
 *   1. https://script.new con tu cuenta de Google, y pegar este archivo.
 *   2. Ejecutar configurar() una vez: crea la carpeta, la hoja y el token.
 *   3. Ejecutar probar(): una foto de prueba tiene que aparecer en Drive.
 *   4. Implementar > Nueva implementación > Aplicación web. Ejecutar como:
 *      «Yo». Quién tiene acceso: «Cualquier usuario». Copiar la URL /exec.
 *   5. En la placa: NUBE_URL con esa URL y NUBE_TOKEN con el token.
 *
 * El script corre con TU cuenta: puede escribir en tu Drive. La placa no:
 * solo puede pedirle a este script que guarde una foto, y solo si conoce el
 * token. Por eso el token no se publica ni se sube a ningún repositorio.
 */

const VERSION = 'clase7-1.0';

/* Una foto UXGA con calidad alta ronda los 300 KB. 2 MB es un tope contra
 * abusos, no contra fotos normales. */
const MAX_BYTES = 2 * 1024 * 1024;

/* Fotos por minuto y por dispositivo: un programa en bucle no llena el Drive. */
const MAX_POR_MINUTO = 30;

/* Cuánto recuerda el script las fotos ya guardadas, para no repetirlas.
 * CacheService no guarda nada más de 6 horas. */
const RECUERDO_S = 6 * 60 * 60;

const NOMBRE_CARPETA = 'Kaluga - fotos';
const NOMBRE_HOJA = 'Kaluga - registro de fotos';
const ENCABEZADO = ['Captura', 'Recibida', 'Dispositivo', 'Foto n.º', 'Archivo',
                    'Bytes', 'Resolución', 'Espera en la placa (s)', 'Enlace'];

/* ======================================================================= */
/*  Instalación: se ejecutan a mano desde el editor                         */
/* ======================================================================= */

/** Crea la carpeta, la hoja y el token. Se puede ejecutar más de una vez:
 *  lo que ya existe se reutiliza y el token no cambia. */
function configurar() {
  const props = PropertiesService.getScriptProperties();

  let carpeta = abrirCarpeta(props.getProperty('CARPETA_ID'));
  if (carpeta === null) {
    carpeta = DriveApp.createFolder(NOMBRE_CARPETA);
    props.setProperty('CARPETA_ID', carpeta.getId());
  }

  let libro = abrirLibro(props.getProperty('HOJA_ID'));
  if (libro === null) {
    libro = SpreadsheetApp.create(NOMBRE_HOJA);
    const hoja = libro.getSheets()[0];
    hoja.appendRow(ENCABEZADO);
    hoja.setFrozenRows(1);
    DriveApp.getFileById(libro.getId()).moveTo(carpeta);  // junto a las fotos
    props.setProperty('HOJA_ID', libro.getId());
  }

  let token = props.getProperty('TOKEN');
  if (!token) {
    token = tokenAlAzar();
    props.setProperty('TOKEN', token);
  }

  console.log('Carpeta de las fotos: ' + carpeta.getUrl());
  console.log('Hoja del registro:    ' + libro.getUrl());
  console.log('Zona horaria:         ' + Session.getScriptTimeZone());
  console.log('NUBE_TOKEN para la placa: ' + token);
}

/** Cambia el token. Las placas con el anterior dejan de poder subir fotos:
 *  es la forma de «revocar» una placa perdida o un token publicado. */
function nuevoToken() {
  const token = tokenAlAzar();
  PropertiesService.getScriptProperties().setProperty('TOKEN', token);
  console.log('Token nuevo: ' + token + ' (el anterior ya no sirve)');
}

/** Sube una foto de prueba de 1x1 píxel sin pasar por la red: comprueba que
 *  el script tiene permiso para escribir en Drive y en la hoja. */
function probar() {
  const token = PropertiesService.getScriptProperties().getProperty('TOKEN');
  const e = {
    postData: {
      type: 'application/json',
      contents: JSON.stringify({
        token: token, dispositivo: 'prueba-editor', arranque: 'editor',
        numero: Date.now() % 100000, edad_ms: 0, ancho: 1, alto: 1,
        jpeg: JPEG_DE_PRUEBA,
      }),
    },
  };
  e.postData.length = e.postData.contents.length;
  console.log(JSON.stringify(recibir(e)));
}

/* Un JPEG válido de 1x1 píxel gris (331 B, hecho con Pillow), en base64. */
const JPEG_DE_PRUEBA =
  '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0a' +
  'HBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/wAALCAABAAEBAREA/8QAHwAAAQUBAQEB' +
  'AQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1Fh' +
  'ByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZ' +
  'WmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXG' +
  'x8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/9oACAEBAAA/ACv/2Q==';

/* ======================================================================= */
/*  La aplicación web                                                        */
/* ======================================================================= */

/** Abrir la URL /exec en el navegador: comprueba que la implementación
 *  responde. No enseña nada que no sea público. */
function doGet() {
  return responder({ ok: true, servicio: 'kaluga-fotos', version: VERSION });
}

/** Lo que llama la placa. Cualquier error acaba en un JSON con ok:false: la
 *  placa lee «error» y decide si reintentar. */
function doPost(e) {
  try {
    return responder(recibir(e));
  } catch (err) {
    console.error(err && err.stack ? err.stack : err);
    return responder({ ok: false, error: 'interno' });
  }
}

/* ======================================================================= */
/*  Recibir una foto                                                         */
/* ======================================================================= */

function recibir(e) {
  const props = PropertiesService.getScriptProperties();
  const token = props.getProperty('TOKEN');
  if (!token) {
    return { ok: false, error: 'sin_configurar' };    // falta ejecutar configurar()
  }
  if (!e || !e.postData || typeof e.postData.contents !== 'string') {
    return { ok: false, error: 'formato' };
  }
  // base64 ocupa 4/3 del JPEG; con el resto del JSON, algo más.
  if (e.postData.length > Math.ceil(MAX_BYTES / 3) * 4 + 1024) {
    return { ok: false, error: 'tamano' };
  }

  let d;
  try {
    d = JSON.parse(e.postData.contents);
  } catch (err) {
    return { ok: false, error: 'formato' };
  }
  if (d === null || typeof d !== 'object') {
    return { ok: false, error: 'formato' };
  }

  // 1. El token, antes que nada: a quien no lo tiene no se le dice nada más.
  if (!iguales(String(d.token || ''), token)) {
    return { ok: false, error: 'token' };
  }

  // Todo lo que llega de la red se comprueba, aunque venga de nuestra placa.
  const dispositivo = String(d.dispositivo || '');
  const arranque = String(d.arranque || '');
  if (!/^[A-Za-z0-9_-]{1,32}$/.test(dispositivo) || !/^[A-Za-z0-9]{1,16}$/.test(arranque)) {
    return { ok: false, error: 'dispositivo' };
  }
  const numero = entero(d.numero, 0, 1e9);
  const edadMs = entero(d.edad_ms, 0, 7 * 24 * 3600 * 1000);
  const ancho = entero(d.ancho, 1, 10000);
  const alto = entero(d.alto, 1, 10000);
  if (numero === null || edadMs === null || ancho === null || alto === null) {
    return { ok: false, error: 'formato' };
  }

  // 2. ¿Ya se guardó? Si la respuesta se perdió por el camino, la placa
  //    reintenta la MISMA foto: sin esto, quedaría dos veces en Drive. Es el
  //    «al menos una vez» del QoS 1 de MQTT (Clase 6), y la misma solución:
  //    que repetir no cambie nada (idempotencia).
  //
  //    Mirar si ya está y guardarla tiene que ser ATÓMICO: si el primer
  //    intento sigue corriendo (Drive lento) cuando llega el reintento, los
  //    dos verían «no está» y la guardarían los dos. Es la sección crítica de
  //    la Clase 4, con un cerrojo del script en vez de un mutex.
  const cerrojo = LockService.getScriptLock();
  if (!cerrojo.tryLock(15000)) {
    return { ok: false, error: 'ocupado' };
  }
  try {
    return guardar(d, dispositivo, arranque, numero, edadMs, ancho, alto, props);
  } finally {
    cerrojo.releaseLock();
  }
}

/** Pasos 2 a 5, con el cerrojo tomado. */
function guardar(d, dispositivo, arranque, numero, edadMs, ancho, alto, props) {
  const cache = CacheService.getScriptCache();
  const clave = ['hecha', dispositivo, arranque, numero].join('_');
  const previa = cache.get(clave);
  if (previa) {
    const r = JSON.parse(previa);
    r.repetida = true;
    return r;
  }

  if (!dentroDelLimite(cache, dispositivo)) {
    return { ok: false, error: 'limite' };
  }

  // 3. ¿Es un JPEG? Empieza con FF D8 y termina con FF D9. En Apps Script los
  //    bytes son con signo, como en Java: 0xFF es -1, 0xD8 es -40, 0xD9 es -39.
  let bytes;
  try {
    bytes = Utilities.base64Decode(String(d.jpeg || ''));
  } catch (err) {
    return { ok: false, error: 'formato' };
  }
  const n = bytes.length;
  if (n < 4 || n > MAX_BYTES) {
    return { ok: false, error: 'tamano' };
  }
  if (bytes[0] !== -1 || bytes[1] !== -40 || bytes[n - 2] !== -1 || bytes[n - 1] !== -39) {
    return { ok: false, error: 'no_es_jpeg' };
  }

  // La hora de captura la pone la nube, con su reloj: ahora menos lo que la
  // foto esperó en la placa. La placa no necesita saber la hora.
  const ahora = new Date();
  const captura = new Date(ahora.getTime() - edadMs);
  const zona = Session.getScriptTimeZone();
  const nombre = dispositivo + '_' +
                 Utilities.formatDate(captura, zona, 'yyyy-MM-dd_HH-mm-ss') +
                 '_' + numero + '.jpg';

  // 4. Drive.
  const carpeta = DriveApp.getFolderById(props.getProperty('CARPETA_ID'));
  const archivo = carpeta.createFile(Utilities.newBlob(bytes, 'image/jpeg', nombre));
  archivo.setDescription('Foto ' + numero + ' de ' + dispositivo + ', ' +
                         ancho + 'x' + alto + ', ' + VERSION);

  // 5. La hoja. appendRow() es atómica: varias placas a la vez no se pisan.
  const hoja = SpreadsheetApp.openById(props.getProperty('HOJA_ID')).getSheets()[0];
  hoja.appendRow([captura, ahora, dispositivo, numero, nombre, n,
                  ancho + 'x' + alto, Math.round(edadMs / 1000), archivo.getUrl()]);

  const resultado = { ok: true, nombre: nombre, id: archivo.getId(), bytes: n };
  cache.put(clave, JSON.stringify(resultado), RECUERDO_S);
  return resultado;
}

/* ======================================================================= */
/*  Utilidades                                                               */
/* ======================================================================= */

function responder(objeto) {
  return ContentService.createTextOutput(JSON.stringify(objeto))
                       .setMimeType(ContentService.MimeType.JSON);
}

/** 32 caracteres hexadecimales: 122 bits al azar (un UUID versión 4). */
function tokenAlAzar() {
  return Utilities.getUuid().replace(/-/g, '');
}

/** Compara sin salir antes en la primera diferencia: el tiempo no dice
 *  cuántos caracteres acertó quien prueba tokens. */
function iguales(a, b) {
  if (a.length !== b.length) {
    return false;
  }
  let dif = 0;
  for (let i = 0; i < a.length; i++) {
    dif |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return dif === 0;
}

/** Un entero entre min y max, o null. JSON.parse da números de coma
 *  flotante y cadenas: «3», 3.5 o "3" no son lo mismo que 3. */
function entero(v, min, max) {
  return (typeof v === 'number' && Number.isInteger(v) && v >= min && v <= max) ? v : null;
}

function dentroDelLimite(cache, dispositivo) {
  const clave = 'min_' + dispositivo + '_' + Math.floor(Date.now() / 60000);
  const n = Number(cache.get(clave) || 0) + 1;
  cache.put(clave, String(n), 120);
  return n <= MAX_POR_MINUTO;
}

function abrirCarpeta(id) {
  if (!id) return null;
  try {
    const c = DriveApp.getFolderById(id);
    return c.isTrashed() ? null : c;
  } catch (err) {
    return null;
  }
}

function abrirLibro(id) {
  if (!id) return null;
  try {
    return SpreadsheetApp.openById(id);
  } catch (err) {
    return null;
  }
}
