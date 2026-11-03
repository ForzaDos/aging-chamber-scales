# Balanzas para la cámara de curado

Dos balanzas Wi-Fi para colgar la **pieza testigo** de un salame dentro de la cámara y seguir cuánto se seca,
sin abrir la puerta. Son el complemento de
[aging-meat-chamber-controller](https://github.com/ForzaDos/aging-meat-chamber-controller): la cámara las encuentra sola en la red,
les pide el peso, calcula la merma y los días que faltan.

| Carpeta | Placa | Celda de carga | Nombre en la red |
|---|---|---|---|
| [`balanza_1kg`](balanza_1kg) | NodeMCU ESP32S | 1 kg, tipo barra | `balanza.local` |
| [`balanza_c3_10kg`](balanza_c3_10kg) | ESP32-C3 Super Mini | 10 kg | `balanza10k.local` |

Las dos usan un módulo **HX711**, pesan en décimas de gramo y tienen su propia página (tarar, calibrar, compensar la
temperatura) y actualización por Wi-Fi. La cámara admite hasta dos a la vez.

## Cómo usarlas
1. Mirá el `LEEME.txt` de la carpeta que te sirva: cableado, librerías, placa y calibración.
2. Antes de cargar el programa, **editá `config.h`**: `WIFI_SSID` y `WIFI_PASS` (tu red) y, sobre todo, **`OTA_CLAVE`**
   (viene como `CAMBIAME`: cambiala).
3. Cada una se carga por USB la primera vez y después por Wi-Fi, en `http://<nombre>.local/actualizar`.

## Seguridad: leelo antes de armarla
- **Pensadas para una red de casa, sin internet.** No hay nube. Cualquiera que esté en tu Wi-Fi puede ver el peso y
  mandar tarar o calibrar (`/api/orden`): no abras estos puertos al exterior ni uses una red de invitados.
- La actualización por Wi-Fi pide usuario y clave (`OTA_USUARIO` / `OTA_CLAVE`), pero **no está cifrada** (HTTP). Cambiá la clave.
- Si no logra conectarse a tu Wi-Fi, la balanza crea una red propia **sin clave** (`Balanza-Secadero` / `Balanza-10k`) para configurarla
  desde el celular en `http://192.168.4.1`. Mientras esté en ese modo, cualquiera cerca puede entrar.
- `config.h` guarda la clave de tu Wi-Fi: **no subas tu `config.h` real** a ningún repositorio.

## In English
Two optional Wi-Fi scales (1 kg and 10 kg load cells with an HX711) that weigh a control piece hanging inside a dry-aging
chamber. The [chamber controller](https://github.com/ForzaDos/aging-meat-chamber-controller) finds them on the LAN, logs the weight
and estimates the days left. Home network only, no cloud. Edit `config.h` (Wi-Fi and the OTA password) before flashing. The
firmware has no encryption and open endpoints on the LAN: do not expose it to the internet. Interface and docs are in Spanish.

## Licencia
MIT (ver [`LICENSE`](LICENSE)). Se entrega "tal cual", sin garantía.
