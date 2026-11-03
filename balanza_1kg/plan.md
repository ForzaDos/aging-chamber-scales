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

## Pendiente
- [ ] Ver con la ventilación andando si el filtro alcanza (subir `SUAVIZADO_S` o `ZONA_MUERTA_G` si baila).
- [ ] Comprobar la deriva en 24 h con peso fijo (temperatura de la heladera: 12–20 °C).
- [ ] Montar la celda con las piezas impresas (`../celda-carga-1kg`) y colgar la pieza testigo.
- [ ] Opcional: IP fija en el router para la balanza (la cámara igual la vuelve a buscar por nombre).

## Ideas a futuro
- Ajustar `TAU_TERMICA_MIN` con datos reales (comparar el aprendizaje con distintos retardos).
- Varias balanzas (una por pieza testigo): hoy la cámara sigue una sola.
