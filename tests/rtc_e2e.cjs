// The WebRTC transport against real browsers: Chromium and Firefox open the two
// data channels to the service (build/web-rtc-host-test, the PS4 stubbed) and talk
// to it. The production-only HTTP surface refuses WebSocket. The page is
// opened by the container's own address, not 127.0.0.1:
// browsers leave loopback out of WebRTC. Run in the Playwright Docker image:
//   NODE_PATH=… node tests/rtc_e2e.cjs
const pw = require('playwright');
const fs = require('node:fs'), os = require('node:os'), assert = require('node:assert/strict'), { spawn } = require('node:child_process');

const address = Object.values(os.networkInterfaces()).flat().find(i => i.family === 'IPv4' && !i.internal).address;
fs.copyFileSync('/src/build/web-rtc-host-test', '/tmp/c4f-rtc-service');
fs.chmodSync('/tmp/c4f-rtc-service', 0o755);

let server, log = '';
function startService(env = {}) {
  server = spawn('/tmp/c4f-rtc-service', [], { env: { ...process.env, C4F_TEST_KLOG_FD: '0', ...env }, stdio: ['pipe', 'pipe', 'pipe'] });
  server.stderr.on('data', d => { log += d; });
  server.stdout.on('data', d => { log += d; });
}
async function stopService() {
  const old = server;
  if (!old) return;
  server = null;
  old.kill();
  await new Promise(done => old.exitCode !== null || old.signalCode !== null ? done() : old.once('exit', done));
}

// In the page: an offer to POST /rtc, the two negotiated channels, then a few
// round trips on each. Returns what happened.
async function talk(page, { echoes = 20 } = {}) {
  return page.evaluate(async ({ echoes }) => {
    const pc = new RTCPeerConnection();
    try {
    const control = pc.createDataChannel('control', { negotiated: true, id: 0 });
    const input = pc.createDataChannel('input', { negotiated: true, id: 1, ordered: false, maxRetransmits: 0 });
    const pushed = [];
    const pending = new Map();
    control.onmessage = ev => {
      const m = JSON.parse(ev.data);
      if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } else pushed.push(m);
    };
    const started = performance.now();
    await pc.setLocalDescription(await pc.createOffer());
    const reply = await fetch('/rtc', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: pc.localDescription.sdp });
    if (!reply.ok) return { error: 'offer refused: ' + reply.status + ' ' + await reply.text() };
    const answer = await reply.text();
    await pc.setRemoteDescription({ type: 'answer', sdp: answer });
    const open = ch => new Promise((done, fail) => {
      if (ch.readyState === 'open') return done();
      ch.onopen = done; ch.onerror = fail;
      setTimeout(() => fail(new Error(ch.label + ' did not open: ' + pc.connectionState + '/' + pc.iceConnectionState)), 10000);
    });
    try { await Promise.all([open(control), open(input)]); } catch (e) { return { error: e.message, answer }; }
    const openedMs = performance.now() - started;
    const call = (id, method, params = []) => new Promise((done, fail) => {
      const timer = setTimeout(() => { pending.delete(id); fail(new Error(method + ' reply timed out')); }, 5000);
      pending.set(id, value => { clearTimeout(timer); done(value); });
      control.send(JSON.stringify({ id, method, params }));
    });
    const info = await call(1, 'info');
    // The service's first push is the status, sent the moment the channel opened.
    await new Promise(done => setTimeout(done, 200));
    const status = pushed.find(m => m.method === 's');
    // Echoes on the unordered channel.
    const back = new Map();
    input.onmessage = ev => { const m = JSON.parse(ev.data); if (m.method === 'e') back.set(m.params[0], performance.now() - m.params[1]); };
    for (let i = 1; i <= echoes; i++) { input.send(JSON.stringify({ method: 'e', params: [i, Math.round(performance.now())] })); await new Promise(d => setTimeout(d, 20)); }
    await new Promise(done => setTimeout(done, 500));
    // A big reply (the invite QR code) comes in pieces over the reliable channel.
    const invite = await call(2, 'invite', []);
    return { openedMs, info: info.result, status: status && status.params, echoes: back.size, invite: invite.result || invite.error };
    } finally { pc.close(); }
  }, { echoes });
}

(async () => {
  startService();
  try {
  const results = {};
  for (const name of ['chromium', 'firefox']) {
    const browser = await pw[name].launch(name === 'firefox' ? { firefoxUserPrefs: { 'media.peerconnection.ice.loopback': true } } : {});
    try {
    const page = await browser.newPage();
    page.on('pageerror', e => console.error(name, 'page error:', e.message));
    for (let tries = 0; ; tries++) {
      try { await page.goto(`http://${address}:4264/`); break; } catch (e) { if (tries > 50) throw e; await new Promise(d => setTimeout(d, 100)); }
    }
    const r = await talk(page);
    results[name] = r;
    if (r.error) { console.error(log); throw new Error(name + ': ' + r.error + '\n' + (r.answer || '')); }
    assert.equal(r.info.protocol, 3, name);
    assert.equal(r.info.input, true, name);
    assert.ok(r.status && r.status.pads.length === 4, name + ': status pushed on open');
    assert.equal(r.echoes, 20, name + ': healthy-link echoes');
    assert.ok(r.invite && r.invite.size > 20, name + ': invite ' + JSON.stringify(r.invite));
    console.log(`PASS rtc ${name}: open in ${r.openedMs.toFixed(0)} ms, info, status push, ${r.echoes}/20 echoes, big reply in pieces`);
    } finally { await browser.close(); }
  }
  if (!/rtc: peer \d+ open/.test(log)) { console.error(log); throw new Error('the service did not log an open peer'); }
  } finally { await stopService(); }
})().catch(e => { console.error(e); process.exit(1); });
