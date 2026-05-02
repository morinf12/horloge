#include "webui.h"
#include "config.h"
#include "display.h"
#include "menu.h"
#include "font_data.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <sys/time.h>

static WebServer s_server(80);
static DNSServer s_dns;
static Preferences s_prefs;
static bool s_apMode = true;       // true while AP/captive-portal active

// ---------------- Embedded UI -------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Horloge</title>
<style>
  :root { color-scheme: dark; }
  @font-face { font-family:'7seg'; src:url('/font.ttf') format('truetype'); }
  * { box-sizing: border-box; }
  body { margin:0; font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
         background:#0e1726; color:#e6edf3; }
  header { background:#1f6feb; padding:14px 18px; font-size:20px; font-weight:600;
           letter-spacing:.5px; }
  main { max-width: 520px; margin: 0 auto; padding: 16px; }
  .card { background:#161f33; border:1px solid #243049; border-radius:10px;
          padding:16px; margin-bottom:14px; }
  h2 { margin:0 0 12px 0; font-size:16px; color:#9fb3d1; font-weight:600;
       text-transform:uppercase; letter-spacing:1px; }
  button { padding:10px; border-radius:8px; border:1px solid #2c3a5a;
           background:#0e1726; color:#e6edf3; font-size:15px; cursor:pointer;
           width:100%; }
  button.primary { background:#1f6feb; border-color:#1f6feb; color:white; }
  .time { text-align:center; padding:12px 0; display:flex; justify-content:center; align-items:baseline; gap:0; }
  .time .d { font-family:'7seg',monospace; font-size:64px; display:inline-block;
             width:0.75em; text-align:right; position:relative; color:transparent;
             --fg:#7ee787; --dim:rgba(126,231,135,0.15); }
  .time .d::before { content:'8'; position:absolute; top:0; right:0;
             color:var(--dim); pointer-events:none; }
  .time .d::after { content:attr(data-v); position:absolute; top:0; right:0;
             color:var(--fg); }
  .time .colon { font-family:'7seg',monospace; font-size:64px; color:#7ee787; width:0.35em;
             text-align:center; display:inline-block; }
  .row { display:flex; align-items:center; gap:10px; margin:8px 0; }
  .row label { width:120px; color:#9fb3d1; flex-shrink:0; }
  .row input { flex:1; padding:8px; border-radius:6px;
        border:1px solid #2c3a5a; background:#0e1726; color:#e6edf3; font-size:15px; }
  a { color:#7ee787; }
  footer { font-size:12px; color:#7280a0; text-align:center; padding:12px; }
</style>
</head>
<body>
<header>Horloge <a href="/debug" style="float:right;color:white;font-size:14px;opacity:0.7;text-decoration:none">Debug</a></header>
<main>

  <section class="card">
    <h2>Heure</h2>
    <div class="time" id="clock">
      <span class="d" id="d0" data-v="-">8</span><span class="d" id="d1" data-v="-">8</span><span class="colon">:</span><span class="d" id="d2" data-v="-">8</span><span class="d" id="d3" data-v="-">8</span><span class="colon">:</span><span class="d" id="d4" data-v="-">8</span><span class="d" id="d5" data-v="-">8</span>
    </div>
    <button class="primary" onclick="syncTime()">Synchroniser depuis le navigateur</button>
  </section>

  <section class="card">
    <h2>Couleurs d'affichage</h2>
    <div class="row">
      <label>Jour depuis</label>
      <input id="dayT" type="time" value="08:00">
    </div>
    <div class="row">
      <label>Couleur jour</label>
      <input id="dayC" type="color" value="#ff0000">
    </div>
    <div class="row">
      <label>Luminosit&#233; jour</label>
      <input id="dayBL" type="range" min="1" max="100" value="100"> <span id="dayBLv">100</span>%
    </div>
    <div class="row">
      <label>Nuit depuis</label>
      <input id="nightT" type="time" value="22:00">
    </div>
    <div class="row">
      <label>Couleur nuit</label>
      <input id="nightC" type="color" value="#0000ff">
    </div>
    <div class="row">
      <label>Luminosit&#233; nuit</label>
      <input id="nightBL" type="range" min="1" max="100" value="20"> <span id="nightBLv">20</span>%
    </div>
    <button class="primary" onclick="saveSchedule()">Enregistrer</button>
  </section>

  <footer>
    <a href="/wifi">Configuration Wi-Fi</a> &nbsp;|&nbsp;
    <a href="/update">Mise &#224; jour firmware (OTA)</a>
  </footer>
</main>

<script>
function updateClock() {
  const now = new Date();
  const h = now.getHours(), m = now.getMinutes(), s = now.getSeconds();
  document.getElementById('d0').dataset.v = h >= 10 ? String(Math.floor(h/10)) : '';
  document.getElementById('d0').style.visibility = h >= 10 ? 'visible' : 'hidden';
  document.getElementById('d1').dataset.v = String(h % 10);
  document.getElementById('d2').dataset.v = String(Math.floor(m/10));
  document.getElementById('d3').dataset.v = String(m % 10);
  document.getElementById('d4').dataset.v = String(Math.floor(s/10));
  document.getElementById('d5').dataset.v = String(s % 10);
  updateClockColor();
}
var _dayMin=480, _nightMin=1320, _dayCol='#ff0000', _nightCol='#0000ff';
function isNight() {
  const now = new Date();
  const cur = now.getHours()*60 + now.getMinutes();
  if (_nightMin > _dayMin) return (cur >= _nightMin || cur < _dayMin);
  else return (cur >= _nightMin && cur < _dayMin);
}
function updateClockColor() {
  const col = isNight() ? _nightCol : _dayCol;
  const dimCol = col + '26';
  document.querySelectorAll('.time .d').forEach(el => {
    el.style.setProperty('--fg', col);
    el.style.setProperty('--dim', dimCol);
  });
  document.querySelectorAll('.time .colon').forEach(el => { el.style.color = col; });
}
updateClock();
setInterval(updateClock, 1000);

function syncTime() {
  const epoch = Math.floor(Date.now()/1000);
  const tz = -new Date().getTimezoneOffset();
  fetch('/api/time?epoch='+epoch+'&tz='+tz).then(()=>{
    alert('Heure synchronis\u00e9e\u00a0!');
  });
}
function rgb565to24(v) {
  let r = ((v >> 11) & 0x1F) << 3; r |= r >> 5;
  let g = ((v >> 5) & 0x3F) << 2; g |= g >> 6;
  let b = (v & 0x1F) << 3; b |= b >> 5;
  return '#' + [r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');
}
function hex24to565(h) {
  const r = parseInt(h.slice(1,3),16);
  const g = parseInt(h.slice(3,5),16);
  const b = parseInt(h.slice(5,7),16);
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
function minToTime(m) {
  return String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
}
function timeToMin(t) {
  const p=t.split(':'); return parseInt(p[0])*60+parseInt(p[1]);
}
function saveSchedule() {
  const d = timeToMin(document.getElementById('dayT').value);
  const n = timeToMin(document.getElementById('nightT').value);
  const dc = hex24to565(document.getElementById('dayC').value);
  const nc = hex24to565(document.getElementById('nightC').value);
  const dbl = document.getElementById('dayBL').value;
  const nbl = document.getElementById('nightBL').value;
  fetch('/api/schedule?day='+d+'&night='+n+'&dayc='+dc+'&nightc='+nc+'&daybl='+dbl+'&nightbl='+nbl).then(()=>{
    alert('Enregistr\u00e9\u00a0!');
  });
}
function loadSchedule() {
  fetch('/api/schedule').then(r=>r.json()).then(s=>{
    document.getElementById('dayT').value = minToTime(s.day);
    document.getElementById('nightT').value = minToTime(s.night);
    document.getElementById('dayC').value = rgb565to24(s.dayc);
    document.getElementById('nightC').value = rgb565to24(s.nightc);
    document.getElementById('dayBL').value = s.daybl;
    document.getElementById('dayBLv').textContent = s.daybl;
    document.getElementById('nightBL').value = s.nightbl;
    document.getElementById('nightBLv').textContent = s.nightbl;
    _dayMin = s.day; _nightMin = s.night;
    _dayCol = rgb565to24(s.dayc); _nightCol = rgb565to24(s.nightc);
    updateClockColor();
  }).catch(()=>{});
}
document.getElementById('dayBL').oninput = function(){ document.getElementById('dayBLv').textContent=this.value; };
document.getElementById('nightBL').oninput = function(){ document.getElementById('nightBLv').textContent=this.value; };
loadSchedule();
</script>
</body>
</html>
)HTML";

// ---------------- Wi-Fi config page (captive portal) -------------------------
static const char WIFI_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configuration Wi-Fi</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font-family: system-ui, sans-serif; background:#0e1726; color:#e6edf3; }
  header { background:#1f6feb; padding:14px 18px; font-size:20px; font-weight:600; }
  main { max-width: 520px; margin:0 auto; padding:16px; }
  .card { background:#161f33; border:1px solid #243049; border-radius:10px;
          padding:16px; margin-bottom:14px; }
  h2 { margin:0 0 12px 0; font-size:16px; color:#9fb3d1;
       text-transform:uppercase; letter-spacing:1px; }
  input, select, button { width:100%; padding:10px; border-radius:6px;
        border:1px solid #2c3a5a; background:#0e1726; color:#e6edf3;
        font-size:15px; box-sizing:border-box; margin:6px 0; }
  button { background:#1f6feb; border-color:#1f6feb; color:white;
           cursor:pointer; font-weight:600; }
  button.secondary { background:#3d2230; border-color:#6b2230; }
  ul { list-style:none; padding:0; margin:0; }
  li { padding:8px 10px; border-radius:6px; cursor:pointer;
       display:flex; justify-content:space-between; align-items:center;
       border:1px solid #243049; margin:4px 0; }
  li:hover { background:#1c2640; }
  li.lock::after { content:"\01F512"; }
  small { color:#9fb3d1; }
  a { color:#7ee787; }
  .ok  { color:#7ee787; }
  .err { color:#f08a8a; }
</style>
</head>
<body>
<header>Configuration Wi-Fi</header>
<main>

  <section class="card">
    <h2>Actuel</h2>
    <div id="cur"><small>chargement...</small></div>
  </section>

  <section class="card">
    <h2>R&#233;seaux disponibles</h2>
    <button onclick="scan()">Actualiser</button>
    <ul id="list"><li><small>recherche...</small></li></ul>
  </section>

  <section class="card">
    <h2>Connexion</h2>
    <input id="ssid"  type="text"     placeholder="SSID" autocomplete="off">
    <input id="pass"  type="password" placeholder="Mot de passe (vide si ouvert)">
    <button onclick="save()">Enregistrer et connecter</button>
    <div id="msg"></div>
  </section>

  <section class="card">
    <h2>Nom d'h&#244;te</h2>
    <input id="hname" type="text" placeholder="horloge" maxlength="32" autocomplete="off">
    <button onclick="saveHostname()">Enregistrer le nom</button>
    <div id="hmsg"></div>
  </section>

  <section class="card">
    <h2>R&#233;initialisation</h2>
    <button class="secondary" onclick="reset()">Oublier le Wi-Fi et red&#233;marrer en AP</button>
  </section>

  <p><a href="/">&laquo; retour</a></p>
</main>

<script>
async function refresh() {
  try {
    const r = await fetch('/api/wifi/status');
    const s = await r.json();
    let html = `<div>Mode : <b>${s.mode}</b></div>`;
    if (s.mode === 'STA') {
      html += `<div>SSID\u00a0: <b>${s.ssid||''}</b></div>`;
      html += `<div>IP\u00a0: <b>${s.ip||''}</b></div>`;
      html += `<div>RSSI\u00a0: <b>${s.rssi} dBm</b></div>`;
    } else {
      html += `<div>AP SSID\u00a0: <b>${s.ap_ssid}</b></div>`;
      html += `<div>AP IP\u00a0: <b>${s.ap_ip}</b></div>`;
      if (s.saved_ssid) html += `<div>SSID STA enregistr\u00e9\u00a0: <b>${s.saved_ssid}</b></div>`;
    }
    document.getElementById('cur').innerHTML = html;
  } catch(e) {}
}

async function scan() {
  document.getElementById('list').innerHTML = '<li><small>recherche...</small></li>';
  try {
    const r = await fetch('/api/wifi/scan');
    const nets = await r.json();
    const ul = document.getElementById('list');
    ul.innerHTML = '';
    if (!nets.length) { ul.innerHTML = '<li><small>aucun r\u00e9seau trouv\u00e9</small></li>'; return; }
    nets.forEach(n => {
      const li = document.createElement('li');
      if (n.enc) li.classList.add('lock');
      li.innerHTML = `<span><b>${n.ssid||'(hidden)'}</b> <small>ch ${n.ch}</small></span>
                      <small>${n.rssi} dBm</small>`;
      li.onclick = () => {
        document.getElementById('ssid').value = n.ssid;
        document.getElementById('pass').focus();
      };
      ul.appendChild(li);
    });
  } catch(e) {
    document.getElementById('list').innerHTML = '<li><small class="err">\u00e9chec du scan</small></li>';
  }
}

async function save() {
  const ssid = document.getElementById('ssid').value.trim();
  const pass = document.getElementById('pass').value;
  const msg  = document.getElementById('msg');
  if (!ssid) { msg.innerHTML = '<span class="err">SSID requis</span>'; return; }
  msg.innerHTML = '<small>enregistrement et red\u00e9marrage en mode STA...</small>';
  const fd = new URLSearchParams();
  fd.append('ssid', ssid);
  fd.append('pass', pass);
  await fetch('/api/wifi/save', { method:'POST', body: fd });
  msg.innerHTML = '<span class="ok">Enregistr\u00e9. Red\u00e9marrage en cours. Reconnectez-vous \u00e0 votre Wi-Fi pour le retrouver.</span>';
}

async function reset() {
  if (!confirm('Oublier le Wi-Fi enregistr\u00e9 et red\u00e9marrer en mode AP\u00a0?')) return;
  await fetch('/api/wifi/reset', { method:'POST' });
  document.getElementById('msg').innerHTML = '<span class="ok">R\u00e9initialis\u00e9. Red\u00e9marrage en AP...</span>';
}

refresh();
scan();
fetch('/api/wifi/hostname').then(r=>r.json()).then(d=>{
  document.getElementById('hname').value = d.hostname||'';
}).catch(()=>{});
async function saveHostname() {
  const h = document.getElementById('hname').value.trim();
  if (!h) { document.getElementById('hmsg').innerHTML='<span class="err">Nom requis</span>'; return; }
  const fd = new URLSearchParams(); fd.append('hostname', h);
  const r = await fetch('/api/wifi/hostname', { method:'POST', body: fd });
  if (r.ok) document.getElementById('hmsg').innerHTML='<span class="ok">Enregistr\u00e9 (actif au red\u00e9marrage)</span>';
  else document.getElementById('hmsg').innerHTML='<span class="err">Erreur</span>';
}
</script>
</body>
</html>
)HTML";

// ---------------- Debug page -------------------------------------------------
static const char DEBUG_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Horloge - Debug</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: system-ui, -apple-system, sans-serif;
         background: #0e1726; color: #e6edf3; }
  header { background: #1f6feb; padding: 14px 18px; font-size: 20px; font-weight: 600; }
  header a { color: white; text-decoration: none; margin-left: 16px; font-size: 14px; opacity: 0.8; }
  main { max-width: 520px; margin: 0 auto; padding: 16px; }
  .card { background: #161f33; border: 1px solid #243049; border-radius: 10px;
          padding: 16px; margin-bottom: 14px; }
  h2 { margin: 0 0 12px; font-size: 16px; color: #9fb3d1; text-transform: uppercase; letter-spacing: 1px; }

  /* TFT screen simulation */
  .tft { background: #000; border: 3px solid #333; border-radius: 6px;
         width: 100%; aspect-ratio: 280/240; position: relative;
         font-family: monospace; overflow: hidden; padding: 8px; }
  .tft .title { color: #00ffff; font-size: 16px; font-weight: bold; margin-bottom: 6px; }
  .tft .item { padding: 3px 6px; margin: 1px 0; border-radius: 3px; font-size: 13px; }
  .tft .item.sel { background: #1a1a3a; }
  .tft .item .lbl { color: #aaa; font-size: 10px; display: block; }
  .tft .item.sel .lbl { color: #fff; }
  .tft .item .val { font-size: 14px; color: #ffcc00; font-weight: bold;
                    font-family: monospace; display: flex; align-items: center; }
  .tft .item.edit .val { color: #0f0; }
  .tft .item .arrow { color: #fff; margin-left: 8px; }
  .tft .swatch { display: inline-block; width: 14px; height: 14px; border: 1px solid #fff;
                 vertical-align: middle; margin-left: 6px; flex-shrink: 0; }
  .tft .hint { position: absolute; bottom: 6px; left: 8px; right: 8px;
               color: #888; font-size: 10px; }
  .tft .inactive { color: #555; font-size: 18px; text-align: center;
                   position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); }

  /* Button pad */
  .pad { display: grid; grid-template-columns: 1fr 1fr 1fr 1fr 1fr 1fr;
         grid-template-rows: auto auto auto; gap: 8px; }
  .btn { padding: 14px 0; border-radius: 10px; border: 2px solid #2c3a5a;
         background: #1a2540; color: #e6edf3; font-size: 16px; font-weight: 700;
         cursor: pointer; text-align: center; user-select: none;
         transition: background 0.1s; }
  .btn:active, .btn.pressed { background: #1f6feb; border-color: #1f6feb; }
  .btn-up    { grid-column: 2; grid-row: 1; }
  .btn-down  { grid-column: 2; grid-row: 3; }
  .btn-left  { grid-column: 1; grid-row: 2; }
  .btn-right { grid-column: 3; grid-row: 2; }
  .btn-a     { grid-column: 5; grid-row: 1; background: #0d3b1e; border-color: #1a6b35; }
  .btn-b     { grid-column: 6; grid-row: 1; background: #3b0d0d; border-color: #6b1a1a; }
  .btn-a:active { background: #1a6b35; }
  .btn-b:active { background: #6b1a1a; }
  .label-dpad { grid-column: 1 / 4; grid-row: 2; display: flex; align-items: center;
                justify-content: center; pointer-events: none; }
  .status { font-size: 12px; color: #666; text-align: center; margin-top: 8px; }
</style>
</head>
<body>
<header>Horloge - Debug <a href="/">Accueil</a> <a href="/wifi">WiFi</a> <a href="/update">OTA</a></header>
<main>

<section class="card">
  <h2>Ecran TFT (280x240)</h2>
  <div class="tft" id="tft"></div>
</section>

<section class="card">
  <h2>Boutons</h2>
  <div class="pad" id="pad">
    <div class="btn btn-up"    data-btn="up">&#9650; UP</div>
    <div class="btn btn-left"  data-btn="left">&#9664; L</div>
    <div class="btn btn-right" data-btn="right">R &#9654;</div>
    <div class="btn btn-down"  data-btn="down">&#9660; DN</div>
    <div class="btn btn-a"     data-btn="a">A</div>
    <div class="btn btn-b"     data-btn="b">B</div>
  </div>
  <div class="status" id="status">Pret</div>
</section>

</main>
<script>
let polling = null;

function renderTft(st) {
  const el = document.getElementById('tft');
  if (!st.active) {
    el.innerHTML = '<div class="inactive">Menu inactif<br><small>Appuyez sur A pour ouvrir</small></div>';
    return;
  }
  let html = '<div class="title">' + esc(st.title) + '</div>';
  if (st.level === 1) html += '<hr style="border-color:#333;margin:3px 0">';
  for (const it of st.items) {
    let cls = 'item';
    if (it.sel) cls += ' sel';
    if (it.edit) cls += ' edit';
    html += '<div class="' + cls + '">';
    if (it.value !== undefined && it.value !== '') {
      html += '<span class="lbl">' + esc(it.label) + '</span>';
      html += '<span class="val">' + esc(it.value);
      if (it.color) html += '<span class="swatch" style="background:' + it.color + '"></span>';
      html += '</span>';
    } else {
      html += '<span class="val" style="font-size:13px">' + esc(it.label);
      if (it.sel) html += ' <span class="arrow">&gt;</span>';
      html += '</span>';
    }
    html += '</div>';
  }
  html += '<div class="hint">' + esc(st.hint || '') + '</div>';
  el.innerHTML = html;
}

function esc(s) {
  const d = document.createElement('div');
  d.textContent = s;
  return d.innerHTML;
}

async function refresh() {
  try {
    const r = await fetch('/api/debug/state');
    const st = await r.json();
    renderTft(st);
  } catch(e) {}
}

let lastPress = 0;
const COOLDOWN = 300;

async function press(btn) {
  const now = Date.now();
  if (now - lastPress < COOLDOWN) return;
  lastPress = now;
  document.getElementById('status').textContent = 'Envoi: ' + btn.toUpperCase();
  try {
    const fd = new URLSearchParams();
    fd.append('btn', btn);
    await fetch('/api/debug/press', { method: 'POST', body: fd });
    setTimeout(refresh, 50);
  } catch(e) {
    document.getElementById('status').textContent = 'Erreur';
  }
}

// Button pad events (prevent touch+mouse double fire)
const pad = document.getElementById('pad');
pad.addEventListener('touchstart', function(e) {
  const b = e.target.closest('[data-btn]');
  if (b) { e.preventDefault(); press(b.dataset.btn); }
}, {passive:false});
pad.addEventListener('mousedown', function(e) {
  const b = e.target.closest('[data-btn]');
  if (b) press(b.dataset.btn);
});

// Keyboard shortcuts
document.addEventListener('keydown', function(e) {
  const map = { ArrowUp:'up', ArrowDown:'down', ArrowLeft:'left', ArrowRight:'right',
                a:'a', A:'a', Enter:'a', b:'b', B:'b', Escape:'b', Backspace:'b' };
  const btn = map[e.key];
  if (btn) { e.preventDefault(); press(btn); }
});

// Auto-refresh
refresh();
polling = setInterval(refresh, 500);
</script>
</body>
</html>
)HTML";

// ---------------- OTA upload page --------------------------------------------
static const char OTA_HTML[] PROGMEM = R"HTML(
<!doctype html><meta charset="utf-8"><title>Mise &#224; jour OTA</title>
<style>body{font-family:system-ui;background:#0e1726;color:#e6edf3;padding:24px}
input,button{padding:10px;font-size:15px}
button{background:#1f6feb;color:white;border:0;border-radius:6px;cursor:pointer}
.bar{height:14px;background:#243049;border-radius:7px;overflow:hidden;margin-top:14px}
.bar>div{height:100%;background:#7ee787;width:0%}</style>
<h2>Mise &#224; jour firmware</h2>
<p>S&#233;lectionnez un fichier <code>.bin</code> compil&#233; et envoyez-le.</p>
<form id="f" enctype="multipart/form-data" method="POST" action="/update">
  <label style="display:inline-block;padding:10px;background:#1f6feb;color:white;border-radius:6px;cursor:pointer;font-size:15px;margin-bottom:8px">Choisir un fichier<input type="file" name="firmware" accept=".bin" required style="display:none" onchange="document.getElementById('fn').textContent=this.files[0]?.name||'Aucun fichier'"></label>
  <span id="fn" style="margin-left:8px;color:#9fb3d1">Aucun fichier</span><br>
  <button type="submit">Envoyer</button>
</form>
<div class="bar"><div id="p"></div></div>
<pre id="log"></pre>
<p><a style="color:#7ee787" href="/">&laquo; retour</a></p>
<script>
const f=document.getElementById('f'),p=document.getElementById('p'),l=document.getElementById('log');
f.addEventListener('submit',e=>{
  e.preventDefault();
  const fd=new FormData(f);
  const x=new XMLHttpRequest();
  x.upload.onprogress=ev=>{ if(ev.lengthComputable){
    p.style.width=(ev.loaded/ev.total*100).toFixed(1)+'%'; }};
  x.onload=()=>{ l.textContent=x.status+' '+x.responseText;
    if(x.status===200){ l.textContent+='\nRed\u00e9marrage en cours...'; }};
  x.onerror=()=>{ l.textContent='Erreur d\'envoi'; };
  x.open('POST','/update'); x.send(fd);
});
</script>
)HTML";

// ---------------- Handlers ----------------------------------------------------
static void hRoot()    { s_server.send_P(200, "text/html", INDEX_HTML); }
static void hOtaPage() { s_server.send_P(200, "text/html", OTA_HTML); }

static void hFont() {
  s_server.sendHeader("Cache-Control", "public, max-age=86400");
  s_server.send_P(200, "font/ttf", (const char*)FONT_TTF, FONT_TTF_LEN);
}

static void hTime() {
  if (s_server.hasArg("epoch")) {
    time_t epoch = (time_t)s_server.arg("epoch").toInt();
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
  }
  if (s_server.hasArg("tz")) {
    int tz = s_server.arg("tz").toInt();
    s_prefs.putInt("tz_off", tz);
    configTime(tz * 60L, 0, "pool.ntp.org", "time.nist.gov");
  }
  s_server.send(200, "application/json", "{\"ok\":true}");
}

static void hSchedule() {
  if (s_server.hasArg("day") && s_server.hasArg("night")) {
    int d = s_server.arg("day").toInt();
    int n = s_server.arg("night").toInt();
    if (d < 0) d = 0; if (d > 1439) d = 1439;
    if (n < 0) n = 0; if (n > 1439) n = 1439;
    s_prefs.putUShort("day_m", (uint16_t)d);
    s_prefs.putUShort("night_m", (uint16_t)n);
    display_setSchedule((uint16_t)d, (uint16_t)n);
  }
  if (s_server.hasArg("dayc") && s_server.hasArg("nightc")) {
    uint16_t dc = (uint16_t)s_server.arg("dayc").toInt();
    uint16_t nc = (uint16_t)s_server.arg("nightc").toInt();
    s_prefs.putUShort("day_c", dc);
    s_prefs.putUShort("night_c", nc);
    display_setColors(dc, nc);
  }
  if (s_server.hasArg("daybl") && s_server.hasArg("nightbl")) {
    int dbl = s_server.arg("daybl").toInt();
    int nbl = s_server.arg("nightbl").toInt();
    if (dbl < 1) dbl = 1; if (dbl > 100) dbl = 100;
    if (nbl < 1) nbl = 1; if (nbl > 100) nbl = 100;
    s_prefs.putUChar("day_bl", (uint8_t)dbl);
    s_prefs.putUChar("night_bl", (uint8_t)nbl);
    display_setBacklight((uint8_t)dbl, (uint8_t)nbl);
  }
  uint16_t d = s_prefs.getUShort("day_m", DEFAULT_DAY_MIN);
  uint16_t n = s_prefs.getUShort("night_m", DEFAULT_NIGHT_MIN);
  uint16_t dc = s_prefs.getUShort("day_c", DEFAULT_DAY_FG);
  uint16_t nc = s_prefs.getUShort("night_c", DEFAULT_NIGHT_FG);
  uint8_t dbl = s_prefs.getUChar("day_bl", DEFAULT_DAY_BL);
  uint8_t nbl = s_prefs.getUChar("night_bl", DEFAULT_NIGHT_BL);
  String j = "{\"day\":" + String(d) + ",\"night\":" + String(n)
           + ",\"dayc\":" + String(dc) + ",\"nightc\":" + String(nc)
           + ",\"daybl\":" + String(dbl) + ",\"nightbl\":" + String(nbl) + "}";
  s_server.send(200, "application/json", j);
}

// ---------------- Wi-Fi config / captive portal ------------------------------
static void hWifiPage() { s_server.send_P(200, "text/html", WIFI_HTML); }

static void hWifiStatus() {
  String j = "{";
  if (s_apMode) {
    j += "\"mode\":\"AP\",";
    j += "\"ap_ssid\":\"" + String(WIFI_AP_SSID) + "\",";
    j += "\"ap_ip\":\""   + WiFi.softAPIP().toString() + "\",";
    String saved = s_prefs.getString("ssid", "");
    j += "\"saved_ssid\":\"" + saved + "\"";
  } else {
    j += "\"mode\":\"STA\",";
    j += "\"ssid\":\"" + WiFi.SSID() + "\",";
    j += "\"ip\":\""   + WiFi.localIP().toString() + "\",";
    j += "\"rssi\":"   + String(WiFi.RSSI());
  }
  j += "}";
  s_server.send(200, "application/json", j);
}

static String jsonEscape(const String& s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if      (c == '"')  o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c < 0x20)  { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); o += b; }
    else                o += c;
  }
  return o;
}

static void hWifiScan() {
  int n = WiFi.scanNetworks(false, false);  // sync, skip hidden
  String j = "[";
  bool first = true;
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i).isEmpty()) continue;   // skip hidden networks
    if (!first) j += ",";
    first = false;
    j += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    j += "\"rssi\":"    + String(WiFi.RSSI(i)) + ",";
    j += "\"ch\":"      + String(WiFi.channel(i)) + ",";
    j += "\"enc\":"     + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
    j += "}";
  }
  j += "]";
  WiFi.scanDelete();
  s_server.send(200, "application/json", j);
}

static void hWifiSave() {
  if (!s_server.hasArg("ssid")) { s_server.send(400, "text/plain", "missing ssid"); return; }
  String ssid = s_server.arg("ssid");
  String pass = s_server.hasArg("pass") ? s_server.arg("pass") : String();
  s_prefs.putString("ssid", ssid);
  s_prefs.putString("pass", pass);
  s_server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

static void hWifiReset() {
  s_prefs.remove("ssid");
  s_prefs.remove("pass");
  s_server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

static void hWifiHostname() {
  if (s_server.method() == HTTP_GET) {
    String h = s_prefs.getString("hostname", "horloge");
    s_server.send(200, "application/json", "{\"hostname\":\"" + h + "\"}");
  } else {
    if (!s_server.hasArg("hostname")) { s_server.send(400, "text/plain", "missing hostname"); return; }
    String h = s_server.arg("hostname");
    h.trim();
    if (h.length() == 0 || h.length() > 32) { s_server.send(400, "text/plain", "invalid hostname"); return; }
    s_prefs.putString("hostname", h);
    WiFi.setHostname(h.c_str());
    s_server.send(200, "application/json", "{\"ok\":true}");
  }
}

// ---------------- Debug handlers ---------------------------------------------
static void hDebugPage() {
  s_server.send_P(200, "text/html", DEBUG_HTML);
}

static void hDebugState() {
  String json;
  menu_getStateJson(json);
  s_server.send(200, "application/json", json);
}

static void hDebugPress() {
  if (!s_server.hasArg("btn")) { s_server.send(400, "text/plain", "missing btn"); return; }
  String b = s_server.arg("btn");
  Button btn = BTN_NONE;
  if      (b == "up")    btn = BTN_UP;
  else if (b == "down")  btn = BTN_DOWN;
  else if (b == "left")  btn = BTN_LEFT;
  else if (b == "right") btn = BTN_RIGHT;
  else if (b == "a")     btn = BTN_A;
  else if (b == "b")     btn = BTN_B;
  if (btn != BTN_NONE) {
    menu_handleButton(btn);
    if (menu_isActive()) menu_draw();
  }
  s_server.send(200, "application/json", "{\"ok\":true}");
}

// Captive-portal: redirect every unknown URL to the root so phones/laptops
// auto-pop the Wi-Fi setup page.
static void hCaptive() {
  if (s_apMode) {
    s_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifi", true);
    s_server.send(302, "text/plain", "");
  } else {
    s_server.send(404, "text/plain", "not found");
  }
}

// ---------------- OTA via Update.h -------------------------------------------
static void hOtaResult() {
  bool ok = !Update.hasError();
  s_server.sendHeader("Connection", "close");
  s_server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
  if (ok) {
    delay(500);
    ESP.restart();
  }
}
static void hOtaUpload() {
  HTTPUpload& up = s_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[OTA] done: %u bytes\n", up.totalSize);
    else                  Update.printError(Serial);
  }
}

// ---------------- Setup -------------------------------------------------------
static bool tryStation(const String& ssid, const String& pass) {
  Serial.printf("[WiFi] STA -> %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : (const char*)nullptr);
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("[WiFi] STA IP: "));
      Serial.println(WiFi.localIP());
      return true;
    }
    delay(250);
  }
  Serial.println(F("[WiFi] STA connect failed"));
  WiFi.disconnect(true, true);
  return false;
}

static void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHAN);
  Serial.print(F("[WiFi] AP IP: "));
  Serial.println(WiFi.softAPIP());
  // DNS server -> redirect every hostname to ourselves (captive portal)
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());
  s_apMode = true;
}

void webui_begin() {
  s_prefs.begin("wifi", false);

  String ssid = s_prefs.getString("ssid", "");
  String pass = s_prefs.getString("pass", "");

  // Apply hostname before WiFi starts
  String hostname = s_prefs.getString("hostname", "horloge");
  WiFi.setHostname(hostname.c_str());

  bool sta = false;
  if (ssid.length()) sta = tryStation(ssid, pass);
  if (!sta)          startAccessPoint();
  else               s_apMode = false;

  // Configure timezone + NTP
  int tz = s_prefs.getInt("tz_off", 0);   // saved tz offset in minutes
  configTime(tz * 60L, 0, "pool.ntp.org", "time.nist.gov");

  // Restore saved schedule + colors
  uint16_t dayM   = s_prefs.getUShort("day_m", DEFAULT_DAY_MIN);
  uint16_t nightM = s_prefs.getUShort("night_m", DEFAULT_NIGHT_MIN);
  display_setSchedule(dayM, nightM);
  uint16_t dayC   = s_prefs.getUShort("day_c", DEFAULT_DAY_FG);
  uint16_t nightC = s_prefs.getUShort("night_c", DEFAULT_NIGHT_FG);
  display_setColors(dayC, nightC);
  uint8_t dayBl   = s_prefs.getUChar("day_bl", DEFAULT_DAY_BL);
  uint8_t nightBl = s_prefs.getUChar("night_bl", DEFAULT_NIGHT_BL);
  display_setBacklight(dayBl, nightBl);
  bool icons = s_prefs.getBool("icons", true);
  display_setShowIcons(icons);

  // Routes
  s_server.on("/",            HTTP_GET,  hRoot);
  s_server.on("/font.ttf",    HTTP_GET,  hFont);
  s_server.on("/api/time",    HTTP_GET,  hTime);
  s_server.on("/api/schedule",HTTP_GET,  hSchedule);

  s_server.on("/wifi",            HTTP_GET,  hWifiPage);
  s_server.on("/api/wifi/status", HTTP_GET,  hWifiStatus);
  s_server.on("/api/wifi/scan",   HTTP_GET,  hWifiScan);
  s_server.on("/api/wifi/save",   HTTP_POST, hWifiSave);
  s_server.on("/api/wifi/reset",  HTTP_POST, hWifiReset);
  s_server.on("/api/wifi/hostname", HTTP_GET,  hWifiHostname);
  s_server.on("/api/wifi/hostname", HTTP_POST, hWifiHostname);

  s_server.on("/debug",             HTTP_GET,  hDebugPage);
  s_server.on("/api/debug/state",   HTTP_GET,  hDebugState);
  s_server.on("/api/debug/press",   HTTP_POST, hDebugPress);

  // Captive-portal probe URLs (Apple, Microsoft, Android, etc.)
  s_server.on("/generate_204",      HTTP_GET, hCaptive);
  s_server.on("/gen_204",           HTTP_GET, hCaptive);
  s_server.on("/hotspot-detect.html", HTTP_GET, hCaptive);
  s_server.on("/connecttest.txt",   HTTP_GET, hCaptive);
  s_server.on("/ncsi.txt",          HTTP_GET, hCaptive);
  s_server.on("/redirect",          HTTP_GET, hCaptive);
  s_server.on("/success.txt",       HTTP_GET, hCaptive);

  s_server.on("/update",      HTTP_GET,  hOtaPage);
  s_server.on("/update",      HTTP_POST, hOtaResult, hOtaUpload);

  s_server.onNotFound([]() {
    if (s_apMode) hCaptive();
    else          s_server.send(404, "text/plain", "not found");
  });
  s_server.begin();
  Serial.println(F("[HTTP] server started"));
}

void webui_loop() {
  if (s_apMode) s_dns.processNextRequest();
  s_server.handleClient();
}
