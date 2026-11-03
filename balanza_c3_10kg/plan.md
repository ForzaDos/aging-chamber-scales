# Plan — Balanza del secadero

## Objetivo
Pesar sola la pieza testigo de cada tanda y que la cámara de curado muestre la merma, los días que
faltan para la merma objetivo y el gráfico, en el LCD y en su web.

## Hecho (17/10/2026)
- [x] Firmware: lectura HX711 con filtro y estabilidad; tara y calibración guardadas en NVS.
- [x] Página local (peso en vivo, tarar, calibrar, borrar calibración) en http://balanza.local.
- [x] Probado por Bluetooth (NimBLE): la balanza andaba, pero la cámara se colgaba por falta de RAM
      (le quedan ~74 KB y BLE pide 60–80 KB). Se pasó todo a **Wi-Fi** (mDNS + HTTP).
- [x] Diagnóstico del cableado: negro y blanco estaban invertidos (HX711 saturado en 8388607). Corregido.
- [x] Subida a la NodeMCU (COM4), Wi-Fi a tu red Wi-Fi (IP 192.168.1.90 al momento de probar).
- [x] Calibrada por el usuario (factor ≈ 707,4 cuentas/g).
- [x] La cámara la encuentra en balanza.local y la lee cada segundo (`cam:1` confirmado).
- [x] Filtro anti-fluctuación por la ventilación: mediana 5 + suavizado 10 s + zona muerta 0,3 g.

## Compensación por temperatura + OTA (24/10/2026)
- [x] La cámara manda su temperatura en cada consulta; la balanza estima la de la celda (retardo 20 min).
- [x] Tara y calibración guardan su temperatura (escrita o "Traer de la cámara").
- [x] Compensación automática del cero y de la sensibilidad, con aviso (página de la balanza y de la cámara).
- [x] Aprendizaje de la deriva (regresión con peso fijo, ≥ 1 h y ≥ 2 °C) y coeficientes a mano.
- [x] Actualización por Wi-Fi en /actualizar (usuario y clave en config.h).
- [x] Primera subida por USB (COM4); después OTA probada 2 veces (sin clave: 401). Bug corregido: ultTemp "| 1" daba minutos enormes.
- [ ] Aprender la deriva: 24 h vacía (cero) y después 24 h con un peso fijo (sensibilidad).
- [ ] Volver a tarar y calibrar para que guarden la temperatura.

## Port a ESP32-C3 Super Mini + celda de 10 kg (31/10/2026)
- [x] Copia del proyecto con pines movidos a GPIO3/GPIO4 (en la C3 el 19 es USB).
- [x] Capacidad 10 kg: `CAPACIDAD_G` 10000, sobrecarga 11000, factor por defecto 70, filtro reajustado.
- [x] `Serial.setTxTimeoutMs(0)`: sin esto la web tardaba 2 s por pedido (el CDC bloquea si nadie lee).
- [x] Asentamiento de 2,5 s al arrancar antes de tarar (el auto-tare inicial dejaba 100 g de corrimiento).
- [x] Compila en 1,09 MB con `PartitionScheme=min_spiffs` (el OTA sigue entrando).
- [x] Probado en la placa: Wi-Fi OK en 192.168.1.95, `balanza.local`, página 200, `/api/peso` JSON,
      `/actualizar` 200, tara por serie, peso en -0,1 g estable en vacío, crudo σ ≈ 25 cuentas.
- [x] IP fija .96 y hostname `balanza10k`: la cámara del secadero vuelve a ver sólo la de 1 kg.
- [x] Vigilante de red (reconecta y se reinicia sola) y log de IP por serie.
- [ ] Calibrar con un peso conocido (falta) y aprender la deriva por temperatura.
- [x] Nombre propio en la red: `balanza10k.local` (la de 1 kg mantiene `balanza.local`, que es la que
      busca la cámara). Ojo: si la de 1 kg se renombró a `balanza-2.local` por el choque inicial,
      hay que reiniciarla para que recupere su nombre.

## Pendiente
- [ ] Ver con la ventilación andando si el filtro alcanza (subir `SUAVIZADO_S` o `ZONA_MUERTA_G` si baila).
- [ ] Comprobar la deriva en 24 h con peso fijo (temperatura de la heladera: 12–20 °C).
- [ ] Montar la celda con las piezas impresas (`../celda-carga-1kg`) y colgar la pieza testigo.
- [ ] Opcional: IP fija en el router para la balanza (la cámara igual la vuelve a buscar por nombre).

## Ideas a futuro
- Ajustar `TAU_TERMICA_MIN` con datos reales (comparar el aprendizaje con distintos retardos).
- Varias balanzas (una por pieza testigo): hoy la cámara sigue una sola.
