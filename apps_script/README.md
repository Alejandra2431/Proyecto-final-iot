# apps_script — el otro extremo (Clase 7)

La **nube** de la cámara: un programa de **Google Apps Script** que recibe las fotos de la placa por HTTPS, comprueba un **token**, las guarda en una carpeta de **Google Drive** y anota cada una en una **hoja de cálculo**. Es gratuito, no pide tarjeta de crédito y corre en los servidores de Google con tu cuenta.

```
   placa ──HTTPS POST: JSON con la foto en base64──▶ Apps Script (doPost)
                                                       1. ¿el token es el bueno?
                                                       2. ¿ya se guardó esta foto?
                                                       3. ¿es un JPEG razonable?
                                                       4. Drive: «Kaluga - fotos»
                                                       5. Hoja: «Kaluga - registro de fotos»
         ◀── {"ok":true,"nombre":"...","id":"..."} ───┘
```

| Archivo                               | Qué es                                                                                                                                                    |
| ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`Codigo.gs`](Codigo.gs)             | El programa: `doPost()` para la placa, `doGet()` para probar en el navegador, y `configurar()`, `probar()` y `nuevoToken()` para ejecutar a mano |
| [`appsscript.json`](appsscript.json) | El manifiesto (opcional): zona horaria de Montevideo y la aplicación web ya configurada como «ejecutar como yo, acceso cualquier usuario»               |

## Qué cuenta usar

- Una **cuenta personal de Google** (`@gmail.com`). Las cuentas de una institución pueden tener prohibidas las aplicaciones web con acceso anónimo, y entonces la placa recibiría una página de inicio de sesión (`ACCESO MAL`).
- En una solución profesional es obvio que debes usar **una cuenta solo para el proyecto**, la cual sería una cuenta corporativa en el Google Workspace de la empresa. Aunque la empresa no use Google como entorno corporativo de trabajo, si se van a usar aplicaciones de Google como esta, Firebase, Pub Sub, etc., debe conterse con Google Workspace. El script puede escribir en todo el Drive de la cuenta que lo instala (los permisos de `DriveApp` son de todo el Drive): si algo sale mal, sale mal en una cuenta que no tiene nada más.

## Instalación, paso a paso

1. Con la cuenta elegida, abre **[https://script.new](https://script.new)**. Se crea un proyecto «Proyecto sin título»: ponle de nombre **Kaluga fotos**.
2. Borra todo lo que hay en `Código.gs` y pega el contenido de [`Codigo.gs`](Codigo.gs). Guarda (⌘S o Ctrl+S).
3. **Configuración del proyecto** (el engranaje) → marca «Mostrar el archivo de manifiesto `appsscript.json` en el editor» → pega en ese archivo el contenido de [`appsscript.json`](appsscript.json).
4. En la barra de arriba, elige la función **`configurar`** y pulsa **Ejecutar**. La primera vez Google pide permiso:
   - «Se necesita autorización» → **Revisar permisos** → elige la cuenta.
   - «Google no ha verificado esta aplicación» → **Configuración avanzada** → **Ir a Kaluga fotos (no seguro)**.
   - En la lista están: ver y crear archivos en Drive, ver y editar hojas de cálculo, pulsa **Permitir**.
5. En el **registro de ejecución** aparecen el enlace a la carpeta, el de la hoja, la zona horaria y la línea **`NUBE_TOKEN para la placa: ...`**: 32 caracteres. Cópialos.
6. Elige la opción **`probar`** en el dropdown que está a la derecha del botón de Depuración y ejecútala. El registro tiene que decir `{"ok":true,"nombre":"prueba-editor_...jpg",...}`, y en la carpeta **Kaluga - fotos** de tu Drive aparece una foto de un píxel gris, con su fila en la hoja.
7. **Implementar** → **Nueva implementación** → el engranaje de «Seleccionar tipo» → **Aplicación web**:
   - Descripción: `v1`.
   - **Ejecutar como: Yo** (tu correo).
   - **Quién tiene acceso: Cualquier usuario.**
   - **Implementar.** Copia la **URL de la aplicación web**: termina en `/exec`.
8. Abre esa URL en el navegador. Tiene que contestar `{"ok":true,"servicio":"kaluga-fotos","version":"clase7-1.0"}`.
9. En [`../camara_nube/main/camara_nube_main.c`](../camara_nube/main/camara_nube_main.c): `NUBE_URL` pega la URL y `NUBE_TOKEN` pega el token.

> «Ejecutar como: Yo» y «Cualquier usuario» parecen inseguros juntos, y por eso está el token. **Cualquiera puede llamar** a la URL, pero el script no hace nada sin el token, y lo único que sabe hacer es guardar un JPEG en una carpeta. Nadie, ni con el token, puede leer, listar ni borrar nada de tu Drive a través de él.

## Probar desde el PC (opcional)

Con `curl` (macOS, Linux, o Git Bash en Windows), cambiando `URL` y `TOKEN`. Manda la misma foto de un píxel que `probar()`, por el mismo camino que la placa: POST, 302 y GET (eso es `-L`).

```bash
URL='https://script.google.com/macros/s/PEGA-AQUI-EL-ID/exec'
TOKEN='pega-aqui-el-token'
curl -sL -H 'Content-Type: application/json' --data @- "$URL" <<EOF
{"token":"$TOKEN","dispositivo":"prueba-pc","arranque":"pc","numero":1,"edad_ms":0,"ancho":1,"alto":1,"jpeg":"/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/wAALCAABAAEBAREA/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/9oACAEBAAA/ACv/2Q=="}
EOF
```

| Respuesta                                          | Qué significa                                                                                    |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `{"ok":true,"nombre":"prueba-pc_..._1.jpg",...}` | Todo bien: mira tu Drive y la hoja                                                                |
| La misma, con `"repetida":true`                  | Volviste a mandar la foto 1 de `arranque` `pc`: el script la reconoce y no la guarda otra vez |
| `{"ok":false,"error":"token"}`                   | El token no es el del script                                                                      |
| Una página HTML de Google                         | La URL no es la de la implementación, o falta `/exec`                                          |
| Una página de inicio de sesión                   | El acceso no es «Cualquier usuario»                                                             |

## Lo que hace cada función

| Función         | Quién la llama | Qué hace                                                                                                                                                                        |
| ---------------- | --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `configurar()` | Tú, una vez    | Crea la carpeta, la hoja (dentro de la carpeta) y el token, y los guarda en las **propiedades del script**, no en el código. Se puede repetir: reutiliza lo que ya existe |
| `probar()`     | Tú             | Guarda una foto de 1×1 sin pasar por la red: comprueba los permisos de Drive y de la hoja                                                                                       |
| `nuevoToken()` | Tú             | Cambia el token: las placas con el anterior quedan fuera. Es la forma de **revocar**                                                                                       |
| `doGet()`      | El navegador    | Contesta que el servicio existe. No enseña nada privado                                                                                                                         |
| `doPost()`     | La placa        | Recibe la foto. Cualquier error acaba en `{"ok":false,"error":"..."}`                                                                                                          |

| `error`                                                | Por qué                                                                             | La placa                         |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------ | -------------------------------- |
| `token`                                                | El token no coincide                                                                 | Descarta la foto (`TOKEN MAL`) |
| `formato`, `dispositivo`, `tamano`, `no_es_jpeg` | El JSON o la foto no son lo que se espera                                            | Descarta la foto                 |
| `sin_configurar`                                       | No se ejecutó `configurar()`                                                      | Descarta la foto                 |
| `limite`                                               | Más de `MAX_POR_MINUTO` (30) fotos en un minuto de una misma placa                | Reintenta más tarde             |
| `ocupado`                                              | Otra subida tenía el cerrojo más de 15 s                                           | Reintenta más tarde             |
| `interno`                                              | Una excepción: Drive o la hoja fallaron (el detalle queda en **Ejecuciones**) | Reintenta más tarde             |

## Límites de una cuenta gratuita

Según la página de cuotas de Apps Script  (octubre de 2026), para una cuenta personal:

| Límite                              | Valor                                       | En esta clase                               |
| ------------------------------------ | ------------------------------------------- | ------------------------------------------- |
| Tiempo por ejecución                | 6 min                                       | Una foto tarda segundos                     |
| Ejecuciones simultáneas por usuario | 30                                          | Una placa sube de una en una                |
| Lectura y escritura de propiedades   | 50 000 por día                             | Una lectura por foto                        |
| Espacio en Drive                     | 15 GB, compartidos con Gmail y Google Fotos | Una foto VGA ocupa unos 40 KB: unas 375 000 |

> Las cuotas las fija Google y pueden cambiar sin aviso. Un proyecto real pondría las fotos en un almacenamiento de objetos (S3, Cloud Storage, Azure Blob) con una URL firmada por foto: ver [../../detalles/fotos-a-la-nube.md](../../detalles/fotos-a-la-nube.md), §6.

## Compartir las fotos

La carpeta y la hoja son **privadas**: solo las ve la cuenta que instaló el script. Para entregar la App 7, comparte la carpeta **Kaluga - fotos** con el correo del docente como **Lector**: la hoja va dentro.

## Cambiar el script después

- Edita y guarda. Para que la placa use la versión nueva: **Implementar** → **Gestionar implementaciones** → el lápiz → **Versión: Nueva versión** → **Implementar**. **La URL no cambia.**
- **Nueva implementación** crea otra URL: la placa seguiría usando la vieja.
- Para cortar todo: **Gestionar implementaciones** → **Archivar**.

## Estado

El script se ejecutó en Node con Drive, Sheets, Cache, Properties y Lock simulados: `configurar()`, `probar()`, `doGet()` y `doPost()` con peticiones buenas y malas, y de punta a punta con el `nube.c` de la placa (ver el README de [`camara_nube`](../camara_nube/README.md#estado)). **No se ha ejecutado todavía en Apps Script de verdad**: los nombres de los menús de la instalación son los de la interfaz en español de octubre de 2026 y pueden variar.
