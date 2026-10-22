// Real production RTC application behavior through a controllable local UDP
// relay. All sockets stay inside the Docker test container; no console access.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const dgram = require('node:dgram');
const { spawn } = require('node:child_process');
const { address, copyService, relayOffer } = require('./rtc_browser_helpers.cjs');
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function relay() {
  const down = dgram.createSocket('udp4'), up = dgram.createSocket('udp4');
  await Promise.all([new Promise(resolve => down.bind(0, '0.0.0.0', resolve)),
    new Promise(resolve => up.bind(0, '0.0.0.0', resolve))]);
  let client, mode = 'pass';
  const held = [];
  function forward(socket, data, port, host) {
    if (mode === 'drop') return;
    const deliver = () => socket.send(data, port, host);
    if (mode === 'hold') held.push(deliver); else deliver();
  }
  down.on('message', (data, from) => {
    client = from;
    forward(up, Buffer.from(data), 4264, address);
  });
  up.on('message', data => { if (client) forward(down, Buffer.from(data), client.port, client.address); });
  return { port: down.address().port,
    mode(value) { mode = value; },
    release() { for (const deliver of held.splice(0)) deliver(); },
    async close() { held.length = 0; await Promise.all([new Promise(r => down.close(r)), new Promise(r => up.close(r))]); }
  };
}

async function connect(page, port) {
  return page.evaluate(async port => {
    const pc = new RTCPeerConnection({ iceServers: [] });
    const control = pc.createDataChannel('control', { negotiated: true, id: 0 });
    const input = pc.createDataChannel('input', { negotiated: true, id: 1, ordered: false, maxRetransmits: 0 });
    const pending = new Map(), pushes = [];
    let nextId = 1;
    control.onmessage = event => {
      const msg = JSON.parse(event.data), request = pending.get(msg.id);
      if (request) { clearTimeout(request.timer); pending.delete(msg.id); request.resolve(msg); }
      else pushes.push(msg);
    };
    input.onmessage = event => pushes.push(JSON.parse(event.data));
    function request(method, params = [], onInput = false) {
      return new Promise((resolve, reject) => {
        const id = nextId++;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error('Request timed out: ' + method)); }, 5000);
        pending.set(id, { resolve, timer });
        (onInput ? input : control).send(JSON.stringify({ id, method, params }));
      });
    }
    await pc.setLocalDescription(await pc.createOffer());
    const response = await fetch('/rtc', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: pc.localDescription.sdp });
    if (!response.ok) throw new Error('Offer refused: ' + response.status);
    let answer = await response.text();
    if (port) answer = answer.replace(/(a=candidate:\S+ \d+ \S+ \d+ \S+ )\d+/g, (_match, prefix) => prefix + port);
    await pc.setRemoteDescription({ type: 'answer', sdp: answer });
    await Promise.all([control, input].map(channel => new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Channel did not open')), 10000);
      if (channel.readyState === 'open') { clearTimeout(timer); resolve(); }
      else channel.onopen = () => { clearTimeout(timer); resolve(); };
    })));
    window.rtcInputTest = { pc, control, input, pushes, request,
      state(seq, buttons = 0, lx = 128, history = []) {
        input.send(JSON.stringify({ method: 'i', params: [0, seq, buttons, lx, 128, 128, 128, 0, 0, 0, history.length, ...history.flat()] }));
      },
      motion(seq, acceleration) {
        input.send(JSON.stringify({ method: 'm', params: [0, seq, 0, 0, 0, 10000, acceleration, 0, 0, 0, 0, 0] }));
      }
    };
    return request('info');
  }, port);
}

(async () => {
  let service, browser, proxy;
  let trace = '', partial = '';
  const frames = [];
  const frameAt = (buttons, lx = undefined) => rows => rows.some(row => +row[3] === buttons && (lx === undefined || +row[4] === lx));
  async function waitFrames(start, condition, timeout = 2500) {
    const deadline = Date.now() + timeout;
    while (!condition(frames.slice(start))) {
      if (Date.now() > deadline) throw new Error('Expected controller report not seen\n' + trace.slice(-5000));
      await sleep(10);
    }
  }
  try {
    if (!address) throw new Error('Run the browser checks with a Docker network interface; --network none has only loopback');
    copyService('/tmp/c4f-rtc-input-service');
    service = spawn('/tmp/c4f-rtc-input-service', [], { env: { ...process.env, C4F_TEST_KLOG_FD: '0' }, stdio: ['pipe', 'pipe', 'pipe'] });
    service.stdout.on('data', data => {
      trace += data; partial += data;
      const lines = partial.split('\n'); partial = lines.pop();
      for (const line of lines) if (line.startsWith('FRAME ')) frames.push(line.split(' '));
    });
    service.stderr.on('data', data => { trace += data; });
    browser = await chromium.launch();
    const context = await browser.newContext();
    await context.grantPermissions(['local-network-access']);
    const page = await context.newPage();
    await relayOffer(page);
    // Supply an empty same-origin document: this test owns the RTC peers.
    await page.route('http://127.0.0.1:4264/', async route => route.fulfill({ response: await route.fetch(), body: '<!doctype html><title>RTC input test</title>' }));
    await page.goto('http://127.0.0.1:4264/');
    for (let tries = 0; ; tries++) {
      try { if ((await page.request.get(`http://${address}:4264/`)).ok()) break; }
      catch (error) { if (tries > 50) throw error; }
      await sleep(100);
    }
    proxy = await relay();
    const info = await connect(page, proxy.port);
    assert.equal(info.result.protocol, 3);
    const claim = await page.evaluate(() => rtcInputTest.request('claim', [0]));
    assert.equal(claim.result.pads[0].mine, true);
    let start = frames.length;
    await page.evaluate(() => rtcInputTest.state(1));
    await waitFrames(start, frameAt(0));

    // The press and release packets were absent, but their bounded history
    // repairs a quick tap. Analog state is current, not reconstructed history.
    start = frames.length;
    await page.evaluate(() => rtcInputTest.state(4, 0, 17, [[2, 0x4000], [3, 0]]));
    await waitFrames(start, rows => frameAt(0x4000, 17)(rows) && frameAt(0, 17)(rows));
    await page.evaluate(() => { rtcInputTest.state(3, 0x2000); rtcInputTest.state(4, 0x2000); });
    await sleep(80);
    assert.ok(frames.slice(start).every(row => +row[3] !== 0x2000), 'late or duplicate state restored an old button');

    const wrongControl = await page.evaluate(() => rtcInputTest.request('ping', [], true));
    assert.equal(wrongControl.error.code, 400);
    const wrongInput = await page.evaluate(() => rtcInputTest.request('i', [0, 5, 0, 128, 128, 128, 128, 0, 0, 0, 0]));
    assert.equal(wrongInput.error.code, 400);
    const malformedHistory = await page.evaluate(() => rtcInputTest.request('i', [0, 5, 0, 128, 128, 128, 128, 0, 0, 0, 2, 4, 0, 3, 0], true));
    assert.equal(malformedHistory.error.code, 400);
    console.log('PASS RTC input: explicit claim, recovered tap, ordered history, duplicate/late rejection, channel separation');

    start = frames.length;
    await page.evaluate(() => { rtcInputTest.motion(10, 1000); rtcInputTest.motion(9, -1000); rtcInputTest.state(5, 0, 33); });
    await waitFrames(start, rows => rows.some(row => +row[4] === 33 && +row[15] === 1));
    assert.ok(frames.slice(start).every(row => +row[15] !== -1), 'old motion overtook new motion');

    // A genuinely held datagram can arrive after a newer one. No retransmit
    // setting can remove it from this queue; receiver sequences must reject it.
    proxy.mode('hold');
    await page.evaluate(() => rtcInputTest.state(6, 0x2000));
    await sleep(100);
    proxy.mode('pass');
    start = frames.length;
    await page.evaluate(() => rtcInputTest.state(7, 0, 44));
    await waitFrames(start, frameAt(0, 44));
    proxy.release();
    await sleep(100);
    assert.ok(frames.slice(start).every(row => +row[3] !== 0x2000), 'held old datagram regressed input');

    // Drop both directions for 900ms, including several full-state messages.
    // Afterward repeat recent events and exact final state. Timing lost during
    // the outage is not claimed to be recoverable.
    proxy.mode('drop');
    start = frames.length;
    await page.evaluate(() => { rtcInputTest.state(8, 0x4000); rtcInputTest.state(9, 0); });
    await sleep(900);
    assert.ok(frames.slice(start).every(row => +row[3] === 0), 'dropped traffic reached the service');
    proxy.mode('pass');
    await page.evaluate(() => rtcInputTest.state(10, 0, 55, [[8, 0x4000], [9, 0]]));
    await waitFrames(start, rows => frameAt(0x4000, 55)(rows) && frameAt(0, 55)(rows));
    console.log('PASS RTC impairment: stale held datagram rejected, 900ms bidirectional loss, bounded tap recovery and exact final state');

    // A stale reset must not forget its sequence floor. Motion by itself must
    // not keep an old held button alive.
    await page.evaluate(() => rtcInputTest.state(11, 0x20));
    await waitFrames(frames.length, frameAt(0x20));
    start = frames.length;
    for (let i = 11; i < 24; i++) {
      await page.evaluate(seq => rtcInputTest.motion(seq, 0), i);
      await sleep(250);
    }
    await waitFrames(start, frameAt(0));
    const status = await page.evaluate(() => rtcInputTest.request('status'));
    assert.equal(status.result.pads[0].state, 'paused');
    start = frames.length;
    await page.evaluate(() => rtcInputTest.state(10, 0x4000));
    await sleep(100);
    assert.ok(frames.slice(start).every(row => +row[3] === 0), 'stale reset discarded sequence floor');
    await page.evaluate(() => rtcInputTest.state(12, 0));
    await waitFrames(start, frameAt(0));
    assert.equal((await page.evaluate(() => rtcInputTest.request('status'))).result.pads[0].state, 'select');
    console.log('PASS RTC freshness: motion cannot renew held buttons; stale reset preserves sequence floor; newer input resumes');

    await page.evaluate(() => rtcInputTest.pc.close());
  } finally {
    if (browser) await browser.close();
    if (proxy) await proxy.close();
    if (service) {
      service.kill();
      await new Promise(resolve => service.exitCode !== null || service.signalCode !== null ? resolve() : service.once('exit', resolve));
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
