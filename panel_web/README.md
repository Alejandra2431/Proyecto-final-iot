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

“Edad al encolar MQTT” es el tiempo que pasó en la placa desde la captura hasta preparar el aviso. No incluye necesariamente el envío posterior ni la recepción o visualización en el navegador. La confirmación QoS 1 del broker tampoco confirma que el panel haya recibido el mensaje. El JSON exportado incluye la hora de recepción del navegador; falta completar el protocolo de medición de latencias del informe.

Desde la raíz del repositorio, las pruebas locales del panel se ejecutan con:

```powershell
node --test tests/panel_mqtt.test.cjs
```

Estas pruebas simulan mensajes y elementos de la página. No sustituyen probar WSS, acceso autenticado a Drive, gráficos y funcionamiento simultáneo de HTTPS y MQTT en la placa.
