const el = id => document.getElementById(id);
const masks = {KeyW:2, ArrowUp:2, KeyS:128, ArrowDown:128, KeyA:4, ArrowLeft:4,
               KeyD:8, ArrowRight:8, Space:1, ControlLeft:1, ControlRight:1, KeyE:16, Escape:32, Enter:64};
const keys = new Set(), pointers = new Map();
let token, connected = false, armed = false, generation, frame;
let queued = Promise.resolve();
let preferredSize = '60';

function post(route, data) {
  return fetch(`/api/${route}`, {method:'POST', headers:{'X-APDO-Token':token, 'Content-Type':'application/json'},
                                body:JSON.stringify(data), keepalive:true}).then(response => {
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
  });
}
function send(value) {
  if (!token) return;
  queued = queued.catch(() => {}).then(() => post('keys', value));
  queued.catch(error => { el('status').textContent = error.message; });
}
function update() {
  let value = 0;
  for (const key of keys) value |= masks[key];
  for (const mask of pointers.values()) value |= mask;
  send(value);
}
function release() {
  keys.clear(); pointers.clear(); armed = false;
  el('enable').textContent = 'キーボードを有効にする';
  send(null);
}
el('enable').onclick = () => {
  if (connected) { armed = true; el('enable').textContent = 'キーボード有効'; }
};
el('release').onclick = release;
el('preview').onchange = async () => {
  try { await post('preview', el('preview').checked); }
  catch (error) { el('status').textContent = error.message; }
};
el('size').onchange = async () => {
  try {
    await post('size', Number(el('size').value));
    preferredSize = el('size').value;
  } catch (error) { el('status').textContent = error.message; }
};
window.addEventListener('keydown', event => {
  if (!armed || !masks[event.code]) return;
  event.preventDefault();
  if (!event.repeat) { keys.add(event.code); update(); }
});
window.addEventListener('keyup', event => {
  if (!keys.delete(event.code)) return;
  event.preventDefault(); update();
});
window.addEventListener('blur', release);
window.addEventListener('pagehide', release);
document.addEventListener('visibilitychange', () => { if (document.hidden) release(); });
for (const button of document.querySelectorAll('[data-mask]')) {
  button.addEventListener('pointerdown', event => {
    if (!connected) return;
    button.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, Number(button.dataset.mask));
    update();
  });
  for (const event of ['pointerup','pointercancel','lostpointercapture']) {
    button.addEventListener(event, e => { if (pointers.delete(e.pointerId)) update(); });
  }
}
setInterval(() => { if (keys.size || pointers.size) update(); }, 100);
async function status() {
  try {
    const state = await (await fetch('/api/status')).json();
    if ((connected && !state.connected) || (generation !== undefined && generation !== state.generation)) release();
    generation = state.generation;
    connected = state.connected;
    el('enable').disabled = !connected;
    el('status').textContent = connected ? state.product + ' 接続済み' : state.error;
    el('size').disabled = !connected || state.product !== 'T3P DOOM V66';
    if (el('size').disabled && connected) el('size').value = '120';
    else if (!el('size').disabled && document.activeElement !== el('size')) el('size').value = preferredSize;
    const show = el('preview').checked && state.frame !== null;
    if (show && state.frame !== frame) el('screen').src = `/api/frame.bmp?id=${state.frame}`;
    frame = show ? state.frame : null;
    el('screen').hidden = !show;
    el('save').hidden = !show;
    el('cadence').hidden = !show || state.capture_ms === null;
    el('cadence').textContent = (state.fps === null ? '' : state.fps.toFixed(1) + ' fps · ')
      + '取得 ' + state.capture_ms + ' ms';
    el('waiting').hidden = show;
    el('waiting').textContent = el('preview').checked ? 'キャプチャ待ち' : 'プレビュー停止中';
  } catch (error) {
    if (connected) release();
    connected = false;
    el('enable').disabled = el('size').disabled = true;
    el('status').textContent = error.message;
  } finally { setTimeout(status, el('preview').checked ? 50 : 250); }
}
fetch('/api/session').then(response => response.json()).then(session => {
  token = session.token;
  if (session.shell) {
    const shell = document.querySelector('.t3p-case');
    shell.src = '/t3p.png';
    shell.hidden = false;
  }
  return post('size', Number(preferredSize)).then(() => post('preview', el('preview').checked));
}).then(status).catch(error => { el('status').textContent = error.message; });
