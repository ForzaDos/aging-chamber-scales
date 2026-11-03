#pragma once
#include <Arduino.h>

// Página local de la balanza (http://balanza.local). Pide /api/peso dos veces por segundo.
const char PAGINA_BALANZA[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Balanza del secadero</title>
<style>
:root{
  --acero:#17191b; --placa:#202326; --placa2:#2a2e32; --borde:#3a3f44;
  --texto:#e9e4da; --tenue:#9a968c; --laton:#c9a54b; --ok:#8fd694; --alarma:#ff6b57;
  --f:"DIN Alternate","Bahnschrift","Roboto Condensed","Arial Narrow",system-ui,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
body{font:16px/1.35 var(--f);color:var(--texto);background:var(--acero);max-width:560px;margin:0 auto;padding:12px 16px 28px}
header{display:flex;justify-content:space-between;align-items:flex-end;padding:6px 2px 10px;border-bottom:2px solid var(--laton);margin-bottom:12px}
h1{font-size:28px;font-weight:700}
.red{display:flex;align-items:center;gap:8px;color:var(--tenue);font-size:14px}
.led{width:12px;height:12px;border-radius:50%;background:#3b3f43;flex:none}
.led.on{background:var(--ok);box-shadow:0 0 9px 1px #8fd69488}
.led.mal{background:var(--alarma)}
.placa{background:var(--placa);border:1px solid var(--borde);padding:14px}
.placa+.placa{margin-top:12px}
h2{font-size:15px;font-weight:600;color:var(--tenue);margin-bottom:10px}
.num{font-size:clamp(64px,22vw,120px);font-weight:700;line-height:1;font-variant-numeric:tabular-nums;color:var(--laton);text-align:right}
.num small{font-size:.32em;font-weight:500;margin-left:4px;color:var(--tenue)}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:12px}
.chip{border:1px solid var(--borde);background:var(--placa2);color:var(--tenue);padding:4px 10px;font-size:14px;font-weight:600}
.chip.ok{color:var(--ok);border-color:#8fd69466}
.chip.mal{color:var(--alarma);border-color:#ff6b5766}
.btn{display:block;width:100%;height:56px;margin-top:14px;background:var(--laton);color:#1b1a14;border:0;border-radius:2px;font:inherit;font-size:20px;font-weight:700;cursor:pointer}
.sec{background:var(--placa2);color:var(--texto);border:1px solid var(--borde);border-radius:2px;height:44px;padding:0 16px;font:inherit;font-weight:600;cursor:pointer}
button:disabled{opacity:.5;cursor:default}
.fila{display:flex;gap:8px;margin-top:10px}
input{flex:1;min-width:0;height:44px;background:var(--placa2);color:var(--texto);border:1px solid var(--borde);padding:0 10px;font:inherit;font-size:17px}
ol{padding-left:20px;color:var(--tenue);font-size:15px}
ol li{margin:4px 0}
.msg{margin-top:10px;min-height:1.3em;font-weight:600;color:var(--ok)}
.msg.err{color:var(--alarma)}
.nota{margin-top:10px;font-size:13px;color:var(--tenue)}
.aviso{margin-top:10px;padding:8px 10px;border-left:3px solid var(--laton);background:var(--placa2);font-size:14px}
.aviso.mal{border-color:var(--alarma);color:var(--alarma)}
.datos{display:grid;grid-template-columns:1fr 1fr;gap:8px 12px;font-size:14px;margin-top:6px}
.datos span{display:block;color:var(--tenue);font-size:12px}
.datos b{font-size:18px;font-variant-numeric:tabular-nums}
label.t{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:10px;color:var(--tenue);font-size:14px}
label.t input{flex:0 0 90px}
</style>
</head>
<body>
<header><h1>Balanza</h1><div class="red"><span class="led" id="conn"></span><span id="ble">--</span></div></header>

<section class="placa">
  <h2>Peso</h2>
  <div class="num"><span id="peso">--</span><small>g</small></div>
  <div class="chips" id="chips"></div>
  <div class="aviso" id="aviso" hidden></div>
  <label class="t">Temperatura de la tara <input type="number" id="tTara" step="0.1" inputmode="decimal" placeholder="°C"> °C
    <button type="button" class="sec traer" data-para="#tTara">Traer de la cámara</button></label>
  <button type="button" class="btn" id="bTara">Tarar (poner a cero)</button>
  <div class="msg" id="msg"></div>
</section>

<section class="placa">
  <h2>Calibración</h2>
  <ol>
    <li>Con la balanza vacía (o con el gancho/hilo puesto), tocá <b>Tarar</b>.</li>
    <li>Colgá un peso conocido (un paquete de 500 g, una pesa) y escribí cuántos gramos son.</li>
    <li>Tocá <b>Calibrar</b> y esperá 2 segundos sin tocar nada.</li>
  </ol>
  <label class="t">Temperatura de la calibración <input type="number" id="tCal" step="0.1" inputmode="decimal" placeholder="°C"> °C
    <button type="button" class="sec traer" data-para="#tCal">Traer de la cámara</button></label>
  <div class="fila"><input type="number" id="gCal" min="1" max="1000" step="any" inputmode="decimal" placeholder="Gramos del peso conocido"><button type="button" class="sec" id="bCal">Calibrar</button></div>
  <div class="fila"><button type="button" class="sec" id="bReset">Borrar calibración</button></div>
  <p class="nota" id="factor"></p>
  <p class="nota">Si dejás la temperatura vacía se usa la que manda la cámara (la estimada de la celda).</p>
</section>

<section class="placa">
  <h2>Temperatura y compensación</h2>
  <div class="datos">
    <div><span>Aire (de la cámara)</span><b id="dAire">--</b></div>
    <div><span>Celda (estimada)</span><b id="dCelda">--</b></div>
    <div><span>Corrección ahora</span><b id="dCorr">--</b></div>
    <div><span>Tara / calibración a</span><b id="dRef">--</b></div>
    <div><span>Deriva del cero</span><b id="dKc">--</b></div>
    <div><span>Deriva de la sensibilidad</span><b id="dKs">--</b></div>
  </div>
  <div class="fila"><button type="button" class="sec" id="bComp">Apagar compensación</button></div>
  <p class="nota">La celda tarda en tomar la temperatura del aire: se estima con un retardo de unos 20 minutos.
    La compensación usa la temperatura de la tara y de la calibración, y los dos coeficientes de deriva.</p>

  <h2 style="margin-top:16px">Aprender la deriva</h2>
  <ol>
    <li><b>Balanza vacía</b> (solo el gancho): aprende la deriva del cero. <b>Con un peso fijo</b>
      (una pesa, una botella cerrada; nunca un salame, que pierde peso): la de la sensibilidad.</li>
    <li>Tocá <b>Empezar</b> y no la toques mientras la temperatura de la cámara cambia al menos 2 °C
      (lo ideal: 24 h con el frío prendiendo y apagando).</li>
    <li>Tocá <b>Terminar y aplicar</b>. Hace falta al menos 1 hora de muestras.</li>
  </ol>
  <div class="datos" id="aprDatos" hidden>
    <div><span>Muestras (1 por minuto)</span><b id="aN">--</b></div>
    <div><span>Rango de temperatura</span><b id="aDt">--</b></div>
    <div><span>Deriva medida</span><b id="aPend">--</b></div>
    <div><span>Ajuste (R²)</span><b id="aR2">--</b></div>
  </div>
  <div class="fila"><button type="button" class="sec" id="bApr">Empezar</button><button type="button" class="sec" id="bAprX" hidden>Cancelar</button></div>
  <details style="margin-top:12px"><summary class="nota">Escribir los coeficientes a mano</summary>
    <div class="fila"><input type="number" id="kc" step="any" placeholder="g/°C (cero)"><input type="number" id="ks" step="any" placeholder="%/°C (sensib.)"><button type="button" class="sec" id="bK">Guardar</button></div>
  </details>
</section>

<p class="nota" style="text-align:center;margin-top:16px"><a href="/actualizar" style="color:var(--tenue)">Actualizar el programa por Wi-Fi</a></p>

<script>
const $ = s => document.querySelector(s);
let esperando = '', ult = null;   // orden enviada cuya respuesta todavía no llegó; último dato
const f = (v, d) => v == null ? '--' : v.toFixed(d).replace('.', ',');

async function orden(o, t) {
  esperando = o[0]; msg('', false);
  const p = { o };
  if (t !== undefined) p.t = t;
  try {
    const r = await fetch('/api/orden', { method: 'POST', body: new URLSearchParams(p) });
    if (!r.ok) throw 0;
    if (o[0] === 'T' || o[0] === 'C') msg(o[0] === 'T' ? 'Tarando, no toques la balanza…' : 'Calibrando, no toques la balanza…', false);
  } catch (_) { esperando = ''; msg('No se pudo enviar la orden', true); }
}
// Temperatura escrita para la tara / calibración: '' = la que manda la cámara, null = inválida.
function tempDe(sel) {
  const v = $(sel).value.trim().replace(',', '.');
  if (!v) return '';
  const t = parseFloat(v);
  return t > -30 && t < 70 ? t : null;
}
function msg(t, err) { const m = $('#msg'); m.textContent = t; m.className = 'msg' + (err ? ' err' : ''); }
function chip(t, c) { return `<span class="chip ${c || ''}">${t}</span>`; }

async function leer() {
  try {
    const r = await fetch('/api/peso', { cache: 'no-store' });
    const d = await r.json();
    $('#conn').className = 'led on';
    $('#peso').textContent = d.ocup ? '···' : d.g == null ? '--' : d.g.toFixed(1).replace('.', ',');
    $('#ble').textContent = (d.cam ? 'cámara conectada' : 'cámara sin conectar') + ` · ${d.rssi} dBm`;
    let c = '';
    if (d.err)       c += chip('HX711 sin respuesta: revisar cables', 'mal');
    else if (d.ocup) c += chip('midiendo…');
    else             c += d.est ? chip('estable', 'ok') : chip('moviéndose');
    if (d.sob)  c += chip('SOBRECARGA', 'mal');
    if (!d.cal) c += chip('sin calibrar', 'mal');
    $('#chips').innerHTML = c;
    $('#factor').textContent = `Factor: ${d.factor} cuentas por gramo` + (d.cal ? '' : ' (por defecto)');
    for (const b of document.querySelectorAll('button')) b.disabled = !!d.ocup;
    ult = d;
    const av = $('#aviso');
    av.hidden = !d.avn; av.textContent = d.av; av.className = 'aviso' + (d.avn > 1 ? ' mal' : '');
    $('#dAire').textContent  = d.t == null ? '--' : f(d.t, 1) + ' °C';
    $('#dCelda').textContent = d.tc == null ? '--' : f(d.tc, 1) + ' °C';
    $('#dCorr').textContent  = d.comp ? (d.corr > 0 ? '+' : '') + f(d.corr, 1) + ' g' : 'apagada';
    $('#dRef').textContent   = `${f(d.tt, 1)} / ${f(d.tk, 1)} °C`;
    $('#dKc').textContent    = f(d.kc, 3) + ' g/°C';
    $('#dKs').textContent    = f(d.ks, 4) + ' %/°C';
    $('#bComp').textContent  = d.comp ? 'Apagar compensación' : 'Encender compensación';
    const a = d.apr;
    $('#aprDatos').hidden = !a; $('#bAprX').hidden = !a;
    $('#bApr').textContent = a ? 'Terminar y aplicar' : 'Empezar';
    if (a) {
      $('#aN').textContent    = a.n + (a.saltos ? ` (${a.saltos} descartadas)` : '');
      $('#aDt').textContent   = f(a.dt, 1) + ' °C';
      $('#aPend').textContent = a.pend == null ? '--' : `${f(a.pend, 3)} g/°C con ${f(a.carga, 0)} g`;
      $('#aR2').textContent   = f(a.r2, 2);
    }
    if (esperando && !d.ocup && d.msg) {
      esperando = '';
      if (d.msg.startsWith('OK T'))       msg('Listo: balanza en cero' + (d.tt != null ? ` (a ${f(d.tt, 1)} °C)` : ''), false);
      else if (d.msg.startsWith('OK C'))  msg('Calibración guardada' + (d.tk != null ? ` (a ${f(d.tk, 1)} °C)` : ''), false);
      else if (d.msg.startsWith('OK R'))  msg('Calibración borrada', false);
      else if (d.msg.startsWith('OK AF')) msg('Deriva aprendida y aplicada: ' + d.msg.slice(6), false);
      else if (d.msg === 'OK AX')         msg('Aprendizaje cancelado', false);
      else if (d.msg.startsWith('OK A'))  msg('Aprendiendo: no toques la balanza', false);
      else if (d.msg.startsWith('OK M'))  msg(d.comp ? 'Compensación encendida' : 'Compensación apagada', false);
      else if (d.msg.startsWith('OK K'))  msg('Coeficientes guardados', false);
      else msg(d.msg.replace(/^ERR /, 'Error: '), true);
    }
  } catch (_) { $('#conn').className = 'led mal'; }
}

for (const b of document.querySelectorAll('.traer')) b.onclick = () => {
  if (!ult || ult.tc == null) { msg('La cámara todavía no mandó la temperatura', true); return; }
  $(b.dataset.para).value = ult.tc.toFixed(1);
};
$('#bTara').onclick  = () => {
  const t = tempDe('#tTara');
  if (t === null) { msg('Temperatura inválida', true); return; }
  orden('T', t);
};
$('#bCal').onclick   = () => {
  const g = parseFloat($('#gCal').value.replace(',', '.')), t = tempDe('#tCal');
  if (!(g > 0 && g <= 1000)) { msg('Escribí los gramos del peso conocido (1 a 1000)', true); return; }
  if (t === null) { msg('Temperatura inválida', true); return; }
  orden('C' + g, t);
};
$('#bComp').onclick = () => orden(ult && ult.comp ? 'M0' : 'M1');
$('#bApr').onclick  = () => orden(ult && ult.apr ? 'AF' : 'A');
$('#bAprX').onclick = () => { if (confirm('¿Cancelar el aprendizaje? Se pierden las muestras.')) orden('AX'); };
$('#bK').onclick    = () => {
  const kc = parseFloat($('#kc').value.replace(',', '.') || '0'), ks = parseFloat($('#ks').value.replace(',', '.') || '0');
  if (isNaN(kc) || isNaN(ks)) { msg('Coeficientes inválidos', true); return; }
  orden(`K${kc}/${ks}`);
};
$('#bReset').onclick = () => { if (confirm('¿Borrar la calibración? El peso va a ser aproximado hasta volver a calibrar.')) orden('R'); };
leer();
setInterval(leer, 500);
</script>
</body>
</html>
)rawliteral";

// Página para actualizar el programa por Wi-Fi (pide usuario y clave, ver config.h).
const char PAGINA_ACTUALIZAR[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Actualizar balanza</title>
<style>body{font:16px system-ui,sans-serif;background:#17191b;color:#e9e4da;max-width:520px;margin:0 auto;padding:20px 16px}
h1{font-size:24px;border-bottom:2px solid #c9a54b;padding-bottom:8px}p{color:#9a968c}
button{margin-top:14px;height:48px;width:100%;background:#c9a54b;border:0;font:inherit;font-weight:700;cursor:pointer}
progress{width:100%;margin-top:14px}#m{margin-top:12px;font-weight:600}</style></head>
<body><h1>Actualizar el programa</h1>
<p>Elegí el archivo <b>balanza.ino.bin</b> (en el IDE: Programa &gt; Exportar binario compilado).
La balanza se reinicia sola al terminar; la tara, la calibración y el aprendizaje quedan guardados.</p>
<input type="file" id="f" accept=".bin"><button id="b">Actualizar</button>
<progress id="p" value="0" max="100" hidden></progress><div id="m"></div>
<script>
document.getElementById('b').onclick = () => {
  const a = document.getElementById('f').files[0], m = document.getElementById('m'), p = document.getElementById('p');
  if (!a) { m.textContent = 'Elegí el archivo .bin'; return; }
  const fd = new FormData(); fd.append('firmware', a, a.name);
  const x = new XMLHttpRequest();
  x.open('POST', '/actualizar');
  x.upload.onprogress = e => { p.hidden = false; p.value = e.loaded / e.total * 100; };
  x.onload = () => { m.textContent = x.responseText; if (x.status === 200) setTimeout(() => location.href = '/', 8000); };
  x.onerror = () => { m.textContent = 'Se cortó la conexión'; };
  x.send(fd);
};
</script></body></html>
)rawliteral";
