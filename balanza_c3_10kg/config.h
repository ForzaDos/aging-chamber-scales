#pragma once

// =====================================================================
//  CONFIGURACIÓN de la balanza (ESP32-C3 Super Mini + HX711 + celda de 10 kg)
//  Port del proyecto original: NodeMCU ESP32S + celda de 1 kg.
// =====================================================================

// ---------- Pines HX711 (números GPIO) ----------
#define PIN_HX_DT    3          // en la C3 el 19 es USB D+: no se puede usar
#define PIN_HX_SCK   4

// ---------- Celda ----------
#define CAPACIDAD_G   10000.0f   // celda de 10 kg
#define SOBRECARGA_G  11000.0f   // arriba de esto se marca sobrecarga (10 % del fondo de escala)
#define FACTOR_DEFECTO   70.0f   // cuentas/g antes de calibrar; la celda de 1 kg daba ~707, la de 10 kg ~1/10

// ---------- Filtro y estabilidad ----------
// El HX711 entrega 10 lecturas por segundo (pin RATE a GND, lo normal en estos módulos).
// 1) Mediana de N_MEDIANA lecturas: saca los picos sueltos.
// 2) Suavizado exponencial con constante SUAVIZADO_S: absorbe el vaivén por la ventilación. Si el
//    peso salta más de SALTO_G (colgar o sacar la pieza) sigue el cambio al instante.
// 3) Zona muerta: el peso mostrado solo cambia si el filtrado se movió más de ZONA_MUERTA_G.
#define N_MEDIANA          5
#define SUAVIZADO_S       10.0f    // segundos (más = más quieto pero más lento)
#define SALTO_G           15.0f
#define ZONA_MUERTA_G      0.8f
#define N_ESTABLE         30       // estable si el filtrado varía menos que ESTABLE_G en las últimas 30 lecturas (3 s)
#define ESTABLE_G          1.0f
#define N_TARA            30       // lecturas que se promedian para tarar o calibrar (3 s)
#define DECIMALES          1       // decimales que se muestran

// ---------- Temperatura y compensación ----------
// La cámara de curado manda su temperatura en cada consulta (GET /api/peso?cam=1&t=15.3).
// La celda tarda en tomar la temperatura del aire: se estima con un retardo de TAU_TERMICA_MIN.
// Peso compensado = (peso - kCero·(Tcelda - Ttara)) / (1 + kSpan·(Tcelda - Tcalib)).
// kCero (g/°C) y kSpan (%/°C) se aprenden con "Aprender deriva" o se escriben a mano.
#define TAU_TERMICA_MIN   20.0f    // minutos que tarda la celda en seguir al aire (constante de tiempo)
#define T_VENCE_MIN       10       // sin temperatura de la cámara en este tiempo -> aviso
#define T_LEJOS_C          8.0f    // aviso si la celda está a más de esto de la temperatura de la tara
#define T_ACOMODA_C        1.5f    // aviso si el aire y la celda difieren más que esto (la celda se está acomodando)
#define AVISO_CORR_G       0.5f    // aviso cuando la compensación corrige más que esto
// Aprendizaje: una muestra por minuto con el peso quieto; hace falta al menos APR_MIN_N muestras
// y que la temperatura haya variado APR_MIN_DT °C. Con la balanza vacía (< APR_VACIA_G) se aprende
// la deriva del cero; con un peso fijo, la de la sensibilidad.
#define APR_INTERVALO_S   60
#define APR_MIN_N         60
#define APR_MIN_DT         2.0f
#define APR_VACIA_G       30.0f

// ---------- Actualización por Wi-Fi ----------
// http://balanza.local/actualizar  (usuario y clave de abajo) -> elegir el .bin y "Actualizar".
#define OTA_USUARIO       "admin"
#define OTA_CLAVE         "CAMBIAME"

// ---------- Wi-Fi ----------
// La página queda en http://balanza.local (o la IP que muestra el Monitor Serie).
// La cámara de curado busca la balanza por este nombre en la red.
#define WIFI_SSID         "TU_RED_WIFI"
#define WIFI_PASS         "TU_CLAVE"
#define HOSTNAME          "balanza10k"         // http://balanza10k.local (la de 1 kg conserva balanza.local)
// Si no conecta a la red en 15 s, la balanza crea su propia red (sin clave) y la página
// queda en http://192.168.4.1 . Sigue intentando conectarse a la red de la casa.
#define AP_NOMBRE         "Balanza-10k"

// ---------- IP fija (opcional pero recomendada) ----------
// La .95 quedó cacheada en la cámara cuando esta placa usó "balanza.local": si la seguimos usando, la
// cámara sigue leyendo ESTA balanza en vez de la de 1 kg. Con IP propia y distinta, la cámara falla
// rápido y vuelve a buscar "balanza.local" (que es la de 1 kg). Mejor todavía: reservarla por DHCP.
#define USAR_IP_FIJA      0
#define IP_FIJA           192, 168, 1, 96
#define GATEWAY_FIJA      192, 168, 1, 1
#define MASCARA_FIJA      255, 255, 255, 0
#define DNS_FIJA          192, 168, 1, 1

// ---------- Potencia de transmisión Wi-Fi ----------
// Si la alimentación es floja (USB de la PC, cable fino), los picos de TX hacen caer la tensión: la
// placa "ve" el router (RSSI bueno) pero no logra autenticarse y entra en bucle de reason=2. Bajar la
// potencia reduce el consumo; 8,5 dBm sobra para una habitación.
#define TX_POTENCIA       WIFI_POWER_8_5dBm
