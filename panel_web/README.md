# Panel de la cámara

Adaptado del panel MQTT de Entrega6. Usa MQTT.js y gráficos SVG, sin instalar un servidor. La entrega anterior queda intacta.

## Uso

1. Abrir `index.html` en el navegador con Internet.
2. Dejar HiveMQ, raíz `ucu-camara-2026-sosadias` y el ID de la placa que aparece en el monitor.
3. Pulsar **Conectar** y esperar **Conectado y suscrito**.
4. Con la placa conectada, deben llegar RSSI y temperatura cada diez segundos.
5. Pulsar BOOT una vez. Después de confirmar la subida aparecerá una tarjeta con motivo, lecturas y enlace al archivo.
6. Usar **Mostrar foto** para abrir la vista de Drive dentro del panel. Si el navegador impide iniciar sesión dentro del recuadro, usar **Abrir en Drive** con una cuenta que tenga permiso. No es necesario hacer públicas las fotos.

Abrir el panel antes de tomar fotos: los avisos no son retenidos y no hay un historial remoto MQTT. Se muestran hasta 60 fotos y 180 muestras en memoria del navegador. Recargar borra esa sesión. El botón de descarga exporta esos datos y el registro de recepción en JSON.

El panel de demostración se puede abrir con `?demo` en la dirección y pulsando Conectar. Muestra solamente telemetría simulada, identificada como tal; no prueba la placa ni el broker.

## Temas y mensajes

Con `ID` igual al identificador de la placa:

- `ucu-camara-2026-sosadias/ID/telemetria`: RSSI y temperatura actuales, cada diez segundos, QoS 0.
- `ucu-camara-2026-sosadias/ID/foto`: aviso tras la confirmación HTTPS, QoS 1. Incluye motivo, lecturas cercanas a la captura, número, arranque e identificador de Drive.

El panel elimina duplicados por dispositivo, arranque y número; QoS 1 puede repetir mensajes. La cola de avisos del firmware tiene ocho huecos más uno en la tarea. El buzón del cliente MQTT tiene un límite de 4096 bytes. Si la cola se llena, puede perderse un aviso, pero la foto ya guardada en Drive no se borra. Apagar pierde los avisos pendientes en RAM.

La placa usa por defecto `mqtts://broker.hivemq.com:8883`, con verificación del certificado; el panel usa `wss://broker.hivemq.com:8884/mqtt`. Se puede cambiar el broker y la raíz en `config_local.h`, usando las mismas opciones en el panel. EMQX usa `mqtts://broker.emqx.io:8883` y `wss://broker.emqx.io:8084/mqtt`.

TLS cifra cada conexión, pero el broker es público: no autentica a la placa ni hace privados los temas. No se publican el JPEG, la contraseña WiFi ni el token de Google. La referencia al archivo no concede permisos de Drive. No publicar imágenes privadas mediante cambios de permisos para resolver un problema del panel.

## Tiempos y pruebas

“Edad al encolar MQTT” es el tiempo que pasó en la placa desde la captura hasta preparar el aviso. No incluye necesariamente el envío posterior ni la recepción o visualización en el navegador. La confirmación QoS 1 del broker tampoco confirma que el panel haya recibido el mensaje.

## Sesión final de medición

Actualizar primero la versión existente del Apps Script y flashear la nueva versión del firmware sin cámara. La placa sincroniza su reloj por SNTP al obtener IP y anuncia `[hora] sincronizada por SNTP`. Las fotos anteriores a ese aviso no tienen hora sincronizada y no sirven para las latencias entre equipos.

En el panel conectado, pegar la URL /exec en el campo correspondiente y pulsar **Preparar medición**. Se hacen tres consultas de hora pública al mismo script, sin token ni acceso a archivos. Se elige la de menor ida y vuelta y se calcula el desfase del navegador respecto de Google con el punto medio. Se indica un margen de la mitad de esa ida y vuelta; no incluye el error de SNTP ni una eventual diferencia de relojes entre servidores de Google. Si cambia el reloj del equipo durante la muestra, hay que prepararla de nuevo.

Desde que indica **Lista**, la tabla toma los primeros diez avisos válidos de fotos capturadas después de esa preparación. Pueden ser de cualquier motivo. Las fotos previas o sin datos de reloj/fila no cuentan. Con diez fotos, la muestra se congela aunque sigan llegando otras. No recargar, reconectar con el botón Conectar ni volver a preparar antes de exportar: esas acciones reinician la muestra. Una reconexión automática del cliente conserva los datos.

Las medidas son:

- **Subida:** desde el primer intento HTTPS hasta recibir la confirmación final en la placa; incluye reintentos y esperas posteriores al primer intento. No incluye el tiempo previo en la cola.
- **Captura a fila:** hora en Google después del primer `SpreadsheetApp.flush()` de la fila menos hora SNTP de captura. Mide confirmación de escritura, no el instante en que alguien ve la fila en el navegador. Luego se escriben las celdas de medición y se confirma otra vez.
- **Captura a respuesta:** intervalo monotónico de la placa; incluye cola, subida y lectura del resultado HTTPS. Es distinto de captura a fila.
- **Captura a panel estimada:** recepción del aviso MQTT en el navegador, corregida con el desfase del reloj, menos captura SNTP. No mide la descarga o representación de la foto.

El JSON de **Descargar datos de la sesión** conserva los diez registros, medias y máximas, y las muestras usadas para comparar relojes. No se ejecuta una medición válida usando el modo demo.

En Sheets se agregan Arranque, Captura epoch (ms), Fila confirmada epoch (ms), Captura a fila (ms) y Reloj de captura. Las filas anteriores se conservan. Para intervalos periódicos, comparar capturas del mismo arranque en la hoja, preferentemente usando epoch en milisegundos. Un período descartado por tiempo muerto puede causar un intervalo múltiplo de 120 s; se debe explicar con el registro del monitor.

Durante esta sesión el firmware informa cada diez segundos para capturar ventanas con HTTPS y MQTT simultáneos. En el monitor, Ctrl+T seguido de Ctrl+L activa o desactiva la escritura de un archivo `log.camara_nube.FECHA.txt`; verificar el mensaje de confirmación. Estos registros quedan excluidos de Git. Conservar el registro completo de la sesión y el JSON para analizar los casos de conexión y los mínimos de memoria.

Prueba de movimientos omitidos en la misma sesión: realizar diez entradas amplias de mano a la escena, sostenerla dos segundos y retirarla. Esperar al menos diez segundos entre intentos y asegurarse de que la foto fija anterior terminó y pasaron cinco segundos desde cualquier captura. Anotar cuántas entradas produjeron un disparo aceptado. Si coincide una periódica durante el tiempo muerto, marcar el intento como interferido y repetirlo; no contarlo como fallo del detector. La retirada también es un cambio de escena y puede producir otra foto, que no debe confundirse con la detección de entrada.

Las pruebas locales preparadas (opcionales) se ejecutan desde la raíz:

Desde la raíz del repositorio, las pruebas locales del panel se ejecutan con:

```powershell
node --test tests/panel_mqtt.test.cjs
```

Estas pruebas simulan mensajes y elementos de la página. No sustituyen probar WSS, acceso autenticado a Drive, gráficos y funcionamiento simultáneo de HTTPS y MQTT en la placa.
