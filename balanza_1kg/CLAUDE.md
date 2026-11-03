# CLAUDE.md — Balanza del secadero (firmware)

## Qué es
Firmware de la **balanza colgante** del secadero de salames. Pesa la **pieza testigo** de la tanda
y se la pasa por Wi-Fi a la **cámara de curado** (`../camara_curado`), que registra el peso, calcula la
merma y estima los días que faltan. La parte mecánica (soportes STL) está en `../celda-carga-1kg`.

## Hardware (fijo — no cambiar sin confirmar con el usuario)
- **NodeMCU ESP32S** ("ESP32 Dev Module" en el IDE). En esta PC suele aparecer en **COM4**
  (la cámara en COM3). Identificar SIEMPRE el puerto leyendo el serie antes de subir: la balanza
  imprime `Peso: ...`, la cámara `T=... HR=...`. El usuario a veces los nombra al revés.
- **Celda de carga 1 kg** tipo barra + **HX711**: VCC→3V3, GND→GND, **DT→GPIO19, SCK→GPIO18**.
- Celda→HX711: rojo E+, negro E-, blanco A-, verde A+. (Hubo un problema con negro y blanco
  invertidos: el HX711 daba 8388607 fijo. Con el puente bien cableado A+–A- da pocos mV.)

## Archivos
- `balanza.ino` — lectura del HX711 a 10 Hz, filtro anti-fluctuación, tara/calibración sin bloquear
  (promedio de 30 lecturas crudas), Wi-Fi + mDNS + WebServer síncrono.
- `config.h` — pines, capacidad, filtro, **Wi-Fi (SSID/clave de la casa)** y `HOSTNAME` = `balanza`.
- `pagina.h` — página local (HTML crudo en PROGMEM).
- `LEEME.txt` — instrucciones para el usuario.

## Filtro (pedido por el usuario: dentro del secadero hay ventilación y el peso no debe bailar)
1. Mediana de 5 lecturas crudas (saca picos).
2. Suavizado exponencial, constante `SUAVIZADO_S` = 10 s; si el peso salta > `SALTO_G` (5 g) sigue al instante.
3. Zona muerta `ZONA_MUERTA_G` (0,3 g) para el valor mostrado.
- "Estable" = el valor filtrado varió < 0,5 g en 3 s. La cámara solo registra el peso estable, así que no
  volver a medir estabilidad sobre el crudo (con ventilación nunca estaría quieto).
- Todo se ajusta en `config.h`.

## Temperatura (la balanza está ADENTRO de la cámara)
- La cámara manda su temperatura en cada consulta (`/api/peso?cam=1&t=14.2`). Se estima la de la celda
  con un retardo exponencial (`TAU_TERMICA_MIN` = 20 min). Sin temperatura en 10 min → aviso, y se usa
  la última estimación.
- Peso compensado = (peso − kCero·(Tcelda − Ttara)) / (1 + kSpan·(Tcelda − Tcalib)). Se aplica antes del
  filtro. NVS: `tTara`, `tCal`, `kCero` (g/°C), `kSpan` (fracción/°C; en pantalla y API en %/°C), `comp`.
- Tara y calibración guardan la temperatura: la que manda el usuario (`t=`) o, si no, la estimada de la celda.
- Aprendizaje (`o=A` / `AF` / `AX`): 1 muestra por minuto con peso quieto; regresión del peso SIN compensar
  contra Tcelda. Pide ≥ 60 muestras y ≥ 2 °C de rango. Carga < 20 g → kCero; con peso fijo → kSpan.
  Se guarda en NVS ("apr") cada 10 muestras. Nunca aprender con un salame (pierde peso).
- Aviso (`avn` 0/1/2 + `av`): sin temperatura, compensación apagada, tara sin temperatura, celda lejos de
  la temperatura de la tara (> 8 °C), corrección ≥ 0,5 g, celda acomodándose (aire−celda > 1,5 °C).

## Cuidado con el cableado
- El pin rotulado "GND" junto al 5V es **GPIO11** (flash): NUNCA usarlo. Cableado mal: bucle de reinicios `flash read err`
  (29/10/2026). HX711: VCC→3V3 y GND→GND real; con VCC y GND invertidos se calienta y se puede quemar.

## Actualización por Wi-Fi (OTA)
- `http://balanza.local/actualizar` (desde el 29/10/2026 SIN clave: `OTA_CON_CLAVE 0` en `balanza.ino`; con 1 pide `OTA_USUARIO`/`OTA_CLAVE` de config.h).
- Desde la PC: compilar con `--output-dir`, y subir `balanza.ino.bin` con
  `curl -F "firmware=@balanza.ino.bin" http://<ip>/actualizar`.
- Partición por defecto del ESP32 Dev Module (dos apps de 1,25 MB): el programa ocupa ~1,01 MB. Si pasa
  de 1,25 MB ya no entra por OTA.

## API HTTP (la usa la cámara — mantener compatible con `camara_curado/balanza.cpp`)
- `GET /api/peso[?cam=1&t=<°C>]` → `{"g":123.4|null,"est","sob","err","cal","ocup","factor","rssi","cam","msg",
  "t","tc","corr","comp","kc","ks","tt","tk","avn","av","apr":{n,dt,pend,r2,carga,saltos}|null}`
  (`cam=1` marca que la consulta viene de la cámara; `cam` en la respuesta = la cámara consultó hace < 5 s).
- `POST /api/orden` con `o=T` (tarar) | `o=C500` (calibrar con 500 g) | `o=R` (borrar calibración) |
  `o=A`/`AF`/`AX` (aprender deriva / terminar y aplicar / cancelar) | `o=M1`/`M0` (compensación) |
  `o=K0.05/0.002` (kCero g/°C / kSpan %/°C a mano); `t=<°C>` opcional para T y C.
  La respuesta llega en `msg` de `/api/peso` cuando `ocup` vuelve a 0 ("OK T", "OK C 421.37", "ERR ...").

## Reglas
- **La tara y el factor se guardan en NVS** ("balanza": `offset`, `factor`). NUNCA tarar solo al
  arrancar si ya hay tara guardada: con el salame colgado, un corte de luz lo pondría a cero.
- Sin Bluetooth: se probó BLE (NimBLE) y a la cámara no le alcanzaba la RAM. Todo va por Wi-Fi.
- Compilar con arduino-cli del IDE:
  `"C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" compile -u -p COM4 -b esp32:esp32:esp32 balanza`
- Comentarios y textos en español rioplatense, como el resto de los proyectos.
