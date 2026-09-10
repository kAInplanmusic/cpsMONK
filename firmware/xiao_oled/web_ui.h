// Generated from web/index.html by web/generate_header.py. Do not edit.
#pragma once
#include <pgmspace.h>

static const char WEB_UI[] PROGMEM = R"cpsMONK_HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#111723">
<title>cpsMONK · Akustische Messung</title>
<style>
:root{color-scheme:dark;--bg:#0b1019;--panel:#141d2b;--line:#2d3c51;--text:#edf3fc;--muted:#aebfd4;--accent:#64e1be;--warn:#ffd489;--bad:#ff9ba4}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.55 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}main{max-width:1120px;margin:auto;padding:24px 18px 48px}header{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:24px}h1{margin:0;font-size:1.9rem;letter-spacing:-.06em}h1 span{color:var(--accent)}h2{font-size:1.16rem;margin:0 0 12px}h3{font-size:1rem;margin:0 0 4px}p{margin:8px 0}.muted,small{color:var(--muted)}.connection{font-size:.85rem;border:1px solid var(--line);border-radius:100px;padding:5px 12px;text-align:center}.online{color:var(--accent)}.offline{color:var(--bad)}.panel{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:20px;margin:16px 0}.notice{border-left:4px solid var(--warn);background:#242334;padding:14px 18px;border-radius:8px}.notice strong{color:var(--warn)}.steps{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:18px 0}.step{padding:12px;border:1px solid var(--line);border-radius:10px}.step b{color:var(--accent)}.status-row,.actions,.legend,.selection{display:flex;flex-wrap:wrap;align-items:center;gap:12px}.status-row{justify-content:space-between}#state{font-weight:700;color:var(--accent);font-size:1.2rem}progress{display:block;width:100%;height:16px;accent-color:var(--accent);margin:14px 0}.actions{margin-top:16px}button,.download{font:inherit;border:1px solid var(--line);border-radius:9px;background:#26364e;color:var(--text);padding:10px 16px;cursor:pointer;text-decoration:none;display:inline-block}button.primary{background:var(--accent);color:#092b23;font-weight:750;border-color:var(--accent)}button:hover:not(:disabled),.download:hover{filter:brightness(1.15)}button:disabled{opacity:.45;cursor:not-allowed}button:focus-visible,a:focus-visible,input:focus-visible{outline:3px solid var(--accent);outline-offset:3px}#error{color:var(--bad);white-space:pre-wrap;overflow-wrap:anywhere}.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:16px 0}.metric{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px}.value{font-size:2rem;font-weight:730;font-variant-numeric:tabular-nums;line-height:1.35}.unit{font-size:.9rem;color:var(--muted)}.metric small{display:block;font-size:.79rem}ul{padding-left:22px}.warnings{color:var(--warn)}.chart-wrap{position:relative;height:330px;margin-top:15px}canvas{display:block;width:100%;height:100%;border:1px solid var(--line);border-radius:10px;background:#0b1422}.legend{font-size:.85rem;margin-top:12px}.legend span::before{content:"";display:inline-block;width:18px;height:3px;background:var(--accent);vertical-align:middle;margin-right:7px}.legend .all::before{background:#6b91c6}.legend .mean::before{background:#ffcc72}.legend .selected::before{background:#70f7d2}.selection{margin-top:18px}.selection input{flex:1;min-width:140px;accent-color:var(--accent)}.selection output{min-width:72px;font-variant-numeric:tabular-nums}.config-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}label small{display:block;margin-top:5px}input[type=number]{display:block;width:100%;margin-top:6px;border:1px solid var(--line);border-radius:8px;background:var(--bg);padding:10px;color:var(--text);font:inherit}fieldset{border:0;margin:0;padding:0}details summary{cursor:pointer;font-weight:650}details[open] summary{margin-bottom:16px}.submetrics{display:flex;flex-wrap:wrap;gap:10px 22px;color:var(--muted);font-size:.9rem}.submetrics b{color:var(--text)}[hidden]{display:none!important}footer{font-size:.84rem;color:var(--muted);margin-top:24px}#chartMessage{margin-top:12px;color:var(--muted)}@media(max-width:760px){.metrics{grid-template-columns:repeat(2,1fr)}.config-grid{grid-template-columns:repeat(2,1fr)}.steps{grid-template-columns:1fr;gap:8px}.step{padding:9px 12px}.chart-wrap{height:270px}header{align-items:flex-start}.panel{padding:16px}}@media(max-width:390px){main{padding:16px 12px}.value{font-size:1.6rem}.metric{padding:12px}.config-grid{grid-template-columns:1fr}.actions button{width:100%}}
</style>
</head>
<body>
<main>
<header><div><h1>cps<span>MONK</span></h1><div class="muted">Akustische Impulsanalyse · XIAO ESP32</div></div><div id="connection" class="connection" role="status">Verbinde …</div></header>
<div class="notice"><strong>Vor dem Start: Motor AUS.</strong> Nach „Messung starten“ wird etwa 1 Sekunde lang die Umgebungsruhe kalibriert. Motor erst einschalten, wenn unten ausdrücklich <strong>„Aufnahme läuft — Motor jetzt einschalten“</strong> erscheint. Während der Aufnahme Mikrofon, Abstand und Aufbau nicht verändern.</div>
<div class="steps"><div class="step"><b>1 · Ruhe messen</b><br>Motor aus, Umgebung still.</div><div class="step"><b>2 · 500 Impulse aufnehmen</b><br>Erst bei Aufnahmebeginn Motor an.</div><div class="step"><b>3 · Formen vergleichen</b><br>Überlagerung und Rohdaten prüfen.</div></div>
<section class="panel" aria-labelledby="measurementHeading">
<h2 id="measurementHeading">Messung</h2>
<div class="status-row"><div id="state" role="status" aria-live="polite">Gerätestatus wird geladen …</div><strong id="count">0 / 500 Impulse</strong></div>
<p id="stateHelp" class="muted">Mit dem Geräte-WLAN verbinden und http://192.168.4.1 öffnen.</p>
<progress id="progress" value="0" max="500" aria-label="Erkannte Impulse"></progress>
<div class="actions"><button id="start" class="primary" disabled>Messung starten</button><button id="reset" disabled>Zurücksetzen</button><a id="jsonDownload" class="download" href="/api/result" download="cpsmonk-result.json" hidden>JSON herunterladen</a><a id="wavDownload" class="download" href="/api/raw.wav" download="cpsmonk-raw.wav" hidden>Rohaufnahme WAV</a><a id="csvDownload" class="download" href="/api/impacts.csv" download="cpsmonk-impacts.csv" hidden>CSV herunterladen</a></div>
<p id="error" role="alert" hidden></p>
</section>
<section class="metrics" aria-label="Messwerte">
<div class="metric"><h3>Frequenz</h3><div><span id="cps" class="value">—</span> <span class="unit">CPS / Hz</span></div><small>Erkannte Impulse pro Sekunde</small></div>
<div class="metric"><h3>Formähnlichkeit</h3><div><span id="shape" class="value">—</span> <span class="unit">%</span></div><small>Akustische Form, nicht Kraft</small></div>
<div class="metric"><h3>Timing-Streuung</h3><div><span id="periodCv" class="value">—</span> <span class="unit">% CV</span></div><small>Variation der Impulsabstände</small></div>
<div class="metric"><h3>Amplituden-Streuung</h3><div><span id="amplitudeCv" class="value">—</span> <span class="unit">% CV</span></div><small>Variation der akustischen Spitzen</small></div>
</section>
<section class="panel" aria-labelledby="qualityHeading"><h2 id="qualityHeading">Signal & Erkennung</h2><div class="submetrics"><span>Übersteuerung: <b id="clipped">—</b> %</span><span>Rauschpegel RMS: <b id="noise">—</b></span><span>Erkennungsschwelle: <b id="threshold">—</b></span><span>Verworfene Kandidaten: <b id="rejected">—</b></span></div><ul id="warnings" class="warnings" hidden></ul><p class="muted">Doppel- oder fehlende Erkennungen können CPS und Streuung verfälschen. Timing-Warnungen und verworfene Kandidaten sind Hinweise, kein sicherer Nachweis. Übersteuerung verändert die Wellenform: Mikrofonabstand oder Pegel anpassen und neu messen.</p></section>
<section class="panel" aria-labelledby="waveHeading"><h2 id="waveHeading">Wellenformen · Überlagerung</h2><p class="muted">Alle erkannten Impulse im festen Zeitfenster, mit Mittelwert als Referenz. Zur Formansicht werden Gleichanteil entfernt und jede Kurve auf ihre eigene Spitzenamplitude normiert. Das Messfenster ist um den Impuls zentriert; die Zeitachse wird nicht auf eine gemeinsame Phase oder Periodenlänge gestreckt.</p>
<div class="chart-wrap"><canvas id="waveCanvas" role="img" aria-label="Akustische Wellenformen: alle Impulse blau, Mittelwert gelb, ausgewählter Impuls mintgrün">Die Wellenformdarstellung benötigt einen Browser mit Canvas-Unterstützung.</canvas></div>
<div class="legend"><span class="all">Alle Impulse</span><span class="mean">Mittelwert-Referenz</span><span class="selected">Ausgewählter Impuls</span></div>
<div class="selection"><label for="selected">Impuls wählen</label><input id="selected" type="range" min="1" max="500" value="1" step="1" disabled><output id="selectedNumber" for="selected">1 / 500</output></div><p id="selectedDetails" class="muted">Einzelwerte stehen nach der vollständigen Aufnahme bereit.</p><p id="chartMessage" role="status">Wellenformen werden erst nach Abschluss geladen.</p><button id="retryWaves" hidden>Wellenformen erneut laden</button>
</section>
<section class="panel"><details><summary>Erkennung einstellen</summary><p class="muted">Nur im Ruhezustand oder nach Abschluss/Fehler änderbar. Grenzwerte passend zur erwarteten Impulsfrequenz wählen. Zu niedrige Schwellen begünstigen Doppelerkennungen, zu hohe Schwellen übersehen Impulse.</p><form id="configForm"><fieldset id="configFields" disabled><div class="config-grid"><label for="minCps">Minimum CPS<input id="minCps" name="minCps" type="number" min="20" max="199" step="any" value="20" required></label><label for="maxCps">Maximum CPS<input id="maxCps" name="maxCps" type="number" min="21" max="200" step="any" value="200" required></label><label for="thresholdMultiplier">Rauschfaktor<input id="thresholdMultiplier" name="thresholdMultiplier" type="number" min="3" max="30" step="any" value="6" required><small>Multiplikator des Rauschpegels</small></label><label for="thresholdFloor">Mindestschwelle<input id="thresholdFloor" name="thresholdFloor" type="number" min="0.00001" max="0.5" step="any" value="0.001" required><small>Normierte Audioamplitude</small></label></div><div class="actions"><button type="submit">Einstellungen speichern</button><span id="configMessage" role="status" class="muted"></span></div></fieldset></form></details></section>
<section class="notice"><strong>Was diese Messung aussagt — und was nicht</strong><p>Formähnlichkeit bewertet ausschließlich die akustische Wiederholbarkeit der erkannten Impulse. Sie misst weder mechanische Schlagkraft noch Nadeltiefe, Hautwirkung oder klinische bzw. Tattoo-Qualität. Timing-CV und Amplituden-CV sind getrennte Streuungsmaße (Standardabweichung relativ zum Mittelwert); kleinere Werte bedeuten weniger Streuung, nicht automatisch bessere Anwendungsergebnisse.</p></section>
<footer>Vollständig lokal · keine Cloud, keine externen Schriften oder Bibliotheken · Geräteadresse: <a href="http://192.168.4.1/">192.168.4.1</a><br>CPS und Formstatistik erscheinen erst nach 500 Impulsen. Nach Abschluss JSON/CSV sichern; Zurücksetzen bzw. Neustart kann die Messdaten verwerfen.</footer>
</main>
<script>
'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const knownStates = ['idle', 'calibrating', 'recording', 'complete', 'failed'];
  const configKeys = ['minCps', 'maxCps', 'thresholdMultiplier', 'thresholdFloor'];
  const labels = {idle:'Bereit — Motor ausgeschaltet lassen', calibrating:'Ruhekalibrierung — Motor AUS lassen!', recording:'Aufnahme läuft — Motor jetzt einschalten', complete:'Messung abgeschlossen — Motor kann aus', failed:'Messung fehlgeschlagen'};
  const helps = {idle:'Starten Sie mit ausgeschaltetem Motor. Zunächst wird etwa 1 Sekunde Umgebungsruhe gemessen.', calibrating:'Etwa 1 Sekunde still bleiben. Noch NICHT einschalten. Warten Sie auf „Aufnahme läuft“.', recording:'Jetzt den Motor einschalten und gleichmäßig laufen lassen, bis alle Impulse erfasst sind.', complete:'Ergebnis prüfen und herunterladen. Eine neue Messung ersetzt die bisherigen Daten.', failed:'Fehler unten prüfen. Motor ausschalten, Aufbau korrigieren und erneut starten.'};
  let status = null, connected = false, actionPending = false, configDirty = false;
  let waveData = null, normalized = [], mean = [], waveAttempted = false, waveError = '';
  let chain = Promise.resolve();
  const enqueue = job => { const task = chain.then(job); chain = task.catch(() => {}); return task; };
  const finite = value => typeof value === 'number' && Number.isFinite(value);
  const format = (value, digits=2) => finite(value) ? value.toLocaleString('de-DE', {minimumFractionDigits:digits,maximumFractionDigits:digits}) : '—';
  function showError(message) { $('error').textContent = message; $('error').hidden = !message; }
  function connection(ok) {
    connected = ok;
    $('connection').textContent = ok ? '● Gerät verbunden' : '● Gerät nicht erreichbar';
    $('connection').className = 'connection ' + (ok ? 'online' : 'offline');
    controls();
  }
  function controls() {
    const editable = connected && status && ['idle','complete','failed'].includes(status.state) && !actionPending;
    $('start').disabled = !editable;
    $('reset').disabled = !connected || !status || actionPending;
    $('configFields').disabled = !editable;
    $('wavDownload').hidden = !(connected && status && ['complete','failed'].includes(status.state) && status.rawSamples > 0 && !actionPending);
    $('jsonDownload').hidden = $('csvDownload').hidden = !(connected && status && status.state === 'complete' && !actionPending);
    $('retryWaves').hidden = !(connected && status && status.state === 'complete' && waveError);
    $('retryWaves').disabled = actionPending;
  }
  async function request(path, options={}, json=true) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), path === '/api/waveforms' ? 30000 : 5000);
    try {
      const response = await fetch(path, {...options, cache:'no-store', signal:controller.signal});
      if (!response.ok) {
        const message = (await response.text()).slice(0,240);
        throw new Error('HTTP ' + response.status + (message ? ': ' + message : ''));
      }
      return json ? await response.json() : await response.text();
    } finally { clearTimeout(timeout); }
  }
  function clearWaves() {
    waveData = null; normalized = []; mean = []; waveAttempted = false; waveError = '';
    $('selected').value = '1'; $('selected').max = '500'; $('selected').disabled = true;
    $('selectedNumber').textContent = '1 / 500';
    $('selectedDetails').textContent = 'Einzelwerte stehen nach der vollständigen Aufnahme bereit.';
    $('chartMessage').textContent = 'Wellenformen werden erst nach Abschluss geladen.';
    $('retryWaves').hidden = true; draw();
  }
  function render(next) {
    if (!knownStates.includes(next.state)) throw new Error('Unbekannter Gerätestatus. Firmware/API prüfen.');
    if ((next.state !== 'complete' || (status && next.runId !== status.runId)) && (waveAttempted || waveData)) clearWaves();
    status = next;
    $('state').textContent = labels[next.state]; $('stateHelp').textContent = helps[next.state];
    const target = finite(next.target) && next.target > 0 ? next.target : 500;
    const count = finite(next.count) ? Math.max(0, Math.min(target, next.count)) : 0;
    $('count').textContent = count + ' / ' + target + ' Impulse';
    $('progress').max = target; $('progress').value = count;
    const hasMetrics = next.state === 'complete';
    for (const [id,key] of [['cps','cps'],['shape','shapeSimilarity'],['periodCv','periodCvPercent'],['amplitudeCv','amplitudeCvPercent']]) $('' + id).textContent = hasMetrics ? format(next[key]) : '—';
    $('clipped').textContent = format(next.clippedPercent);
    $('noise').textContent = format(next.noiseRms,6); $('threshold').textContent = format(next.threshold,6); $('rejected').textContent = format(next.rejected,0);
    const warnings = [];
    if (next.clippingWarning || next.clippedPercent > 0) warnings.push('Übersteuerung erkannt: abgeschnittene Spitzen können den Formvergleich verfälschen.');
    if (next.timingWarning) warnings.push('Unregelmäßiges Timing: mögliche Doppel-/Fehlerkennungen oder ausgelassene Impulse. Frequenzbereich und Signal prüfen.');
    if (next.rejected > 0) warnings.push('Kandidaten wurden verworfen. Mögliche Störungen oder Doppelerkennungen; daraus folgt keine genaue Zahl fehlender Impulse.');
    $('warnings').replaceChildren(...warnings.map(text => { const li = document.createElement('li'); li.textContent = text; return li; })); $('warnings').hidden = !warnings.length;
    showError(typeof next.error === 'string' ? next.error : '');
    if (!configDirty) for (const key of configKeys) if (finite(next[key])) $(key).value = String(next[key]);
    connection(true);
  }
  function normalize(wave) {
    const dc = wave.reduce((sum,v) => sum + v, 0) / wave.length;
    const centered = wave.map(v => v - dc);
    const peak = centered.reduce((max,v) => Math.max(max, Math.abs(v)), 0);
    return centered.map(v => peak > 0 ? v / peak : 0);
  }
  async function loadWaves() {
    if (waveAttempted || !status || status.state !== 'complete') return;
    waveAttempted = true; waveError = ''; $('chartMessage').textContent = 'Wellenformen werden vom Gerät geladen …'; controls();
    try {
      const data = await request('/api/waveforms');
      if (!data || !finite(data.sampleRate) || data.sampleRate <= 0 || !Number.isInteger(data.wavePoints) || data.wavePoints < 2 || !Array.isArray(data.impacts) || data.impacts.length !== status.target || !data.impacts.every(i => Array.isArray(i.waveform) && i.waveform.length === data.wavePoints && i.waveform.every(finite))) throw new Error('Unvollständige oder ungültige Wellenformdaten.');
      waveData = data; normalized = data.impacts.map(i => normalize(i.waveform));
      mean = Array(data.wavePoints).fill(0);
      normalized.forEach(wave => wave.forEach((value,i) => { mean[i] += value / normalized.length; }));
      $('selected').max = String(data.impacts.length); $('selected').disabled = false;
      $('chartMessage').textContent = data.impacts.length + ' Impulse · ' + data.wavePoints + ' Punkte je Impuls · Abtastrate ' + format(data.sampleRate,0) + ' Hz. Blau: alle Kurven; gelb: arithmetischer Mittelwert der normierten Formen.';
      updateSelection();
    } catch (error) {
      waveError = error.name === 'AbortError' ? 'Zeitüberschreitung beim Laden.' : error.message;
      $('chartMessage').textContent = 'Wellenformen nicht geladen: ' + waveError + ' Manuell erneut versuchen; Messwerte bleiben erhalten.';
    }
    controls();
  }
  function updateSelection() {
    if (!waveData) return;
    const selected = Math.max(0, Math.min(waveData.impacts.length - 1, Number($('selected').value) - 1));
    const impact = waveData.impacts[selected];
    $('selectedNumber').textContent = (selected + 1) + ' / ' + waveData.impacts.length;
    $('selectedDetails').textContent = 'Impuls ' + impact.index + ' · Sample ' + impact.sample + ' · Amplitude ' + format(impact.amplitude,6) + ' · Formähnlichkeit ' + format(impact.similarity) + ' %';
    draw();
  }
  function draw() {
    const canvas = $('waveCanvas'), ctx = canvas.getContext('2d');
    if (!ctx) { $('chartMessage').textContent = 'Dieser Browser unterstützt keine Canvas-Darstellung. JSON/CSV bleiben verfügbar.'; return; }
    const rect = canvas.getBoundingClientRect(), dpr = Math.min(window.devicePixelRatio || 1,2);
    canvas.width = Math.max(1,Math.round(rect.width*dpr)); canvas.height = Math.max(1,Math.round(rect.height*dpr));
    ctx.setTransform(dpr,0,0,dpr,0,0);
    const width = rect.width, height = rect.height, left = 44, right = width-15, top = 24, bottom = height-40;
    const y = value => top + (1-value)/2*(bottom-top);
    ctx.clearRect(0,0,width,height); ctx.font = '11px system-ui'; ctx.lineWidth=1;
    [-1,-.5,0,.5,1].forEach(v => { ctx.strokeStyle='#26364a';ctx.beginPath();ctx.moveTo(left,y(v));ctx.lineTo(right,y(v));ctx.stroke();ctx.fillStyle='#adbed4';ctx.textAlign='right';ctx.fillText(format(v,1),left-7,y(v)+4); });
    ctx.textAlign='left';ctx.fillStyle='#adbed4';ctx.fillText('Normierte Amplitude',left,15);
    if (!waveData) { ctx.textAlign='center';ctx.fillStyle='#bdcde1';ctx.fillText('Noch keine vollständigen Wellenformen',width/2,height/2-12);return; }
    const n = waveData.wavePoints;
    for (let tick=0;tick<=4;tick++) {
      const fraction=tick/4, x=left+fraction*(right-left);
      ctx.strokeStyle='#26364a';ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,bottom);ctx.stroke();
      const ms=(fraction-0.5)*(waveData.windowDurationMs || 3.0);
      ctx.fillStyle='#adbed4';ctx.textAlign=tick===0?'left':tick===4?'right':'center';ctx.fillText(format(ms,2),x,bottom+18);
    }
    ctx.textAlign='center';ctx.fillText('Zeit relativ zur Fenstermitte (ms)',(left+right)/2,height-7);
    const line = (wave,color,lineWidth) => {ctx.strokeStyle=color;ctx.lineWidth=lineWidth;ctx.beginPath();wave.forEach((v,i)=>{const x=left+i/(n-1)*(right-left);if(i===0)ctx.moveTo(x,y(v));else ctx.lineTo(x,y(v));});ctx.stroke();};
    ctx.save();ctx.beginPath();ctx.rect(left,top,right-left,bottom-top);ctx.clip();
    normalized.forEach(wave => line(wave,'rgba(107,145,198,0.055)',1));
    line(mean,'#ffcc72',2.5);line(normalized[Number($('selected').value)-1] || normalized[0],'#70f7d2',1.7);ctx.restore();
  }
  async function refresh() { const next = await request('/api/status'); render(next); await loadWaves(); }
  function failure(error) {
    connection(false);
    $('state').textContent = 'Verbindung unterbrochen — Gerätestatus unbekannt';
    $('stateHelp').textContent = 'Motor ausgeschaltet lassen bzw. ausschalten. Letzte Werte können veraltet sein. Geräte-WLAN und http://192.168.4.1 prüfen. Verbindung wird automatisch erneut versucht.';
    showError(error.name === 'AbortError' ? 'Zeitüberschreitung: Das Gerät antwortet nicht.' : 'Kommunikationsfehler: ' + error.message);
  }
  async function poll() {
    try { await enqueue(refresh); } catch(error) { failure(error); }
    setTimeout(poll,500);
  }
  async function action(path,body) {
    if (actionPending) return;
    actionPending=true; controls(); showError('');
    try {
      await enqueue(async () => {
        await request(path,{method:'POST', ...(body ? {headers:{'Content-Type':'application/x-www-form-urlencoded'},body} : {})},false);
        if (path !== '/api/config') clearWaves();
        else { configDirty=false; $('configMessage').textContent='Einstellungen gespeichert.'; }
        await refresh();
      });
    } catch(error) {
      showError('Aktion nicht bestätigt: ' + (error.name === 'AbortError' ? 'Zeitüberschreitung. Status wird erneut geprüft; nicht sofort nochmals starten.' : error.message));
      if (error.name === 'AbortError' || error instanceof TypeError) failure(error);
    } finally { actionPending=false; controls(); }
  }
  $('start').addEventListener('click', () => {
    if (status && status.state === 'complete' && !window.confirm('Neue Messung ersetzt das Ergebnis. Daten heruntergeladen und Motor ausgeschaltet?')) return;
    action('/api/start');
  });
  $('reset').addEventListener('click', () => {
    if (status && status.state !== 'idle' && !window.confirm('Messung abbrechen bzw. Ergebnis löschen? Motor vorher ausschalten.')) return;
    action('/api/reset');
  });
  $('selected').addEventListener('input',updateSelection);
  $('retryWaves').addEventListener('click', async () => {
    if (actionPending) return;
    actionPending=true;controls();
    try { await enqueue(async () => {waveAttempted=false;await loadWaves();}); }
    finally {actionPending=false;controls();}
  });
  $('configForm').addEventListener('input', () => { configDirty=true; $('configMessage').textContent='Ungespeicherte Änderungen'; });
  $('configForm').addEventListener('submit', event => {
    event.preventDefault();
    const values = configKeys.map(key => Number($(key).value));
    if (!values.every(Number.isFinite) || values[0] <= 0 || values[1] <= values[0] || values[2] <= 0 || values[3] < 0) { $('configMessage').textContent='Bitte gültige Zahlen eingeben: 0 < Minimum < Maximum, Rauschfaktor > 0, Mindestschwelle ≥ 0.'; return; }
    const body = new URLSearchParams();configKeys.forEach((key,i)=>body.set(key,String(values[i])));action('/api/config',body.toString());
  });
  let resizeFrame;
  window.addEventListener('resize', () => {cancelAnimationFrame(resizeFrame);resizeFrame=requestAnimationFrame(draw);});
  draw();poll();
})();
</script>
</body>
</html>
)cpsMONK_HTML";
