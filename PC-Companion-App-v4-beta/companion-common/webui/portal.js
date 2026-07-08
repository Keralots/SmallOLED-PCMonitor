(function () {
'use strict';
var $  = function (s, r) { return (r || document).querySelector(s); };
var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
var CFG = window.SOLED || {};
var navToggle = $('#navToggle'), navScrim = $('#navScrim');
function setNav(open) {
document.documentElement.classList.toggle('nav-open', open);
if (navToggle) navToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
}
function closeNav() { setNav(false); }
if (navToggle) navToggle.addEventListener('click', function () { setNav(!document.documentElement.classList.contains('nav-open')); });
if (navScrim) navScrim.addEventListener('click', closeNav);
document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeNav(); });
var navItems = $$('.nav-item');
var pages = $$('.page');
var crumb = $('#crumb');
function showPage(key) {
pages.forEach(function (p) { p.classList.toggle('active', p.dataset.page === key); });
navItems.forEach(function (n) { n.classList.toggle('active', n.dataset.nav === key); });
var active = navItems.filter(function (n) { return n.dataset.nav === key; })[0];
if (active && crumb) crumb.textContent = active.textContent.replace(/PC$/, '').trim();
window.scrollTo(0, 0);
closeNav();
try { localStorage.setItem('soled_section', key); } catch (e) {}
}
navItems.forEach(function (n) { n.addEventListener('click', function () { showPage(n.dataset.nav); }); });
try { var s = localStorage.getItem('soled_section'); if (s && $('[data-page="' + s + '"]')) showPage(s); } catch (e) {}
var accSw = $$('.acc-sw');
function setAccent(acc) {
document.documentElement.setAttribute('data-accent', acc);
accSw.forEach(function (b) { b.classList.toggle('on', b.dataset.acc === acc); });
try { localStorage.setItem('soled_accent', acc); } catch (e) {}
}
accSw.forEach(function (b) { b.addEventListener('click', function () { setAccent(b.dataset.acc); }); });
try { var a = localStorage.getItem('soled_accent'); if (a) setAccent(a); } catch (e) {}
var modeBtns = $$('.mode-toggle button');
function setMode(mode) {
document.documentElement.setAttribute('data-mode', mode);
modeBtns.forEach(function (b) { b.classList.toggle('on', b.dataset.mode === mode); });
var meta = $('meta[name="theme-color"]'); if (meta) meta.setAttribute('content', mode === 'dark' ? '#161512' : '#f4f0e7');
try { localStorage.setItem('soled_mode', mode); } catch (e) {}
}
modeBtns.forEach(function (b) { b.addEventListener('click', function () { setMode(b.dataset.mode); }); });
try { var m = localStorage.getItem('soled_mode'); if (m) setMode(m); } catch (e) {}
var saveMeta = $('#saveMeta');
function markDirty() { if (saveMeta) { saveMeta.classList.remove('clean'); $('.txt', saveMeta).textContent = 'Unsaved changes'; } }
function markClean(txt) { if (saveMeta) { saveMeta.classList.add('clean'); $('.txt', saveMeta).textContent = txt || 'All saved'; } }
var form = $('#cfgForm');
form.addEventListener('input', markDirty);
form.addEventListener('change', markDirty);
function fmtRange(inp) {
var span = $('.range-val[data-for="' + inp.id + '"]');
if (!span) return;
var suf = inp.dataset.suffix || '';
if (inp.dataset.pct) { span.textContent = Math.round((inp.value / 255) * 100) + '%'; return; }
var div = parseFloat(inp.dataset.div || '1');
var fixed = parseInt(inp.dataset.fixed || '0', 10);
span.textContent = (inp.value / div).toFixed(fixed) + suf;
}
$$('input[type="range"]').forEach(function (inp) { fmtRange(inp); inp.addEventListener('input', function () { fmtRange(inp); }); });
function toggle(el, on) { if (el) el.style.display = on ? '' : 'none'; }
var nightChk = $('#enableScheduledDimming');
if (nightChk) { var fn = function () { toggle($('#nightFields'), nightChk.checked); }; nightChk.addEventListener('change', fn); fn(); }
var staticSel = $('#useStaticIP');
if (staticSel) { var fs = function () { toggle($('#staticFields'), staticSel.value === '1'); }; staticSel.addEventListener('change', fs); fs(); }
var refSel = $('#refreshRateMode');
if (refSel) { var fr = function () { toggle($('#refreshRateFields'), refSel.value === '1'); }; refSel.addEventListener('change', fr); fr(); }
var marioEnc = $('#marioIdleEncounters');
if (marioEnc) { var fe = function () { toggle($('#marioEncFields'), marioEnc.checked); }; marioEnc.addEventListener('change', fe); fe(); }
var STYLE_PANELS = { '0':'marioSettings','3':'spaceSettings','4':'spaceSettings','5':'pongSettings','6':'pacmanSettings','7':'snakeSettings','8':'tetrisSettings','10':'asteroidsSettings','11':'dinoSettings' };
var ALL_PANELS = ['marioSettings','spaceSettings','pongSettings','pacmanSettings','snakeSettings','tetrisSettings','asteroidsSettings','dinoSettings'];
var clockStyle = $('#clockStyle');
function syncClockPanels() {
ALL_PANELS.forEach(function (id) { var el = document.getElementById(id); if (el) el.style.display = 'none'; });
var show = STYLE_PANELS[clockStyle.value];
if (show) { var e = document.getElementById(show); if (e) e.style.display = ''; }
}
if (clockStyle) { clockStyle.addEventListener('change', syncClockPanels); syncClockPanels(); }
var dn = $('#deviceName');
if (dn) dn.addEventListener('input', function () {
var v = dn.value.toLowerCase() || 'smalloled';
var hp = $('#hostPreview'); if (hp) hp.textContent = v;
var sh = $('#srHost'); if (sh) sh.textContent = v;
});
var metricsData = [];
var DEVTIME = '12:34';
var MAX_ROWS = CFG.maxRows || 5;
var IS_LARGE = !!CFG.isLarge;
function rowGeom() {
var rm = parseInt($('#rowMode').value, 10);
IS_LARGE = (rm >= 2);
MAX_ROWS = (rm === 0) ? 5 : (rm === 1) ? 6 : (rm === 2) ? 2 : 3;
return { rows: MAX_ROWS, cols: IS_LARGE ? 1 : 2, large: IS_LARGE };
}
function byId(id) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].id === id) return metricsData[i]; return null; }
var FONT = (function () {
var h = "00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800ffe7c3e7ff0018241800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a7f3e1c1c08081c1c3e7f14227f22145f5f005f5f06097f017f006689956a606060606094a2ffa29408047e040810207e201008082a1c08081c2a08081e101010100c1e0c1e0c30383e3830060e3e0e06000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c08040408485454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c4400083641000000770000004136080002010204023c2623263c1ea1a161123a4040207a385454555921555579412254547842215554784020545579400c1e5272123955555559395454545939555454580000457c410002457d420001457c407d1211127df0282528f07c545545002054547c547c0a097f4932494949323a4444443a324a4848303a4141217a3a42402078009da0a07d3d4242423d3d4040403d3c24ff2424487e4943662b2ffc2f2bff0929f620c0887e090320545479410000447d413048484a32384040227a007a0a0a727d0d19317d2629292f28262929292630484d4020380808080808080808382f10c8acba2f102834fa00007b000008142a142222142a14085500550055aa55aa55aaff55ff55ff000000ff00101010ff00141414ff001010ff00ff1010f010f0141414fc001414f700ff0000ff00ff1414f404fc141417101f10101f101f1414141f00101010f0000000001f101010101f10101010f010000000ff101010101010101010ff10000000ff140000ff00ff00001f10170000fc04f414141710171414f404f40000ff00f714141414141414f700f7141414171410101f101f141414f4141010f010f000001f101f0000001f14000000fc140000f010f01010ff10ff141414ff141010101f00000000f010fffffffffff0f0f0f0f0ffffff0000000000ffff0f0f0f0f0f3844443844fc4a4a4a347e02020606027e027e0263554941633844443c04407e201e2006027e020299a5e7a5991c2a492a1c4c7201724c304a4d4d303048784830bc625a463d3e494949007e0101017e2a2a2a2a2a44445f444440514a444040444a51400000ff0103e080ff000008086b6b083612362436060f090f06000018180000001010003040ff0101001f01011e00191d1712003c3c3c3c0000000000";
var a = new Uint8Array(1280);
for (var i = 0; i < 1280; i++) a[i] = parseInt(h.substr(i * 2, 2), 16);
return a;
})();
function FB() { this.buf = new Uint8Array(128 * 64); }
FB.prototype.px = function (x, y, v) { x |= 0; y |= 0; if (x >= 0 && x < 128 && y >= 0 && y < 64) this.buf[y * 128 + x] = v; };
FB.prototype.fillRect = function (x, y, w, h, v) { for (var yy = y; yy < y + h; yy++) for (var xx = x; xx < x + w; xx++) this.px(xx, yy, v); };
FB.prototype.drawRect = function (x, y, w, h, v) { var xx, yy; for (xx = x; xx < x + w; xx++) { this.px(xx, y, v); this.px(xx, y + h - 1, v); } for (yy = y; yy < y + h; yy++) { this.px(x, yy, v); this.px(x + w - 1, yy, v); } };
function drawChar(fb, x, y, ch, size) {
var o = ch.charCodeAt(0); if (o > 255) o = 63;
if (x >= 128 || y >= 64 || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0) return;
var base = o * 5;
for (var col = 0; col < 5; col++) {
var line = FONT[base + col];
for (var row = 0; row < 8; row++) {
if ((line >> row) & 1) {
if (size === 1) fb.px(x + col, y + row, 255);
else fb.fillRect(x + col * size, y + row * size, size, size, 255);
}
}
}
}
function write(fb, x, y, text, size, wrap) {
var cx = x, cy = y;
for (var i = 0; i < text.length; i++) {
var ch = text.charAt(i);
if (ch === '\n') { cx = 0; cy += size * 8; continue; }
if (ch === '\r') continue;
if (wrap && (cx + size * 6) > 128) { cx = 0; cy += size * 8; }
drawChar(fb, cx, cy, ch, size); cx += size * 6;
}
return [cx, cy];
}
function buildMetricText(label, unit, value, rpmK, netMB) {
var dl = (label || '').replace(/\^/g, ' ');
var stripped = dl.replace(/ +$/, '');
var trailing = dl.length - stripped.length; dl = stripped;
if (dl.charAt(dl.length - 1) === '%') dl = dl.slice(0, -1);
var spaces = new Array(Math.min(trailing, 10) + 1).join(' ');
if (rpmK && unit === 'RPM' && value >= 1000) return dl + ':' + spaces + (value / 1000).toFixed(1) + 'K';
if (unit === 'KB/s') { var a = value / 10; if (netMB) return dl + ':' + spaces + (a / 1000).toFixed(1) + 'M'; return dl + ':' + spaces + a.toFixed(1) + unit; }
return dl + ':' + spaces + value + unit;
}
function buildCompanionText(unit, value, netMB) {
if (unit === 'KB/s') { var cv = value / 10; if (netMB) return ' ' + (cv / 1000).toFixed(1) + 'M'; return ' ' + cv.toFixed(1) + unit; }
return ' ' + value + unit;
}
function renderFrame() {
saveFormState();
var fb = new FB();
var rm = parseInt($('#rowMode').value, 10);
var showClock = $('#showClock').checked;
var clockPos = parseInt(($('#clockPosition') || {}).value || '0', 10);
var clockOffset = parseInt(($('#clockOffset') || {}).value || '0', 10) || 0;
var rpmK = $('#rpmKFormat') ? $('#rpmKFormat').checked : false;
var netMB = $('#netMBFormat') ? $('#netMBFormat').checked : false;
var ts = DEVTIME || '12:34';
var isLarge = rm >= 2, textH = isLarge ? 16 : 8;
function slotText(pos) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].position === pos) return metricsData[i]; return null; }
function slotBar(pos) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].barPosition === pos) return metricsData[i]; return null; }
function lblOf(mt) { return (mt.label && mt.label.length) ? mt.label : mt.name; }
function drawBar(x, y, mt, large) {
var ax = x + (mt.barOffsetX || 0), aw = (mt.barWidth || 60);
if (ax >= 128 || ax < 0) return;
if (ax + aw > 128) aw = 128 - ax;
if (aw <= 0) return;
var bmin = mt.barMin | 0, bmax = (mt.barMax == null ? 100 : mt.barMax);
var rng = bmax - bmin; if (rng <= 0) rng = 100;
var dv = (mt.unit === 'KB/s') ? Math.floor(mt.value / 10) : mt.value;
var vir = Math.max(bmin, Math.min(dv, bmax)) - bmin;
var fillW = Math.floor(vir * (aw - 2) / rng);
var barH = large ? 16 : 8;
fb.drawRect(ax, y, aw, barH, 255);
if (fillW > 0) fb.fillRect(ax + 1, y + 1, fillW, barH - 2, 255);
}
function drawText(x, y, mt, size, wrap, large) {
var text = buildMetricText(lblOf(mt), mt.unit, mt.value | 0, rpmK, netMB);
var comp = (mt.companionId > 0) ? byId(mt.companionId) : null;
if (comp && !large) text += buildCompanionText(comp.unit, comp.value | 0, netMB);
var cc = write(fb, x, y, text, size, wrap);
if (comp && large) {
var ct = buildCompanionText(comp.unit, comp.value | 0, netMB).slice(1);
var cxp = 128 - ct.length * 12; if (cxp < cc[0] + 4) cxp = cc[0] + 4;
write(fb, cxp, y, ct, size, wrap);
}
}
if (isLarge) {
var maxRows = (rm === 2) ? 2 : 3;
var startY = (rm === 2) ? 8 : 4;
if (showClock) { write(fb, 48 + clockOffset, 0, ts, 1, false); startY = (rm === 2) ? 12 : 10; }
var rowH = (rm === 2) ? 32 : (showClock ? 18 : 20);
for (var r = 0; r < maxRows; r++) {
var y = startY + r * rowH; if (y + textH > 64) break;
var b = slotBar(r); if (b) { drawBar(0, y, b, true); continue; }
var e = slotText(r); if (e) drawText(0, y, e, 2, false, true);
}
} else {
var COL1 = 0, COL2 = 62, maxRowsN = (rm === 0) ? 5 : 6, sY, rH;
if (rm === 0) { sY = 0; rH = (showClock && clockPos === 0) ? 11 : 13; } else { sY = 2; rH = 10; }
if (showClock) {
if (clockPos === 0) { write(fb, 48 + clockOffset, sY, ts, 1, true); sY += 10; }
else if (clockPos === 1) write(fb, COL1 + clockOffset, sY, ts, 1, true);
else if (clockPos === 2) write(fb, COL2 + clockOffset, sY, ts, 1, true);
}
for (var rr = 0; rr < maxRowsN; rr++) {
var yy = sY + rr * rH; if (yy + 8 > 64) break;
var cols = [[COL1, rr * 2, showClock && clockPos === 1 && rr === 0], [COL2, rr * 2 + 1, showClock && clockPos === 2 && rr === 0]];
for (var ci = 0; ci < 2; ci++) {
var cx = cols[ci][0], pos = cols[ci][1]; if (cols[ci][2]) continue;
var bb = slotBar(pos); if (bb) { drawBar(cx, yy, bb, false); continue; }
var ee = slotText(pos); if (ee) drawText(cx, yy, ee, 1, true, false);
}
}
}
blit(fb);
var g = rowGeom();
$('#oledMeta').textContent = '128x64 - ' + g.rows + ' rows - ' + (g.cols === 1 ? 'single column' : '2 columns');
}
function blit(fb) {
var canvas = $('#oledCanvas'); if (!canvas) return;
var ctx = canvas.getContext('2d');
var amber = document.documentElement.getAttribute('data-accent') === 'amber';
var on = amber ? [255, 207, 122] : [132, 243, 173];
var off = amber ? [12, 8, 3] : [6, 13, 9];
var img = ctx.createImageData(128, 64), d = img.data;
for (var i = 0; i < fb.buf.length; i++) {
var c = fb.buf[i] ? on : off, o = i * 4;
d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
}
ctx.putImageData(img, 0, 0);
}
function rowMaxSlots(rm) { return rm === 0 ? 10 : rm === 1 ? 12 : rm === 2 ? 2 : 3; }
function slotGeom(rm, slot, showClock, clockPos) {
if (slot < 0 || slot >= rowMaxSlots(rm)) return null;
if (rm >= 2) {
var sy, rh;
if (showClock) { if (rm === 2) { sy = 12; rh = 32; } else { sy = 10; rh = 18; } }
else { if (rm === 2) { sy = 8; rh = 32; } else { sy = 4; rh = 20; } }
return { x: 0, y: sy + slot * rh, w: 128, h: rh };
}
var sy2, rh2;
if (rm === 0) { sy2 = 0; rh2 = (showClock && clockPos === 0) ? 11 : 13; } else { sy2 = 2; rh2 = 10; }
if (showClock && clockPos === 0) sy2 += 10;
var row = Math.floor(slot / 2), col = slot % 2;
return { x: col === 0 ? 0 : 62, y: sy2 + row * rh2, w: col === 0 ? 60 : 64, h: rh2 };
}
function clockBlockedSlot(rm, clockPos, showClock) { if (!showClock || rm >= 2 || clockPos === 0) return -1; return clockPos === 1 ? 0 : 1; }
function buildDropCells() {
var host = $('#dropCells'); if (!host) return;
var rm = parseInt($('#rowMode').value, 10);
var showClock = $('#showClock').checked;
var clockPos = parseInt(($('#clockPosition') || {}).value || '0', 10);
host.innerHTML = '';
var n = rowMaxSlots(rm), blocked = clockBlockedSlot(rm, clockPos, showClock);
for (var s = 0; s < n; s++) {
if (s === blocked) continue;
var g = slotGeom(rm, s, showClock, clockPos); if (!g) continue;
var h = Math.min(g.h, 64 - g.y); if (h <= 0) continue;
var cell = document.createElement('div');
var occ = metricsData.filter(function (m) { return m.position === s; })[0];
cell.className = 'drop-cell' + (occ ? ' filled' : ''); cell.dataset.slot = s;
if (occ) cell.title = 'Click to remove ' + occ.name + ' from the screen';
cell.style.left = (g.x / 128 * 100) + '%'; cell.style.top = (g.y / 64 * 100) + '%';
cell.style.width = (g.w / 128 * 100) + '%'; cell.style.height = (h / 64 * 100) + '%';
attachDrop(cell, s);
host.appendChild(cell);
}
}
var DRAG_ID = null;
function setPositions(targetSlot) {
saveFormState();
if (targetSlot !== 255) metricsData.forEach(function (x) { if (x.id !== DRAG_ID && x.position === targetSlot) x.position = 255; });
var mt = byId(DRAG_ID); if (mt) { mt.position = targetSlot; if (targetSlot === 255) mt.barPosition = 255; }
renderMetrics(); renderFrame(); buildDropCells(); buildChipTray(); markDirty();
}
function attachDrop(cell, slot) {
cell.addEventListener('dragover', function (e) { if (DRAG_ID != null) { e.preventDefault(); cell.classList.add('over'); } });
cell.addEventListener('dragleave', function () { cell.classList.remove('over'); });
cell.addEventListener('drop', function (e) { e.preventDefault(); cell.classList.remove('over'); if (DRAG_ID != null) setPositions(slot); });
cell.addEventListener('click', function () {
if (SEL_ID != null) { DRAG_ID = SEL_ID; setSel(null); setPositions(slot); DRAG_ID = null; return; }
var occ = metricsData.filter(function (m) { return m.position === slot; })[0];
if (occ) { DRAG_ID = occ.id; setPositions(255); DRAG_ID = null; }
});
}
var SEL_ID = null;
function setSel(id) {
SEL_ID = id;
var st = $('#oledStage'); if (st) st.classList.toggle('placing', id != null);
var tray = $('#chipTray'); if (tray) tray.classList.toggle('placing', id != null);
buildChipTray();
}
function slotLabel(pos, g) {
if (pos === 255 || pos == null) return 'Hidden';
if (g.large) return 'R' + (pos + 1);
return 'R' + (Math.floor(pos / 2) + 1) + (pos % 2 === 0 ? '·L' : '·R');
}
function buildChipTray() {
var host = $('#chipTray'); if (!host) return;
host.innerHTML = '';
if (!metricsData.length) { host.innerHTML = '<span class="chip-empty">No metrics yet - start the companion app on your PC.</span>'; return; }
var g = rowGeom();
var sorted = metricsData.slice().sort(function (a, b) { return a.displayOrder - b.displayOrder; });
sorted.forEach(function (mt) {
var placed = mt.position !== 255 && mt.position != null;
var chip = document.createElement('div');
chip.className = 'chip' + (placed ? ' placed' : '') + (SEL_ID === mt.id ? ' sel' : '');
chip.innerHTML = '<span class="cn">' + esc(mt.name) + '</span><span class="cb">' + slotLabel(mt.position, g) + '</span>';
makeRowDraggable(chip, mt.id);
chip.addEventListener('click', function () { setSel(SEL_ID === mt.id ? null : mt.id); });
host.appendChild(chip);
});
}
function makeRowDraggable(row, id) {
row.setAttribute('draggable', 'true');
row.addEventListener('dragstart', function (e) {
DRAG_ID = id; row.classList.add('drag-src');
var st = $('#oledStage'); if (st) st.classList.add('dragging');
try { e.dataTransfer.setData('text/plain', String(id)); e.dataTransfer.effectAllowed = 'move'; } catch (_) {}
});
row.addEventListener('dragend', function () {
DRAG_ID = null; row.classList.remove('drag-src');
var st = $('#oledStage'); if (st) st.classList.remove('dragging');
});
}
function setupListDrop() {
var tray = $('#chipTray');
if (tray) {
tray.addEventListener('dragover', function (e) { if (DRAG_ID != null) e.preventDefault(); });
tray.addEventListener('drop', function (e) { if (DRAG_ID != null) { e.preventDefault(); setPositions(255); } });
tray.addEventListener('click', function (e) { if (e.target === tray && SEL_ID != null) setSel(null); });
}
document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && SEL_ID != null) setSel(null); });
var list = $('#metricsList'); if (!list) return;
list.addEventListener('dragover', function (e) { if (DRAG_ID != null) e.preventDefault(); });
list.addEventListener('drop', function (e) { if (DRAG_ID != null) { e.preventDefault(); setPositions(255); } });
}
function saveFormState() {
metricsData.forEach(function (mt) {
var lbl = document.querySelector('input[name="label_' + mt.id + '"]'); if (lbl) mt.label = lbl.value;
var comp = document.getElementById('comp_' + mt.id); if (comp) mt.companionId = parseInt(comp.value, 10);
var bp = document.getElementById('barPos_' + mt.id); if (bp) mt.barPosition = parseInt(bp.value, 10);
var bmin = document.querySelector('input[name="barMin_' + mt.id + '"]'); if (bmin) mt.barMin = parseInt(bmin.value, 10) || 0;
var bmax = document.querySelector('input[name="barMax_' + mt.id + '"]'); if (bmax) mt.barMax = parseInt(bmax.value, 10) || 100;
var bw = document.querySelector('input[name="barWidth_' + mt.id + '"]'); if (bw) mt.barWidth = parseInt(bw.value, 10) || 60;
var bo = document.querySelector('input[name="barOffset_' + mt.id + '"]'); if (bo) mt.barOffsetX = parseInt(bo.value, 10) || 0;
});
}
function onRowMode() {
saveFormState();
var g = rowGeom();
var maxPos = g.large ? g.rows : g.rows * 2;
var hidden = metricsData.filter(function (mt) {
return (mt.position !== 255 && mt.position >= maxPos) || (mt.barPosition !== 255 && mt.barPosition >= maxPos);
});
if (hidden.length > 0) {
var names = hidden.map(function (mt) { return mt.name; }).join(', ');
if (!confirm('Warning: ' + hidden.length + ' metric(s) (' + names + ') will be hidden in this row mode. Continue?')) { return; }
metricsData.forEach(function (mt) {
if (mt.position !== 255 && mt.position >= maxPos) mt.position = 255;
if (mt.barPosition !== 255 && mt.barPosition >= maxPos) mt.barPosition = 255;
});
}
renderMetrics(); buildDropCells(); buildChipTray(); renderFrame();
}
function posOptionsHtml(cur, g, includeNoneLabel) {
var html = '<option value="255">' + (includeNoneLabel || 'None (hidden)') + '</option>';
for (var r = 0; r < g.rows; r++) {
if (g.large) {
html += '<option value="' + r + '"' + (cur === r ? ' selected' : '') + '>Row ' + (r + 1) + '</option>';
} else {
var lp = r * 2, rp = r * 2 + 1;
html += '<option value="' + lp + '"' + (cur === lp ? ' selected' : '') + '>Row ' + (r + 1) + ' &middot; Left</option>';
html += '<option value="' + rp + '"' + (cur === rp ? ' selected' : '') + '>Row ' + (r + 1) + ' &middot; Right</option>';
}
}
return html;
}
function esc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
// Slider range for the value-space bar min/max, by unit (the editable number
// box still accepts any value beyond the slider's range).
function valRange(unit) {
if (unit === 'C') return [0, 120, 1];
if (unit === 'RPM') return [0, 5000, 50];
if (unit === 'KB/s') return [0, 100000, 100];
return [0, 100, 1]; // % and default
}
// A "slider + editable number" control (like the clock offset, but the number
// is editable). The NUMBER keeps name=<name> so FormData / saveFormState read it;
// the range is just a quick-set that syncs to it.
function barField(label, name, val, lo, hi, step, bounded) {
var na = bounded ? (' min="' + lo + '" max="' + hi + '"') : '';
return '<div><label class="field-label">' + label + '</label><div class="range-row">' +
'<input type="range" class="bar-range" data-for="' + name + '" min="' + lo + '" max="' + hi + '" step="' + step + '" value="' + val + '">' +
'<input type="number" class="range-num" name="' + name + '" value="' + val + '"' + na + '></div></div>';
}
function renderMetrics() {
var g = rowGeom();
var list = $('#metricsList');
var openIds = {}; Array.prototype.forEach.call(list.querySelectorAll('details.metric-row[open]'), function (d) { openIds[d.dataset.id] = 1; });
list.innerHTML = '';
if (!metricsData.length) { list.innerHTML = '<p class="field-hint">No metrics received yet. Start the companion app on your PC.</p>'; return; }
var sorted = metricsData.slice().sort(function (a, b) { return a.displayOrder - b.displayOrder; });
sorted.forEach(function (mt) {
var compName = mt.companionId > 0 ? (metricsData.filter(function (x) { return x.id === mt.companionId; })[0] || {}).name : null;
var compOpts = '<option value="0">None</option>';
metricsData.forEach(function (x) {
if (x.id !== mt.id) compOpts += '<option value="' + x.id + '"' + (mt.companionId === x.id ? ' selected' : '') + '>' + esc(x.name) + ' (' + esc(x.unit) + ')</option>';
});
var placed = mt.position !== 255 && mt.position != null;
var row = document.createElement('details');
row.className = 'metric-row';
row.dataset.id = mt.id;
row.dataset.placed = placed ? '1' : '0';
if (openIds[mt.id]) row.open = true;
var vr = valRange(mt.unit);
row.innerHTML =
'<summary class="metric-sum"><span class="ms-main"><span class="ms-nm">' + esc(mt.label || mt.name) + '</span>' +
'<span class="ms-sub">' + esc(mt.unit) + (compName ? ' · + ' + esc(compName) : '') + '</span></span>' +
'<span class="ms-badge">' + slotLabel(mt.position, g) + '</span><span class="ms-chev"></span></summary>' +
'<div class="metric-body"><div class="metric-adv">' +
'<div><label class="field-label">Custom label (10 max)</label><input type="text" name="label_' + mt.id + '" value="' + esc(mt.label) + '" maxlength="10" placeholder="' + esc(mt.name) + '"></div>' +
'<div><label class="field-label">Pair with</label><div class="select-wrap"><select id="comp_' + mt.id + '" name="companion_' + mt.id + '">' + compOpts + '</select></div></div>' +
'<div class="full"><label class="field-label">Progress bar position</label><div class="select-wrap"><select id="barPos_' + mt.id + '" name="barPosition_' + mt.id + '">' + posOptionsHtml(mt.barPosition, g, 'None') + '</select></div></div>' +
'<div class="bar-opts" id="barOpts_' + mt.id + '" style="display:' + ((mt.barPosition !== 255 && mt.barPosition != null) ? 'contents' : 'none') + '">' +
barField('Bar min', 'barMin_' + mt.id, (mt.barMin || 0), vr[0], vr[1], vr[2], false) +
barField('Bar max', 'barMax_' + mt.id, (mt.barMax == null ? 100 : mt.barMax), vr[0], vr[1], vr[2], false) +
barField('Bar width (px)', 'barWidth_' + mt.id, (mt.barWidth || 60), 10, 64, 1, true) +
barField('Bar offset X (px)', 'barOffset_' + mt.id, (mt.barOffsetX || 0), 0, 54, 1, true) +
'</div>' +
'</div></div>' +
'<input type="hidden" name="order_' + mt.id + '" value="' + mt.displayOrder + '">' +
'<input type="hidden" name="position_' + mt.id + '" value="' + mt.position + '">';
list.appendChild(row);
$('#comp_' + mt.id, row).addEventListener('change', function () { saveFormState(); renderMetrics(); renderFrame(); markDirty(); });
$('#barPos_' + mt.id, row).addEventListener('change', function () { var bo = $('#barOpts_' + mt.id, row); if (bo) bo.style.display = (parseInt(this.value, 10) !== 255 ? 'contents' : 'none'); saveFormState(); renderFrame(); markDirty(); });
// Keep each bar slider and its editable number box in sync (target-phase
// listeners run before the row's bubbled 'input', so saveFormState reads fresh).
Array.prototype.forEach.call(row.querySelectorAll('input.bar-range'), function (rng) {
var num = row.querySelector('input[name="' + rng.dataset.for + '"]');
rng.addEventListener('input', function () { if (num) num.value = rng.value; });
if (num) num.addEventListener('input', function () { rng.value = num.value; });
});
row.addEventListener('input', function () { saveFormState(); renderFrame(); var nm = $('.ms-nm', row), li = document.querySelector('input[name="label_' + mt.id + '"]'); if (nm && li) nm.textContent = li.value || mt.name; });
});
}
var rowModeSel = $('#rowMode');
if (rowModeSel) rowModeSel.addEventListener('change', onRowMode);
var showClockChk = $('#showClock');
if (showClockChk) showClockChk.addEventListener('change', function () { buildDropCells(); renderFrame(); });
var clockPosSel = $('#clockPosition');
if (clockPosSel) clockPosSel.addEventListener('change', function () { buildDropCells(); renderFrame(); });
['clockOffset', 'rpmKFormat', 'netMBFormat'].forEach(function (id) {
var el = document.getElementById(id); if (!el) return;
el.addEventListener('input', renderFrame); el.addEventListener('change', renderFrame);
});
setupListDrop();
// applyMetrics(data, full): full=true rebuilds the whole editor (used on first
// load and after the selection changes via /api/select); full=false just patches
// live values for the preview. pollMetrics never adds/removes metrics, so the
// selection flow MUST call reloadMetrics() to surface newly picked sensors.
function setDisplayControls(disp) {
if (!disp) return;
function sv(id, v) { var e = $('#' + id); if (e == null || v == null) return; if (e.type === 'checkbox') e.checked = !!v; else e.value = v; }
sv('rowMode', disp.rowMode); sv('showClock', disp.showClock); sv('clockPosition', disp.clockPosition);
sv('clockOffset', disp.clockOffset); sv('rpmKFormat', disp.rpmK); sv('netMBFormat', disp.netMB);
var _co = $('#clockOffset'); if (_co) _co.dispatchEvent(new Event('input'));  // refresh the range-val label
}
function applyMetrics(data, full, initial) {
if (data.time) DEVTIME = data.time;
if (initial) setDisplayControls(data.display);
if (full) {
metricsData = (data.metrics && data.metrics.length) ? data.metrics : [];
renderMetrics(); buildDropCells(); buildChipTray();
} else if (data.metrics) {
data.metrics.forEach(function (d) { var m = byId(d.id); if (m) m.value = d.value; });
}
renderFrame();
}
function reloadMetrics(initial) {
return fetch('/metrics').then(function (r) { return r.json(); })
.then(function (data) { applyMetrics(data, true, !!initial); })
.catch(function () { var l = $('#metricsList'); if (l) l.innerHTML = '<p class="field-hint">Could not load metrics.</p>'; });
}
function pollMetrics() {
fetch('/metrics').then(function (r) { return r.json(); })
.then(function (data) { applyMetrics(data, false, false); }).catch(function () {});
}
reloadMetrics(true);
setInterval(pollMetrics, 1500);
form.addEventListener('submit', function (e) {
e.preventDefault();
saveFormState();
var btn = $('#saveBtn'); var orig = btn.textContent;
btn.disabled = true; btn.textContent = 'Saving...';
var body = new URLSearchParams(new FormData(form));
fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); })
.then(function (d) {
btn.disabled = false; btn.textContent = orig;
if (d.success) {
markClean('Saved');
if (d.networkChanged) {
alert('Network settings changed. The device is restarting - you may need to reconnect at the new IP address.');
setTimeout(function () { window.location.href = '/'; }, 3000);
}
} else { alert('Error saving settings.'); }
})
.catch(function (err) { btn.disabled = false; btn.textContent = orig; alert('Error saving settings: ' + err); });
});
// pywebview bridge (present only inside the native window); browser uses fallbacks.
function nativeApi() { return (window.pywebview && window.pywebview.api) ? window.pywebview.api : null; }
function browserDownload(text, name) {
var blob = new Blob([text], { type: 'application/json' });
var url = URL.createObjectURL(blob);
var a = document.createElement('a'); a.href = url; a.download = name;
document.body.appendChild(a); a.click(); document.body.removeChild(a); URL.revokeObjectURL(url);
}
$('#exportBtn').addEventListener('click', function () {
fetch('/api/export').then(function (r) { return r.json(); }).then(function (data) {
var text = JSON.stringify(data, null, 2);
var api = nativeApi();
// A Blob <a download> is a no-op in the embedded WebView2, so use the native
// save dialog when running in the pywebview window.
if (api && api.save_text) {
api.save_text(text, 'pcmonitor-config.json').then(function (r) {
if (r && r.ok) markClean('Exported to ' + r.path);
else if (r && r.error) alert('Could not save: ' + r.error);
});
} else {
browserDownload(text, 'pcmonitor-config.json');
}
}).catch(function (err) { alert('Error exporting configuration: ' + err); });
});
function doImport(cfgText) {
var cfg;
try { cfg = JSON.parse(cfgText); } catch (err) { alert('Invalid configuration file: ' + err); return; }
fetch('/api/import', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cfg) })
.then(function (r) { return r.json(); })
.then(function (d) {
if (d.success) { alert('Configuration imported. Reloading...'); location.reload(); }
else { alert('Error importing configuration: ' + d.message); }
})
.catch(function (err) { alert('Error importing configuration: ' + err); });
}
$('#importBtn').addEventListener('click', function () {
var api = nativeApi();
if (api && api.open_text) {
api.open_text().then(function (r) { if (r && r.ok) doImport(r.text); else if (r && r.error) alert('Could not open: ' + r.error); });
} else { $('#importFile').click(); }
});
$('#importFile').addEventListener('change', function (ev) {
var file = ev.target.files[0]; if (!file) return;
var reader = new FileReader();
reader.onload = function (e) { doImport(e.target.result); };
reader.readAsText(file);
});
// ---- PC companion: revert unsaved changes -------------------------------
var revertBtn = $('#revertBtn');
if (revertBtn) revertBtn.addEventListener('click', function () {
if (!confirm('Discard unsaved changes and reload the last saved configuration?')) return;
fetch('/api/revert', { method: 'POST' }).then(function (r) { return r.json(); }).then(function (d) {
if (d.success) reloadMetrics(true).then(function () { loadSensors(false); markClean('Reverted to saved'); refreshStatus(); });
}).catch(function (err) { alert('Revert failed: ' + err); });
});

// ---- PC companion: pull layout from device ------------------------------
var pullBtn = $('#pullBtn');
if (pullBtn) pullBtn.addEventListener('click', function () {
var orig = pullBtn.textContent; pullBtn.disabled = true; pullBtn.textContent = 'Pulling...';
var res = $('#pullResult'); if (res) res.textContent = '';
fetch('/api/pull', { method: 'POST' }).then(function (r) { return r.json(); }).then(function (d) {
pullBtn.disabled = false; pullBtn.textContent = orig;
if (res) res.textContent = d.message || '';
if (d.success) reloadMetrics(true).then(function () { markClean('Pulled from device'); });
}).catch(function (err) { pullBtn.disabled = false; pullBtn.textContent = orig; if (res) res.textContent = 'Error: ' + err; });
});

// ---- PC companion: quick layout templates -------------------------------
var applyTemplateBtn = $('#applyTemplateBtn');
if (applyTemplateBtn) applyTemplateBtn.addEventListener('click', function () {
var key = (($('#templateSel') || {}).value) || 'compact';
var body = new URLSearchParams(); body.set('key', key);
applyTemplateBtn.disabled = true;
fetch('/api/template', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); }).then(function (d) {
applyTemplateBtn.disabled = false;
if (d.success) reloadMetrics(true).then(function () { markDirty(); });
var res = $('#pullResult'); if (res) res.textContent = d.message || '';
}).catch(function (err) { applyTemplateBtn.disabled = false; var res = $('#pullResult'); if (res) res.textContent = 'Error: ' + err; });
});

// ---- PC companion: status readout (sidebar CRT panel) -------------------
function setText(id, v) { var e = $('#' + id); if (e) e.textContent = v; }
function refreshStatus() {
fetch('/api/status').then(function (r) { return r.json(); }).then(function (d) {
var led = $('#srLed'), title = $('#srTitle');
if (led) { led.classList.toggle('online', !!d.deviceReachable); led.classList.toggle('offline', !d.deviceReachable); }
if (title) title.textContent = (d.deviceReachable ? 'Device online' : 'Device offline') + ' · ' + (d.monitoring ? 'sending' : 'idle');
setText('srDevice', d.deviceIp || '-');
setText('srSource', d.source || '-');
setText('srCount', (d.metricCount || 0) + ' metric' + (d.metricCount === 1 ? '' : 's'));
}).catch(function () {});
}

// ---- PC companion: connection settings ----------------------------------
// Fill the connection fields from the saved config so they reflect what is
// actually in use (otherwise they show the static HTML defaults on every open).
function hydrateConnection() {
fetch('/api/info').then(function (r) { return r.json(); }).then(function (d) {
var ip = $('#esp32_ip'); if (ip && d.ip) ip.value = d.ip;
var port = $('#udp_port'); if (port && d.udp_port != null) port.value = d.udp_port;
var iv = $('#update_interval'); if (iv && d.update_interval != null) iv.value = d.update_interval;
}).catch(function () {});
}
var connResult = $('#connResult');
function setConnResult(msg, ok) {
if (!connResult) return;
connResult.textContent = msg;
connResult.className = 'note ' + (ok ? '' : 'warn');
connResult.style.display = msg ? '' : 'none';
}
var saveConnBtn = $('#saveConnBtn');
if (saveConnBtn) saveConnBtn.addEventListener('click', function () {
var body = new URLSearchParams();
body.set('esp32_ip', ($('#esp32_ip') || {}).value || '');
body.set('udp_port', ($('#udp_port') || {}).value || '');
body.set('update_interval', ($('#update_interval') || {}).value || '');
saveConnBtn.disabled = true;
fetch('/api/connection', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); }).then(function (d) {
saveConnBtn.disabled = false;
if (d.success) { markClean('Connection saved'); setConnResult('Saved. Device set to ' + d.esp32_ip + ':' + d.udp_port + ', every ' + d.update_interval + 's.', true); refreshStatus(); }
else { setConnResult(d.message || 'Could not save connection settings.', false); }
}).catch(function (err) { saveConnBtn.disabled = false; setConnResult('Error: ' + err, false); });
});
var testConnBtn = $('#testConnBtn');
if (testConnBtn) testConnBtn.addEventListener('click', function () {
var ip = ($('#esp32_ip') || {}).value || '';
if (!ip) { setConnResult('Enter the device IP first.', false); return; }
var orig = testConnBtn.textContent; testConnBtn.disabled = true; testConnBtn.textContent = 'Testing...';
var body = new URLSearchParams(); body.set('esp32_ip', ip); body.set('udp_port', ($('#udp_port') || {}).value || '');
fetch('/api/test', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); }).then(function (d) {
testConnBtn.disabled = false; testConnBtn.textContent = orig;
setConnResult(d.message || (d.reachable ? 'Reachable.' : 'Not reachable.'), !!d.reachable);
}).catch(function (err) { testConnBtn.disabled = false; testConnBtn.textContent = orig; setConnResult('Error: ' + err, false); });
});

// ---- PC companion: Windows autostart ------------------------------------
var autoChk = $('#autostartChk');
function setAutostartState(enabled) {
var t = $('#autostartState'); if (t) t.textContent = enabled ? 'Enabled - saved' : 'Disabled';
}
function refreshAutostart() {
fetch('/api/autostart').then(function (r) { return r.json(); }).then(function (d) {
if (autoChk) autoChk.checked = !!d.enabled; setAutostartState(!!d.enabled);
}).catch(function () {});
}
if (autoChk) autoChk.addEventListener('change', function () {
// Saves itself immediately (writes the HKCU Run key) - no "Save & push" needed.
var body = new URLSearchParams(); body.set('enable', autoChk.checked ? '1' : '0');
autoChk.disabled = true; var _as = $('#autostartState'); if (_as) _as.textContent = 'Saving...';
fetch('/api/autostart', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); }).then(function (d) { autoChk.disabled = false; autoChk.checked = !!d.enabled; setAutostartState(!!d.enabled); })
.catch(function () { autoChk.disabled = false; refreshAutostart(); });
});

// ---- PC companion: sensor source + selection ----------------------------
var SENSORS = [], MAXSEL = (CFG.maxMetrics || 20), selOrder = [];
var CAT_LABELS = { system: 'System (CPU / RAM / Disk)', gpu: 'GPU', temperature: 'Temperatures', fan: 'Fans', load: 'Loads', clock: 'Clocks', power: 'Power', data: 'Data', throughput: 'Network throughput', other: 'Other' };
function selectedKeys() { return selOrder.slice(); }
function updateSensorCount() {
var n = selOrder.length;
setText('sensorCount', 'Selected: ' + n + ' / ' + MAXSEL);
$$('#sensorList input[type="checkbox"]').forEach(function (cb) {
if (!cb.checked) cb.disabled = (n >= MAXSEL);
});
}
function nameForKey(key) { for (var i = 0; i < SENSORS.length; i++) if (SENSORS[i].key === key) return SENSORS[i].display_name; return key; }
function renderSelectedTray() {
var host = $('#selectedTray'); if (!host) return;
host.innerHTML = '';
if (!selOrder.length) { host.innerHTML = '<span class="chip-empty">No sensors selected yet - tick some below.</span>'; return; }
selOrder.forEach(function (key) {
var s = null; for (var i = 0; i < SENSORS.length; i++) if (SENSORS[i].key === key) { s = SENSORS[i]; break; }
var onscreen = !!(s && s.placed);
var chip = document.createElement('div'); chip.className = 'chip ' + (onscreen ? 'onscreen' : 'sel');
chip.title = (onscreen ? 'On the device screen' : 'Sent but not placed on screen yet') + ' - click to remove';
chip.innerHTML = '<span class="cn">' + esc(nameForKey(key)) + '</span><span class="cb">remove</span>';
chip.addEventListener('click', function () {
var i = selOrder.indexOf(key); if (i >= 0) selOrder.splice(i, 1);
syncCheckboxes(); updateSensorCount(); renderSelectedTray(); postSelection();
});
host.appendChild(chip);
});
}
function syncCheckboxes() {
$$('#sensorList .check-row').forEach(function (row) {
var cb = row.querySelector('input[type="checkbox"]'); if (!cb) return;
cb.checked = selOrder.indexOf(row.dataset.key) >= 0;
});
updateSensorCount();
}
function postSelection() {
return fetch('/api/select', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ keys: selectedKeys() }) })
.then(function (r) { return r.json(); })
.then(function () { return reloadMetrics(); })
.then(function () { markDirty(); refreshStatus(); });  // deferred - persists on Save & push
}
function applySensorFilter() {
var q = (($('#sensorSearch') || {}).value || '').toLowerCase();
$$('#sensorList .check-row').forEach(function (row) {
var hay = row.dataset.search || '';
row.style.display = (!q || hay.indexOf(q) >= 0) ? '' : 'none';
});
$$('#sensorList .sensor-cat').forEach(function (h) {
var any = false, el = h.nextElementSibling;
while (el && el.classList.contains('check-row')) { if (el.style.display !== 'none') { any = true; break; } el = el.nextElementSibling; }
h.style.display = any ? '' : 'none';
});
}
function renderSensors(data) {
SENSORS = data.sensors || [];
MAXSEL = data.max || 20;
selOrder = SENSORS.filter(function (s) { return s.selected; }).map(function (s) { return s.key; });
var banner = $('#sourceBanner');
if (banner && data.banner) { banner.textContent = data.banner.text; banner.className = 'note ' + (data.banner.level === 'ok' ? '' : data.banner.level === 'warn' ? 'warn' : 'warn'); }
var list = $('#sensorList'); if (!list) return;
list.innerHTML = '';
if (!SENSORS.length) { list.innerHTML = '<p class="field-hint">No sensors discovered yet. Start LibreHardwareMonitor and click Rescan.</p>'; updateSensorCount(); return; }
var order = ['system', 'gpu', 'temperature', 'fan', 'load', 'clock', 'power', 'data', 'throughput', 'other'];
var byCat = {}; SENSORS.forEach(function (s) { (byCat[s.category] = byCat[s.category] || []).push(s); });
order.forEach(function (cat) {
var arr = byCat[cat]; if (!arr || !arr.length) return;
var h = document.createElement('div'); h.className = 'sensor-cat field-label'; h.textContent = CAT_LABELS[cat] || cat;
h.style.marginTop = '14px'; list.appendChild(h);
arr.forEach(function (s) {
var row = document.createElement('label'); row.className = 'check-row';
row.dataset.search = (s.display_name + ' ' + s.name + ' ' + s.unit).toLowerCase();
row.dataset.key = s.key;
var val = (s.current_value != null) ? (' · ' + s.current_value + (s.unit || '')) : '';
row.innerHTML = '<input type="checkbox"' + (s.selected ? ' checked' : '') + '><span class="check-box"></span>' +
'<span class="check-text"><strong>' + esc(s.display_name) + '</strong>' +
'<span class="ct-hint">' + esc(s.name) + ' · ' + esc(s.unit || '') + ' · ' + esc(s.source) + val + '</span></span>';
var cb = row.querySelector('input');
cb.addEventListener('change', function () {
if (cb.checked) { if (selOrder.indexOf(s.key) < 0) selOrder.push(s.key); }
else { var i = selOrder.indexOf(s.key); if (i >= 0) selOrder.splice(i, 1); }
if (selOrder.length > MAXSEL) { selOrder.pop(); cb.checked = false; alert('Maximum ' + MAXSEL + ' metrics.'); return; }
updateSensorCount();
renderSelectedTray();
postSelection();
});
list.appendChild(row);
});
});
applySensorFilter();
updateSensorCount();
renderSelectedTray();
}
function loadSensors(rescan) {
var banner = $('#sourceBanner'); if (banner && rescan) { banner.textContent = 'Rescanning sensors...'; banner.className = 'note'; }
return fetch('/api/sensors' + (rescan ? '?rescan=1' : '')).then(function (r) { return r.json(); })
.then(renderSensors).catch(function () { if (banner) { banner.textContent = 'Could not read sensors.'; banner.className = 'note warn'; } });
}
var rescanBtn = $('#rescanBtn');
if (rescanBtn) rescanBtn.addEventListener('click', function () { rescanBtn.disabled = true; loadSensors(true).then(function () { rescanBtn.disabled = false; refreshStatus(); }); });
var sensorSearch = $('#sensorSearch');
if (sensorSearch) sensorSearch.addEventListener('input', applySensorFilter);
// Re-read sensors when the Sensors tab is opened so the green 'on-screen' chips
// reflect layout changes made since (cheap - discovery is skipped if populated).
var sensorsNav = document.querySelector('.nav-item[data-nav="sensors"]');
if (sensorsNav) sensorsNav.addEventListener('click', function () { loadSensors(false); });

hydrateConnection();
refreshStatus();
refreshAutostart();
loadSensors(false);
setInterval(refreshStatus, 5000);
})();
