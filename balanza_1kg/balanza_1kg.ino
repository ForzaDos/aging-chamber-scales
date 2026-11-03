/*
  Balanza del secadero — NodeMCU ESP32S + HX711 + celda de carga de 1 kg

  - Lee la celda, filtra y detecta cuándo el peso está estable.
  - Página web propia (http://balanza.local o la IP del Monitor Serie): peso, tarar y calibrar.
  - La cámara de curado la encuentra en la red (balanza.local) y consulta /api/peso cada segundo;
    también puede tarar y calibrar con POST /api/orden (o=T | o=C500 | o=R).
  - Tara y calibración quedan guardadas en la memoria de la placa: si se corta la luz con el
    salame colgado, al volver NO se pone a cero (seguiría marcando su peso real).
  - Temperatura: la balanza está ADENTRO de la cámara. La cámara le manda su temperatura en cada
    consulta; con eso se estima la temperatura de la celda y se compensa la deriva (ver config.h).
    La tara y la calibración guardan la temperatura a la que se hicieron.
  - Actualización por Wi-Fi: http://balanza.local/actualizar (usuario y clave en config.h).

  Librería (Gestor de Librerías): HX711 (Bogdan Necula / bogde)
  Placa: "esp32 by Espressif Systems" 3.x -> "ESP32 Dev Module" (sirve para la NodeMCU ESP32S).
  Partition Scheme: el de fábrica ("Default 4MB with spiffs"), que deja lugar para actualizar por Wi-Fi.

  Conexiones:
    HX711 VCC -> 3V3      HX711 GND -> GND
    HX711 DT  -> GPIO19   HX711 SCK -> GPIO18     (se cambian en config.h)
    Celda: rojo E+, negro E-, blanco A-, verde A+ (si el peso sale negativo da igual: la
    calibración corrige el signo).

  Monitor Serie (115200, "Nueva línea"):
    t = tarar   c500 = calibrar con 500 g   r = borrar calibración
    a / af / ax = aprender la deriva por temperatura / terminar y aplicar / cancelar
    m1 / m0 = compensación por temperatura sí / no   k0.05/0.002 = coeficientes a mano (g/°C, %/°C)
*/

#include "config.h"
#include <HX711.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "pagina.h"

static HX711 hx;
static Preferences prefs;
static WebServer web(80);

// ---------- Calibración (guardada en NVS, espacio "balanza") ----------
static long  offsetCrudo = 0;            // lectura cruda con la balanza vacía (tara)
static float factor = FACTOR_DEFECTO;    // cuentas por gramo
static bool  calibrada = false;

// ---------- Lecturas y filtro (ver config.h) ----------
static long     medBuf[N_MEDIANA];       // últimas lecturas crudas, para la mediana
static uint8_t  nMed = 0, iMed = 0;
static float    filtrado = NAN;          // peso suavizado (g)
static float    estBuf[N_ESTABLE];       // últimos valores filtrados, para la estabilidad
static uint8_t  nEst = 0, iEst = 0;
static float    pesoG = NAN;             // lo que se muestra (filtrado con zona muerta)
static bool     estable = false;
static uint32_t ultLectura = 0;          // millis() de la última lectura válida

// ---------- Tara / calibración en curso (no bloquean: se promedian las lecturas que van llegando) ----------
enum Proceso : uint8_t { P_NINGUNO, P_TARA, P_CALIB };
static Proceso   proceso = P_NINGUNO;
static float     calibG = 0;
static long long acum = 0;
static uint16_t  nAcum = 0;
static char      ultMsg[48] = "";         // última respuesta (la leen la página y la cámara)

// ---------- Temperatura y compensación (ver config.h) ----------
static float    tAire = NAN;              // última temperatura que mandó la cámara (°C)
static uint32_t ultTemp = 0;              // millis() de esa temperatura
static float    tCelda = NAN;             // temperatura estimada de la celda (sigue al aire con retardo)
static float    tTara = NAN, tCal = NAN;  // temperatura a la que se taró / calibró (NVS)
static float    kCero = 0;                // deriva del cero, g/°C (NVS)
static float    kSpan = 0;                // deriva de la sensibilidad, fracción por °C (NVS; en pantalla %/°C)
static bool     compensar = true;         // compensación encendida (NVS "comp")
static float    corrG = 0;                // corrección que se está aplicando (g, ya sumada al peso)
static float    tProceso = NAN;           // temperatura que se guarda al terminar la tara / calibración

// Aprendizaje de la deriva: regresión del peso (sin compensar) contra la temperatura de la celda, con
// un peso fijo colgado. Se guarda en NVS cada 10 muestras (sobrevive a un corte o a una actualización).
struct Aprendizaje {
  uint8_t  activo;
  uint16_t n, saltos;
  float    x0, y0, xmin, xmax;            // primera muestra (las sumas van relativas a ella) y rango de T
  double   sx, sy, sxx, sxy, syy;
};
static Aprendizaje apr = {};

static uint32_t ultCamara = 0;            // millis() de la última consulta de la cámara
static bool     apActivo = false;
static uint32_t conectadoDesde = 0;

// =====================================================================

static bool sinHx711() { return millis() - ultLectura > 1500; }

static void responder(const char* msg) {
  strlcpy(ultMsg, msg, sizeof(ultMsg));
  Serial.println(msg);
}

static bool  hayTemp()    { return ultTemp && millis() - ultTemp < T_VENCE_MIN * 60000UL; }
static float tempActual() { return hayTemp() ? tCelda : NAN; }

// Llega una temperatura de la cámara: la celda la sigue con retardo (exponencial con TAU_TERMICA_MIN).
static void nuevaTemp(float t) {
  if (isnan(t) || t < -30 || t > 70) return;
  const uint32_t ahora = millis();
  if (isnan(tCelda)) tCelda = t;
  else tCelda += (t - tCelda) * (1.0f - expf(-(float)(ahora - ultTemp) / (TAU_TERMICA_MIN * 60000.0f)));
  tAire = t;
  ultTemp = ahora ? ahora : 1;              // (no "| 1": podría quedar 1 ms en el futuro y la resta da enorme)
}

// Peso compensado por temperatura. Sin temperatura reciente se usa la última estimación de la celda.
static float compensado(float g) {
  corrG = 0;
  if (!compensar || isnan(tCelda)) return g;
  float c = g;
  if (!isnan(tTara)) c -= kCero * (tCelda - tTara);
  if (!isnan(tCal))  c /= 1.0f + kSpan * (tCelda - tCal);
  corrG = c - g;
  return c;
}

static void guardarFloat(const char* k, float v) { if (isnan(v)) prefs.remove(k); else prefs.putFloat(k, v); }
static void guardarApr() { prefs.putBytes("apr", &apr, sizeof(apr)); }
static void borrarApr()  { apr = {}; prefs.remove("apr"); }

static void vaciarVentana() { nMed = 0; iMed = 0; nEst = 0; iEst = 0; filtrado = NAN; pesoG = NAN; estable = false; }

static void terminarAprendizaje() {
  char b[48];
  const double n = apr.n, den = n * apr.sxx - apr.sx * apr.sx;
  if (apr.n < APR_MIN_N || apr.xmax - apr.xmin < APR_MIN_DT || den <= 0) {
    snprintf(b, sizeof(b), "ERR faltan datos (%u muestras, %.1f C)", apr.n, apr.xmax - apr.xmin);
    responder(b);
    return;                                // sigue aprendiendo
  }
  const float pend  = (n * apr.sxy - apr.sx * apr.sy) / den;      // g por °C, con esta carga
  const float carga = apr.y0 + apr.sy / n;
  if (fabsf(carga) < APR_VACIA_G) {        // vacía: es la deriva del cero
    kCero = pend;
    prefs.putFloat("kCero", kCero);
    snprintf(b, sizeof(b), "OK AF cero %.3f g/C", kCero);
  } else {                                 // con peso: lo que sobra es la deriva de la sensibilidad
    kSpan = (pend - kCero) / carga;
    prefs.putFloat("kSpan", kSpan);
    snprintf(b, sizeof(b), "OK AF sensib %.4f %%/C", kSpan * 100);
  }
  borrarApr();
  responder(b);
}

// o: la orden. t: temperatura para la tara / calibración (NAN = la que manda la cámara).
static void ejecutarOrden(const char* o, float t = NAN) {
  if (!o[0]) return;
  if (proceso != P_NINGUNO) { responder("OCUPADA"); return; }
  if (isnan(t)) t = tempActual();
  char b[48];
  switch (o[0]) {
    case 'T': case 't':
      if (sinHx711()) { responder("ERR sin HX711"); return; }
      proceso = P_TARA; acum = 0; nAcum = 0; tProceso = t;
      Serial.printf("Tarando a %.1f C... no toques la balanza\n", t);
      break;
    case 'C': case 'c': {
      if (sinHx711()) { responder("ERR sin HX711"); return; }
      const float g = atof(o + 1);
      if (!(g > 0 && g <= CAPACIDAD_G)) { responder("ERR peso invalido"); return; }
      calibG = g; proceso = P_CALIB; acum = 0; nAcum = 0; tProceso = t;
      Serial.printf("Calibrando con %.1f g a %.1f C...\n", g, t);
      break;
    }
    case 'R': case 'r':
      factor = FACTOR_DEFECTO; calibrada = false; tCal = NAN;
      prefs.remove("factor"); prefs.remove("tCal");
      vaciarVentana();
      responder("OK R");
      break;
    case 'A': case 'a':                     // A empezar, AF terminar y aplicar, AX cancelar
      if (o[1] == 'F' || o[1] == 'f')      { if (apr.activo) terminarAprendizaje(); else responder("ERR no se esta aprendiendo"); }
      else if (o[1] == 'X' || o[1] == 'x') { borrarApr(); responder("OK AX"); }
      else if (!hayTemp())                 responder("ERR sin temperatura de la camara");
      else { apr = {}; apr.activo = 1; guardarApr(); responder("OK A"); }
      break;
    case 'M': case 'm':
      compensar = o[1] != '0';
      prefs.putBool("comp", compensar);
      responder(compensar ? "OK M1" : "OK M0");
      break;
    case 'K': case 'k': {                   // K<g/°C>/<%/°C>
      const char* barra = strchr(o, '/');
      const float kc = atof(o + 1), ks = barra ? atof(barra + 1) : kSpan * 100;
      if (fabsf(kc) > 5 || fabsf(ks) > 1) { responder("ERR coeficiente fuera de rango"); return; }
      kCero = kc; kSpan = ks / 100;
      prefs.putFloat("kCero", kCero); prefs.putFloat("kSpan", kSpan);
      snprintf(b, sizeof(b), "OK K %.3f %.4f", kCero, kSpan * 100);
      responder(b);
      break;
    }
    default:
      responder("ERR orden desconocida");
  }
}

static void terminarProceso(long prom) {
  char b[48];
  if (proceso == P_TARA) {
    offsetCrudo = prom;
    tTara = tProceso;
    prefs.putLong("offset", offsetCrudo);
    guardarFloat("tTara", tTara);
    vaciarVentana();
    responder("OK T");
  } else if (proceso == P_CALIB) {
    const long delta = prom - offsetCrudo;
    if (labs(delta) < 1000) {
      responder("ERR lectura muy baja (falta tarar o el peso?)");
    } else {
      factor = (float)delta / calibG;     // con la celda al revés queda negativo y el peso sale bien igual
      calibrada = true;
      tCal = tProceso;
      prefs.putFloat("factor", factor);
      guardarFloat("tCal", tCal);
      vaciarVentana();
      snprintf(b, sizeof(b), "OK C %.2f", factor);
      responder(b);
    }
  }
  proceso = P_NINGUNO;
}

static void leerHx711() {
  if (!hx.is_ready()) return;
  const long crudo = hx.read();
  if (crudo == 8388607L || crudo == -8388608L) return;      // fuera de escala / glitch del HX711
  ultLectura = millis();

  if (proceso != P_NINGUNO) {
    acum += crudo;
    if (++nAcum >= N_TARA) terminarProceso((long)(acum / nAcum));
    return;
  }

  // 1) Mediana de las últimas N_MEDIANA lecturas crudas, en gramos y compensada por temperatura
  medBuf[iMed] = crudo;
  iMed = (iMed + 1) % N_MEDIANA;
  if (nMed < N_MEDIANA) nMed++;
  long ord[N_MEDIANA];
  memcpy(ord, medBuf, sizeof(long) * nMed);
  for (uint8_t a = 1; a < nMed; a++)                        // inserción: son 5 números
    for (uint8_t b = a; b > 0 && ord[b - 1] > ord[b]; b--) { const long t = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = t; }
  const float g = compensado(((float)ord[nMed / 2] - offsetCrudo) / factor);

  // 2) Suavizado exponencial; un salto grande se sigue al instante
  const float alfa = 1.0f / (SUAVIZADO_S * 10.0f);         // 10 lecturas por segundo
  if (isnan(filtrado) || fabsf(g - filtrado) > SALTO_G) { filtrado = g; nEst = 0; iEst = 0; }
  else filtrado += alfa * (g - filtrado);

  // 3) Zona muerta para lo que se muestra
  if (isnan(pesoG) || fabsf(filtrado - pesoG) >= ZONA_MUERTA_G) pesoG = filtrado;

  // Estable: el filtrado casi no se movió en los últimos 3 s
  estBuf[iEst] = filtrado;
  iEst = (iEst + 1) % N_ESTABLE;
  if (nEst < N_ESTABLE) nEst++;
  float mn = estBuf[0], mx = estBuf[0];
  for (uint8_t k = 1; k < nEst; k++) { mn = min(mn, estBuf[k]); mx = max(mx, estBuf[k]); }
  estable = nEst == N_ESTABLE && mx - mn < ESTABLE_G;
}

// Aprendizaje: una muestra por minuto, con el peso quieto y temperatura reciente.
static void aprenderLoop() {
  static uint32_t ult = 0;
  if (!apr.activo || millis() - ult < APR_INTERVALO_S * 1000UL) return;
  if (!estable || proceso != P_NINGUNO || isnan(filtrado) || sinHx711() || !hayTemp()) return;
  ult = millis();
  const float x = tCelda, y = filtrado - corrG;            // peso SIN compensar
  if (apr.n == 0) { apr.x0 = x; apr.y0 = y; apr.xmin = apr.xmax = x; }
  else if (fabsf(y - (apr.y0 + apr.sy / apr.n)) > 20) { apr.saltos++; return; }   // tocaron la balanza
  const double dx = x - apr.x0, dy = y - apr.y0;
  apr.sx += dx; apr.sy += dy; apr.sxx += dx * dx; apr.sxy += dx * dy; apr.syy += dy * dy;
  apr.xmin = min(apr.xmin, x); apr.xmax = max(apr.xmax, x);
  if (++apr.n % 10 == 0) guardarApr();
}

// Aviso de temperatura para la página y la cámara. Devuelve 0 = nada, 1 = información, 2 = atención.
static uint8_t avisoTemp(char* b, size_t tam) {
  b[0] = 0;
  if (!ultTemp) { snprintf(b, tam, "Sin temperatura de la cámara: no se compensa"); return compensar ? 2 : 1; }
  if (!hayTemp()) {
    snprintf(b, tam, "Sin temperatura de la cámara hace %lu min: se usa la última (%.1f °C)",
             (unsigned long)((millis() - ultTemp) / 60000UL), tCelda);
    return 2;
  }
  if (!compensar) { snprintf(b, tam, "Compensación por temperatura apagada"); return 1; }
  if (isnan(tTara) && kCero != 0) { snprintf(b, tam, "Tarar de nuevo para guardar la temperatura de la tara"); return 2; }
  if (!isnan(tTara) && fabsf(tCelda - tTara) > T_LEJOS_C) {
    snprintf(b, tam, "Celda a %.1f °C, muy lejos de la tara (%.1f °C): corrección aproximada", tCelda, tTara);
    return 2;
  }
  if (fabsf(corrG) >= AVISO_CORR_G) {
    snprintf(b, tam, "Compensando %+.1f g por temperatura (celda %.1f °C)", corrG, tCelda);
    return 1;
  }
  if (fabsf(tAire - tCelda) > T_ACOMODA_C) {
    snprintf(b, tam, "La celda se está acomodando a la temperatura (aire %.1f, celda %.1f °C)", tAire, tCelda);
    return 1;
  }
  if (apr.activo) { snprintf(b, tam, "Aprendiendo la deriva: %u muestras, %.1f °C de rango", apr.n, apr.xmax - apr.xmin); return 1; }
  if (kCero == 0 && kSpan == 0) { snprintf(b, tam, "Falta aprender la deriva por temperatura"); return 1; }
  return 0;
}

// ---------- Wi-Fi y página ----------
static bool hayRedConfigurada() { return strcmp(WIFI_SSID, "TU_RED_WIFI") != 0; }

static void levantarAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NOMBRE);
  apActivo = true;
  Serial.printf("Wi-Fi: red propia \"%s\" -> http://%s\n", AP_NOMBRE, WiFi.softAPIP().toString().c_str());
}

static const char* num(char* b, size_t tam, float v, int dec) {
  if (isnan(v)) strlcpy(b, "null", tam); else snprintf(b, tam, "%.*f", dec, v);
  return b;
}

// {"g":123.4|null,"est":1,"sob":0,"err":0,"cal":1,"ocup":0,"factor":..,"rssi":-60,"cam":1,"msg":"OK T",
//  "t":15.3|null,"tc":15.12|null,"corr":-0.40,"comp":1,"kc":0.0500,"ks":0.00200,"tt":14.8|null,"tk":14.8|null,
//  "avn":1,"av":"...","apr":{"n":120,"dt":3.20,"pend":0.051,"r2":0.93,"carga":0.2,"saltos":0}|null}
// t = temperatura del aire (la de la cámara), tc = estimada de la celda, corr = gramos que se están corrigiendo,
// kc = g/°C, ks = %/°C, tt / tk = temperatura de la tara / calibración, avn/av = aviso (0 nada, 1 info, 2 atención).
// La cámara pide /api/peso?cam=1&t=<temperatura> (así la página sabe si la cámara la está leyendo).
static void enviarPeso() {
  if (web.hasArg("cam")) ultCamara = millis() | 1;
  if (web.hasArg("t")) nuevaTemp(web.arg("t").toFloat());
  const bool err = sinHx711();
  char g[16], t[12], tc[12], tt[12], tk[12], av[100], ap[140] = "null";
  num(g, sizeof(g), err ? NAN : pesoG, DECIMALES);
  const uint8_t avn = avisoTemp(av, sizeof(av));
  if (apr.activo) {
    const double n = apr.n, den = n * apr.sxx - apr.sx * apr.sx, cov = n * apr.sxy - apr.sx * apr.sy;
    const double vy = n * apr.syy - apr.sy * apr.sy;
    const bool hay = apr.n >= 3 && den > 0;
    char pe[16], r2[12];
    num(pe, sizeof(pe), hay ? cov / den : NAN, 3);
    num(r2, sizeof(r2), hay && vy > 0 ? cov * cov / (den * vy) : NAN, 2);
    snprintf(ap, sizeof(ap), "{\"n\":%u,\"dt\":%.2f,\"pend\":%s,\"r2\":%s,\"carga\":%.1f,\"saltos\":%u}",
             apr.n, apr.n ? apr.xmax - apr.xmin : 0.0f, pe, r2, apr.n ? apr.y0 + apr.sy / n : 0.0, apr.saltos);
  }
  char b[720];
  snprintf(b, sizeof(b),
           "{\"g\":%s,\"est\":%d,\"sob\":%d,\"err\":%d,\"cal\":%d,\"ocup\":%d,\"factor\":%.3f,\"rssi\":%d,\"cam\":%d,\"msg\":\"%s\","
           "\"t\":%s,\"tc\":%s,\"corr\":%.2f,\"comp\":%d,\"kc\":%.4f,\"ks\":%.5f,\"tt\":%s,\"tk\":%s,\"avn\":%u,\"av\":\"%s\",\"apr\":%s}",
           g, estable && !err, !isnan(pesoG) && pesoG > SOBRECARGA_G, err, calibrada, proceso != P_NINGUNO, factor,
           (int)WiFi.RSSI(), ultCamara && millis() - ultCamara < 5000, ultMsg,
           num(t, sizeof(t), hayTemp() ? tAire : NAN, 1), num(tc, sizeof(tc), tCelda, 2), corrG, compensar,
           kCero, kSpan * 100, num(tt, sizeof(tt), tTara, 1), num(tk, sizeof(tk), tCal, 1), avn, av, ap);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", b);
}

// o=<orden> [&t=<°C de la tara / calibración; vacío = la de la cámara>]
static void recibirOrden() {
  if (!web.hasArg("o")) { web.send(400, "application/json", "{\"ok\":false}"); return; }
  ultMsg[0] = 0;
  const String t = web.hasArg("t") ? web.arg("t") : String("");
  ejecutarOrden(web.arg("o").c_str(), t.length() ? t.toFloat() : NAN);
  web.send(200, "application/json", "{\"ok\":true}");
}

// ---------- Actualización por Wi-Fi (http://balanza.local/actualizar) ----------
// Sin clave (red de casa). Para volver a pedirla: poner OTA_CON_CLAVE en 1 (usuario/clave de config.h).
#define OTA_CON_CLAVE 0
static bool otaAutorizada = false;
static bool autorizadoOta() {
#if OTA_CON_CLAVE
  return web.authenticate(OTA_USUARIO, OTA_CLAVE);
#else
  return true;
#endif
}

static void paginaActualizar() {
  if (!autorizadoOta()) return web.requestAuthentication();
  web.send_P(200, "text/html; charset=utf-8", PAGINA_ACTUALIZAR);
}

static void finActualizar() {
  if (!otaAutorizada) return web.requestAuthentication();
  const bool ok = !Update.hasError();
  web.sendHeader("Connection", "close");
  web.send(ok ? 200 : 500, "text/plain; charset=utf-8",
           ok ? "OK: actualizada, reiniciando..." : "ERROR al actualizar (sigue el programa anterior)");
  if (ok) { Serial.println("Actualizacion OK, reiniciando"); delay(500); ESP.restart(); }
}

static void subidaActualizar() {
  HTTPUpload& u = web.upload();
  if (u.status == UPLOAD_FILE_START) {
    otaAutorizada = autorizadoOta();
    if (!otaAutorizada) return;
    if (apr.activo) guardarApr();
    Serial.printf("Actualizando con %s...\n", u.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (!otaAutorizada) {
    return;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (Update.write(u.buf, u.currentSize) != u.currentSize) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

static void wifiInit() {
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  if (hayRedConfigurada()) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Wi-Fi: conectando a " WIFI_SSID);
    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
    Serial.println();
  }
  if (WiFi.status() != WL_CONNECTED) levantarAp();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", PAGINA_BALANZA); });
  web.on("/api/peso", HTTP_GET, enviarPeso);
  web.on("/api/orden", HTTP_POST, recibirOrden);
  web.on("/actualizar", HTTP_GET, paginaActualizar);
  web.on("/actualizar", HTTP_POST, finActualizar, subidaActualizar);
  web.onNotFound([] { web.send(404, "text/plain", "No encontrado"); });
  web.begin();
}

static void wifiLoop() {
  web.handleClient();
  const bool con = WiFi.status() == WL_CONNECTED;
  if (con && !conectadoDesde) {
    conectadoDesde = millis() | 1;
    WiFi.setSleep(false);       // sin ahorro de energía: contesta siempre rápido a la cámara
    Serial.printf("Wi-Fi: conectado, http://%s  (o http://" HOSTNAME ".local)\n", WiFi.localIP().toString().c_str());
  }
  if (!con) conectadoDesde = 0;
  // Sin la red de la casa: reintento cada 30 s. La reconexión automática del núcleo se rinde con algunos motivos
  // de corte (pasó al cambiar el canal del router: la balanza quedó afuera hasta reiniciarla).
  static uint32_t ultReintento = 0;
  if (!con && !apActivo && millis() - ultReintento > 30000UL) {
    ultReintento = millis();
    Serial.println("Wi-Fi: sin conexion, reintento");
    WiFi.reconnect();
  }
  // Con la red de la casa funcionando hace 2 minutos, se apaga la red propia.
  if (apActivo && con && millis() - conectadoDesde > 120000UL) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActivo = false;
    Serial.println("Wi-Fi: red propia apagada (ya hay conexión a la red de la casa)");
  }
}

// =====================================================================

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== Balanza del secadero ==");

  prefs.begin("balanza", false);
  factor    = prefs.getFloat("factor", FACTOR_DEFECTO);
  calibrada = prefs.isKey("factor");
  const bool hayTara = prefs.isKey("offset");
  offsetCrudo = prefs.getLong("offset", 0);
  tTara     = prefs.getFloat("tTara", NAN);
  tCal      = prefs.getFloat("tCal", NAN);
  kCero     = prefs.getFloat("kCero", 0);
  kSpan     = prefs.getFloat("kSpan", 0);
  compensar = prefs.getBool("comp", true);
  if (prefs.getBytesLength("apr") == sizeof(apr)) prefs.getBytes("apr", &apr, sizeof(apr));

  hx.begin(PIN_HX_DT, PIN_HX_SCK);
  if (!hx.wait_ready_timeout(2000)) Serial.println("HX711 NO RESPONDE: revisar VCC, GND, DT y SCK");

  // Solo la primera vez (sin tara guardada) se tara sola al arrancar.
  if (!hayTara) { proceso = P_TARA; Serial.println("Primera vez: tarando... dejá la balanza vacía"); }
  Serial.printf("Factor %.3f (%s), tara %ld a %.1f C\n", factor, calibrada ? "calibrada" : "SIN CALIBRAR", offsetCrudo, tTara);
  Serial.printf("Compensacion %s: cero %.3f g/C, sensibilidad %.4f %%/C%s\n", compensar ? "SI" : "NO",
                kCero, kSpan * 100, apr.activo ? " (aprendiendo)" : "");

  wifiInit();
  Serial.println("Comandos: t | c<gramos> | r | a | af | ax | m1 | m0 | k<g/C>/<%/C>");
}

void loop() {
  leerHx711();
  aprenderLoop();

  if (Serial.available()) {
    String c = Serial.readStringUntil('\n');
    c.trim();
    ejecutarOrden(c.c_str());
  }

  static uint32_t ultLog = 0;
  if (millis() - ultLog >= 1000) {
    ultLog = millis();
    if (sinHx711())          Serial.println("HX711 sin lecturas");
    else if (!isnan(pesoG))  Serial.printf("Peso: %8.*f g %s%s  T celda %.2f corr %+.2f g\n", DECIMALES, pesoG,
                                           estable ? "(estable)" : "", pesoG > SOBRECARGA_G ? " [SOBRECARGA]" : "",
                                           tCelda, corrG);
  }

  wifiLoop();
  delay(2);
}
