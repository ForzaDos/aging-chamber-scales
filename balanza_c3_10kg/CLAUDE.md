# CLAUDE.md — Balanza del secadero (firmware) — port ESP32-C3 + celda de 10 kg

## Qué es
Firmware de la **balanza colgante** del secadero de salames. Pesa la **pieza testigo** de la tanda
y se la pasa por Wi-Fi a la **cámara de curado** (`../camara_curado`), que registra el peso, calcula la
merma y estima los días que faltan. La parte mecánica (soportes STL) está en `../celda-carga-1kg`.

## Hardware (fijo — no cambiar sin confirmar con el usuario)
- **NodeMCU ESP32-C3 Super Mini** ("ESP32C3 Dev Module"). Aparece en **COM6** como USB-Serial/JTAG
  (VID:PID `303A:1001`, MAC <MAC de tu placa>). Identificar SIEMPRE el puerto leyendo el serie antes
  de subir: la balanza imprime `Peso: ...`, la cámara `T=... HR=...`. El usuario a veces los nombra al revés.
- **Celda de carga 10 kg** tipo barra + **HX711**: VCC→3V3, GND→GND, **DT→GPIO3, SCK→GPIO4**.
  El proyecto original (ESP32S) usaba GPIO19/18: en la C3 el 18/19 es el USB nativo, no se pueden usar.
  En la C3 tampoco se tocan 11..17 (flash interno) ni 2/8/9 (strapping; el 9 es BOOT).
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

## Diferencias del port (C3 + 10 kg) — 31/10/2026
- `Serial.setTxTimeoutMs(0)`: en la C3 el Monitor Serie es USB CDC; sin esto cada `Serial.print` bloquea
  hasta 2 s cuando nadie lo lee y la web (y el filtro) se arrastraban. Fue lo que hacía que la página
  tardara 2 s por pedido.
- Asentamiento de 2,5 s al arrancar antes de procesar lecturas (`arranqueListo`): si se tara con las
  primeras lecturas, el cero queda corrido (pasó: marcaba 100 g en vacío).
- Crudo en el log de 1 Hz (`crudo N`) para diagnosticar sin tocar las constantes.
- Calibración: el guard de "diferencia muy chica" ahora es `factor × 20` (≈20 g), no 1000 cuentas fijas.
- `CAPACIDAD_G` 10000, `SOBRECARGA_G` 11000, `FACTOR_DEFECTO` 70 (la celda de 1 kg daba ~707).
- Filtro reajustado por el ruido mayor de la celda de 10 kg: `SALTO_G` 15 g, `ZONA_MUERTA_G` 0,8 g,
  `ESTABLE_G` 1,0 g, `APR_VACIA_G` 30 g.
- Partición `min_spiffs`: 1,9 MB por ranura, así sigue entrando el OTA (el binario ocupa 1,09 MB).
- Medición real del port: crudo σ ≈ 25 cuentas (pico a pico ~80). Con factor 70 eso es ~0,35 g de ruido;
  con la celda calibrada (factor real ~70–110) queda en 0,2–0,35 g.

- **Nombre en la red**: esta balanza es `balanza10k.local` (HOSTNAME = "balanza10k", IP típica
  192.168.1.96). La balanza de 1 kg del secadero conserva `balanza.local`: la cámara sigue hablando con
  esa, no con esta. Si alguna vez las dos publican el mismo nombre, el mDNS le agrega un sufijo a la
  segunda (`balanza-2.local`) hasta que se reinicia, y la cámara termina leyendo la placa equivocada.

- **Vigilante de red** (`vigilarRed`, una prueba por minuto): `WiFi.status()` puede decir "conectado" mientras el
  router ya olvidó al cliente, y la balanza queda sorda hasta reiniciarla. Se prueba un TCP al gateway;
  con 5 fallos seguidos (5 min) reconecta (volviendo a fijar la IP) y, tras 3 reconexiones sin éxito, se reinicia sola.
- El **AP de respaldo ya no se apaga en caliente**: el `WiFi.mode(WIFI_STA)` con la STA asociada dejaba
  la placa "conectada" pero sin responder. Si se levantó el AP, queda hasta el próximo reinicio.
- **Log de red**: la IP se imprime al cambiar y cada 10 s (`Red: ip=... gw=... rssi=... host=...`).
  Sin eso es imposible saber a qué dirección entró cuando el DHCP cambia.
- Si la PC no llega a la placa aunque la placa diga "conectado": suele ser una entrada ARP vieja en
  Windows. `ipconfig /flushdns` y `arp -d <ip>`; si sigue, reiniciar la balanza.
- Conviene una **reserva DHCP** en el router para la MAC `<MAC de tu placa>` (así la IP no cambia).

- **IP fija `192.168.1.96`** (`USAR_IP_FIJA` en config.h). La .95 quedó cacheada en la cámara del
  secadero cuando esta placa usó `balanza.local`, y la cámara seguía leyendo ESTA balanza (se veía
  `cam:1` con `t=` en `/api/peso`). Con IP propia, la cámara falla rápido y vuelve a resolver
  `balanza.local`, que es la de 1 kg. Lo ideal igual es reservar por DHCP las dos direcciones.

- **Potencia de TX a 8,5 dBm** (\TX_POTENCIA\ en config.h). Con la potencia máxima la placa veía el
  router (RSSI -40) pero no lograba autenticarse: bucle de eason=2 AUTH_EXPIRE\ porque los picos de
  transmisión hacían caer la tensión del USB de la PC. Con 8,5 dBm quedó estable (60/60 pedidos, 34 ms
  promedio). Si se alimenta con fuente propia o hub con alimentación se puede volver a probar el máximo.

## Actualización por Wi-Fi (OTA)
- `http://balanza10k.local/actualizar` (desde el 29/10/2026 SIN clave: `OTA_CON_CLAVE 0` en `balanza.ino`; con 1 pide `OTA_USUARIO`/`OTA_CLAVE` de config.h).
- Desde la PC: compilar con `--output-dir`, y subir `balanza.ino.bin` con
  `curl -F "firmware=@balanza.ino.bin" http://<ip>/actualizar`.
- En la C3 con `min_spiffs` hay dos ranuras de 1,9 MB: el programa ocupa 1,09 MB, así que el OTA
  sigue entrando (máximo 1,9 MB).

## API HTTP (la usa la cámara — mantener compatible con `camara_curado/balanza.cpp`)
- `GET /api/peso[?cam=1&t=<°C>]` → `{"host":"balanza10k","g":123.4|null,"est","sob","err","cal","ocup","factor","rssi","cam","msg",
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
- Compilar/subir: `arduino-cli compile -b esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=min_spiffs balanza_c3_10kg`
  y después `arduino-cli upload -p <PUERTO> ...` (o por Wi-Fi desde `http://balanza10k.local/actualizar`).
- Comentarios y textos en español rioplatense, como el resto de los proyectos.
- `host` en `/api/peso`: la cámara lo compara con el nombre que busca (`BALANZAS[].host`) y descarta lo que viene de otra placa (v1.0.20).
