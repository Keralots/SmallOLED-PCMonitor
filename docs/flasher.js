// SmallOLED Web Flasher — client logic.
// Builds an ESP Web Tools manifest on the fly from a single board map and keeps
// the install button in sync with the chosen OLED. Three display variants share
// two firmware images: SSD1306 (0.96") and SSD1309 (2.42") use the same binary;
// SH1106 (1.3") needs its own.

const BOARDS = {
  ssd1306: {
    label: 'ESP32-C3 · SSD1306 0.96″ (most common)',
    chipFamily: 'ESP32-C3',
    firmware: 'ssd1306',                 // bin id → SmallOLED-ssd1306-<ver>-Full.bin
    display: 'SSD1306 · 128×64',
    note: 'The 0.96″ panel most kits ship with. Avoid dual-colour variants — they won\u2019t work.',
  },
  sh1106: {
    label: 'ESP32-C3 · SH1106 1.3″ (larger)',
    chipFamily: 'ESP32-C3',
    firmware: 'sh1106',
    display: 'SH1106 · 128×64',
    note: 'Uses its own firmware — the SH1106 has a 132-px RAM with a 2-column offset. Don\u2019t flash the 0.96″ image here.',
  },
  ssd1309: {
    label: 'ESP32-C3 · SSD1309 2.42″ (largest)',
    chipFamily: 'ESP32-C3',
    firmware: 'ssd1306',                 // identical image to the 0.96"
    display: 'SSD1309 · 128×64',
    note: 'Shares the same firmware as the 0.96″ SSD1306 — TTP223 touch and the LED still work.',
  },
};

const DEFAULT_BOARD = 'ssd1306';

let _version = null;
let _currentManifestUrl = null;

async function loadVersion() {
  const r = await fetch('firmware/latest/VERSION', { cache: 'no-cache' });
  if (!r.ok) throw new Error(`firmware/latest/VERSION returned HTTP ${r.status}`);
  const text = (await r.text()).trim();
  if (!text) throw new Error('VERSION file is empty');
  return text;
}

function buildManifest(boardId, version) {
  const board = BOARDS[boardId];
  const binUrl = new URL(
    `firmware/latest/SmallOLED-${board.firmware}-${version}-Full.bin`,
    location.href,
  ).href;
  return {
    name: 'SmallOLED PC-Monitor',
    version,
    new_install_prompt_erase: true,
    // After flashing, wait up to 15s for the device to boot, then probe for
    // Improv-Serial. The firmware exposes Improv only on first boot (no stored
    // WiFi credentials), so this kicks in for fresh installs and lets ESP Web
    // Tools show its in-browser "Configure WiFi" dialog (section 02). The
    // WiFiManager AP portal stays up in parallel as a fallback.
    new_install_improv_wait_time: 15,
    builds: [{
      chipFamily: board.chipFamily,
      parts: [{ path: binUrl, offset: 0 }],
    }],
  };
}

function manifestBlobUrl(boardId, version) {
  if (_currentManifestUrl) {
    URL.revokeObjectURL(_currentManifestUrl);
    _currentManifestUrl = null;
  }
  const blob = new Blob([JSON.stringify(buildManifest(boardId, version))], { type: 'application/json' });
  _currentManifestUrl = URL.createObjectURL(blob);
  return _currentManifestUrl;
}

function populateBoardSelect() {
  const sel = document.getElementById('board-select');
  for (const [id, info] of Object.entries(BOARDS)) {
    const opt = document.createElement('option');
    opt.value = id;
    opt.textContent = info.label;
    sel.appendChild(opt);
  }
  sel.value = DEFAULT_BOARD;
}

function renderSpecs(boardId) {
  const info = BOARDS[boardId];
  document.getElementById('spec-chip').textContent = info.chipFamily;
  document.getElementById('spec-display').textContent = info.display;
  document.getElementById('board-note-text').textContent = info.note;
}

function renderInstallButton(boardId, version) {
  // ESP Web Tools caches the manifest on first render — recreate the element on
  // every board switch so the new manifest is picked up.
  const slot = document.getElementById('install-slot');
  slot.innerHTML = '';
  const btn = document.createElement('esp-web-install-button');
  btn.setAttribute('manifest', manifestBlobUrl(boardId, version));

  const fallback = document.createElement('span');
  fallback.setAttribute('slot', 'unsupported');
  fallback.className = 'unsupported';
  fallback.textContent = 'Your browser does not support Web Serial. Use Chrome or Edge on desktop.';
  btn.appendChild(fallback);

  const notAllowed = document.createElement('span');
  notAllowed.setAttribute('slot', 'not-allowed');
  notAllowed.className = 'unsupported';
  notAllowed.textContent = 'Web Serial requires a secure context (HTTPS). Open this page from https://.';
  btn.appendChild(notAllowed);

  slot.appendChild(btn);
}

function showStatus(message, kind) {
  const line = document.getElementById('status-line');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function showVersion(version) {
  document.getElementById('spec-version').textContent = version;
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = version;
}

function showVersionError(err) {
  document.getElementById('spec-version').textContent = 'unavailable';
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = 'unavailable';
  showStatus(
    `Could not load firmware version (${err.message}). The site may be mid-deploy — try again in a minute.`,
    'error',
  );
  document.getElementById('install-slot').innerHTML = '';
}

function checkBrowserSupport() {
  if (!('serial' in navigator)) {
    document.getElementById('browser-callout').classList.add('show');
  }
}

async function init() {
  checkBrowserSupport();
  populateBoardSelect();
  renderSpecs(DEFAULT_BOARD);
  wireMonitor();

  try {
    _version = await loadVersion();
  } catch (err) {
    showVersionError(err);
    return;
  }

  showVersion(_version);
  renderInstallButton(DEFAULT_BOARD, _version);

  document.getElementById('board-select').addEventListener('change', (e) => {
    const boardId = e.target.value;
    renderSpecs(boardId);
    renderInstallButton(boardId, _version);
  });
}

// ────────── 04 serial monitor ──────────
// Reads the device's USB CDC stream at 115200 baud and appends decoded text to
// <pre id="monitor-output">. Independent of the install button — only one
// program can hold the port at a time, so don't click Install while connected.

let _monitorPort = null;
let _monitorReader = null;
let _monitorReadLoopRunning = false;

async function monitorConnect() {
  if (_monitorPort) return;
  let port;
  try {
    port = await navigator.serial.requestPort();
  } catch (err) {
    if (err && err.name === 'NotFoundError') return; // user cancelled picker
    setMonitorStatus(`Could not pick a port: ${err.message}`, 'error');
    return;
  }
  try {
    await port.open({ baudRate: 115200 });
  } catch (err) {
    setMonitorStatus(`Could not open the port: ${err.message}. Close other monitors and try again.`, 'error');
    return;
  }
  _monitorPort = port;
  toggleMonitorButtons(true);
  setMonitorStatus('Connected. Reading from device…', 'ok');
  monitorReadLoop().catch((err) => setMonitorStatus(`Read error: ${err.message}`, 'error'));
}

async function monitorDisconnect() {
  if (!_monitorPort) return;
  setMonitorStatus('Disconnecting…');
  try { if (_monitorReader) await _monitorReader.cancel(); } catch (_) {}
  const startedAt = Date.now();
  while (_monitorReadLoopRunning && Date.now() - startedAt < 1000) {
    await new Promise((r) => setTimeout(r, 20));
  }
  try { await _monitorPort.close(); } catch (_) {}
  _monitorPort = null;
  _monitorReader = null;
  toggleMonitorButtons(false);
  setMonitorStatus('Disconnected.');
}

async function monitorReadLoop() {
  _monitorReadLoopRunning = true;
  const decoder = new TextDecoder();
  try {
    if (!_monitorPort || !_monitorPort.readable) return;
    const reader = _monitorPort.readable.getReader();
    _monitorReader = reader;
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.byteLength) appendMonitorOutput(decoder.decode(value, { stream: true }));
      }
    } finally {
      try { reader.releaseLock(); } catch (_) {}
      _monitorReader = null;
    }
  } finally {
    _monitorReadLoopRunning = false;
  }
}

function appendMonitorOutput(text) {
  const out = document.getElementById('monitor-output');
  const wasEmpty = out.textContent.length === 0;
  const atBottom = out.scrollHeight - out.clientHeight - out.scrollTop < 4;
  out.appendChild(document.createTextNode(text));
  if (out.textContent.length > 200000) out.textContent = out.textContent.slice(-150000);
  if (atBottom) out.scrollTop = out.scrollHeight;
  if (wasEmpty) setMonitorBufferButtons(true);
}

function monitorExport() {
  const out = document.getElementById('monitor-output');
  const text = out.textContent;
  if (!text) return;
  const ts = new Date().toISOString().replace(/[:.]/g, '-').replace('Z', '');
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `smalloled-serial-${ts}.txt`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function monitorClear() {
  document.getElementById('monitor-output').textContent = '';
  setMonitorBufferButtons(false);
}

function setMonitorBufferButtons(hasContent) {
  document.getElementById('monitor-export').disabled = !hasContent;
  document.getElementById('monitor-clear').disabled = !hasContent;
}

function setMonitorStatus(message, kind) {
  const line = document.getElementById('monitor-status');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function toggleMonitorButtons(connected) {
  document.getElementById('monitor-connect').disabled = connected;
  document.getElementById('monitor-disconnect').disabled = !connected;
}

function wireMonitor() {
  const connectBtn = document.getElementById('monitor-connect');
  if (!('serial' in navigator)) {
    connectBtn.disabled = true;
    setMonitorStatus('Web Serial is unavailable in this browser — use desktop Chrome or Edge.', 'warn');
    return;
  }
  connectBtn.addEventListener('click', monitorConnect);
  document.getElementById('monitor-disconnect').addEventListener('click', monitorDisconnect);
  document.getElementById('monitor-export').addEventListener('click', monitorExport);
  document.getElementById('monitor-clear').addEventListener('click', monitorClear);
}

init();
