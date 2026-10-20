// Settings → Experimental in a real browser, against the real service with the PS4
// stubbed (build/web-host-test). WebHID devices, gamepads and motion sensors are
// fakes that record what the page does. Test instrumentation is inserted into
// the page in memory, never shipped. Run in the Playwright Docker image:
//   NODE_PATH=… node tests/experimental_test.cjs
const { chromium } = require('playwright');
const fs = require('node:fs'), assert = require('node:assert/strict'), { spawn } = require('node:child_process');

const hook = '\nwindow.c4fTest = { experiments, hidLinks, setExperiment, hidReport, triggerEffect, phoneMotion, status, ' +
  'gamepadSources, setGamepadPad, openPad, closePad, openSettings, showPage, conn, rumble };\n';
const html = fs.readFileSync('/src/client/index.html', 'utf8').replace('\n  })();', hook + '  })();');
fs.copyFileSync('/src/build/web-host-test', '/tmp/c4f-experimental-service');

// An independent CRC-32 (zlib's), to check the Bluetooth reports' last four bytes.
function crc32(bytes) {
  let c = ~0;
  for (const b of bytes) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xedb88320 : c >>> 1;
  }
  return ~c >>> 0;
}
const le32 = (bytes, at) => (bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | bytes[at + 3] << 24) >>> 0;

// In the page before its own script: fake gamepads, WebHID and motion, and a
// record of the socket's messages and the page's sensor listeners.
function fakes({ insecure = false } = {}) {
  window.sentMessages = [];
  const socketSend = WebSocket.prototype.send;
  WebSocket.prototype.send = function(data) {
    try { sentMessages.push(JSON.parse(data)); } catch {}
    return socketSend.call(this, data);
  };
  window.motionListeners = 0;
  const addListener = window.addEventListener;
  window.addEventListener = function(type, ...rest) {
    if (type === 'devicemotion') motionListeners++;
    return addListener.call(this, type, ...rest);
  };
  window.effects = [];
  const pad = (index, id, actuator) => ({ index, id, mapping: 'standard', connected: true, timestamp: 0,
    buttons: Array.from({ length: 17 }, () => ({ value: 0, pressed: false })), axes: [0, 0, 0, 0], vibrationActuator: actuator });
  const xbox = { type: 'dual-rumble', effects: ['dual-rumble', 'trigger-rumble'],
    playEffect: async (type, params) => { effects.push([type, params]); return 'complete'; }, reset: async () => 'complete' };
  window.testPads = [
    pad(0, 'Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 09cc)'),
    pad(1, 'Xbox 360 Controller (XInput STANDARD GAMEPAD)', xbox),
    pad(2, 'DualSense Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 0ce6)'),
  ];
  Object.defineProperty(navigator, 'getGamepads', { value: () => testPads });
  if (insecure) {
    Object.defineProperty(window, 'isSecureContext', { value: false });
    delete Navigator.prototype.hid;
    delete window.DeviceMotionEvent;
    return;
  }
  class FakeDevice extends EventTarget {
    constructor(productId, outputs, features) {
      super();
      Object.assign(this, { vendorId: 0x054c, productId, productName: 'Fake', opened: false, closes: 0, sent: [], features,
        collections: [{ outputReports: outputs.map(reportId => ({ reportId })) }] });
    }
    async open() { this.opened = true; }
    async close() { this.opened = false; this.closes++; }
    async sendReport(id, data) { this.sent.push({ id, data: Array.from(data) }); }
    async receiveFeatureReport(id) {
      if (!this.features[id]) throw new DOMException('no such report', 'NotAllowedError');
      return new DataView(Uint8Array.from(this.features[id]).buffer);
    }
    input(reportId, bytes) {
      const ev = new Event('inputreport');
      Object.assign(ev, { reportId, data: new DataView(Uint8Array.from(bytes).buffer) });
      this.dispatchEvent(ev);
    }
  }
  // DS4 calibration (report 0x02 over USB, ID included): biases 0, every gyro axis
  // ±8192 for a speed of 1024 + 1024 (so 1/8 degree per second per step), and
  // the accelerometer ±8192 for ±1 G.
  const calibration = new Uint8Array(37);
  const put = (at, v) => { calibration[at] = v & 0xff; calibration[at + 1] = (v >> 8) & 0xff; };
  calibration[0] = 0x02;
  for (const at of [7, 11, 15]) put(at, 8192);
  for (const at of [9, 13, 17]) put(at, -8192);
  put(19, 1024); put(21, 1024);
  for (const at of [23, 27, 31]) put(at, 8192);
  for (const at of [25, 29, 33]) put(at, -8192);
  // DualSense firmware 0x20, update version 2.32 at byte 44.
  const firmware = new Uint8Array(64);
  firmware[0] = 0x20; firmware[44] = 0x32; firmware[45] = 0x02;
  window.fakeHid = {
    ds4: new FakeDevice(0x09cc, [0x05], { 0x02: Array.from(calibration) }),
    ds4bt: new FakeDevice(0x09cc, [0x11], {}),
    dualsense: new FakeDevice(0x0ce6, [0x02], { 0x20: Array.from(firmware) }),
    dualsensebt: new FakeDevice(0x0ce6, [0x31], { 0x20: Array.from(firmware) }),
    granted: [], next: null, calls: [],
  };
  Object.defineProperty(navigator, 'hid', { configurable: true, value: {
    requestDevice: async options => { fakeHid.calls.push(['requestDevice', options]); return fakeHid.next ? [fakeHid.next] : []; },
    getDevices: async () => { fakeHid.calls.push(['getDevices']); return fakeHid.granted; },
    addEventListener: type => { fakeHid.calls.push(['addEventListener', type]); },
  } });
}

// A fresh service for each scenario, so its controllers' handles start over:
// the first controller created is 11030d, the second 12030d.
let server = null;
async function startService() {
  if (server) {
    const old = server;
    old.kill();
    await new Promise(done => old.exitCode !== null || old.signalCode !== null ? done() : old.once('exit', done));
  }
  server = spawn('/tmp/c4f-experimental-service', [], { env: { ...process.env, C4F_TEST_KLOG_FD: '0' }, stdio: ['pipe', 'pipe', 'pipe'] });
  server.on('error', error => console.error('Service harness:', error));
  server.stderr.on('data', data => console.error(data.toString()));
  server.stdout.on('data', () => {});
}
const feedback = (handle, large, small, r = 0, g = 0, b = 0) => server.stdin.write(`C4F-TEST-FEEDBACK ${handle} ${large} ${small} ${r} ${g} ${b}\n`);

async function open(browser, options) {
  await startService();
  const context = await browser.newContext({ viewport: { width: 1280, height: 900 } });
  await context.grantPermissions(['local-network-access']);
  await context.addInitScript(fakes, options);
  const page = await context.newPage();
  page.on('pageerror', e => { console.error('Page error:', e.stack); process.exitCode = 1; });
  await page.route(/^http:\/\/127\.0\.0\.1:4264\/(?:\?.*)?$/, async r => r.fulfill({ response: await r.fetch(), body: html }));
  for (let tries = 0; ; tries++) {
    try { await page.goto('http://127.0.0.1:4264/'); break; } catch (e) {
      if (tries > 50) throw e;
      await new Promise(done => setTimeout(done, 100)); // the service is still starting
    }
  }
  await page.waitForFunction(() => window.c4fTest?.gamepadSources.size === 3);
  await page.waitForFunction(() => document.querySelector('#conn').dataset.conn === 'open' && c4fTest.conn.motion);
  return { context, page };
}

const lastReport = (page, device) => page.evaluate(d => fakeHid[d].sent.at(-1), device);

(async () => {
  let browser;
  try {
    browser = await chromium.launch({ headless: true });

    // Off by default: the tab is there, nothing new runs.
    let { context, page } = await open(browser);
    await page.evaluate(() => { c4fTest.openSettings('experimental'); });
    assert.deepEqual(await page.$$eval('#expRows .check', inputs => inputs.map(i => [i.dataset.experiment, i.checked, i.disabled])),
                     [['hid', false, false], ['triggers', false, false], ['motion', false, false]]);
    assert.equal(await page.locator('#expSony').isHidden(), true);
    assert.equal(await page.locator('#expMotion').isHidden(), true);
    await page.evaluate(() => { c4fTest.setGamepadPad(0, 0); c4fTest.setGamepadPad(1, 0); c4fTest.openPad(0); });
    await page.waitForFunction(() => c4fTest.status[0].mine);
    feedback('11030d', 200, 40);
    await page.waitForFunction(() => effects.length > 0);
    await page.waitForTimeout(300);
    assert.equal(await page.evaluate(() => fakeHid.calls.length), 0, 'WebHID was used while switched off');
    assert.equal(await page.evaluate(() => motionListeners), 0, 'motion sensors were listened to while switched off');
    assert.equal(await page.evaluate(() => sentMessages.some(m => m.method === 'm')), false, 'motion was sent while switched off');
    assert.equal(await page.evaluate(() => effects.every(([type]) => type === 'dual-rumble')), true);
    feedback('11030d', 0, 0);
    console.log('PASS experimental: three switches, all off; nothing of them runs while off');
    await context.close();

    // A page the browser treats as insecure: the WebHID and motion switches explain why they can't be used.
    ({ context, page } = await open(browser, { insecure: true }));
    await page.evaluate(() => c4fTest.openSettings('experimental'));
    assert.deepEqual(await page.$$eval('#expRows .check', inputs => inputs.map(i => i.disabled)), [true, false, true]);
    assert.equal(await page.locator('#expRows .blocked').count(), 2);
    assert.match(await page.locator('#expRows .blocked').first().textContent(), /insecure.*How to turn it on/);
    assert.match(await page.locator('#expRows .blocked a').first().getAttribute('href'), /troubleshooting\.md#experimental-features$/);
    console.log('PASS experimental: insecure page: WebHID and motion off with the reason and a link; trigger rumble available');
    await context.close();

    // Lights and rumble on a DualShock 4 by USB, then by Bluetooth.
    ({ context, page } = await open(browser));
    await page.evaluate(() => { c4fTest.setGamepadPad(0, 0); c4fTest.setGamepadPad(1, 0); c4fTest.setGamepadPad(2, 1); c4fTest.openSettings('experimental'); });
    await page.waitForFunction(() => c4fTest.status[0].mine && c4fTest.status[1].mine);
    await page.check('#expRows [data-experiment="hid"]');
    await page.waitForFunction(() => fakeHid.calls.some(([call]) => call === 'getDevices'));
    assert.equal(await page.locator('#expSony').isVisible(), true);
    assert.equal(await page.locator('#expPads [data-link]').count(), 2, 'both Sony gamepads listed, the Xbox one not');
    await page.evaluate(() => { fakeHid.next = fakeHid.ds4; });
    await page.click('#expPads [data-link="0"]');
    await page.waitForFunction(() => fakeHid.ds4.sent.length > 0);
    assert.deepEqual(await page.evaluate(() => fakeHid.calls.find(([c]) => c === 'requestDevice')[1]),
                     { filters: [{ vendorId: 0x054c, productId: 0x09cc }] });
    let report = await lastReport(page, 'ds4');
    assert.equal(report.id, 0x05);
    assert.equal(report.data.length, 31);
    assert.deepEqual(report.data.slice(0, 8), [0x03, 0, 0, 0, 0, 32, 96, 255], 'still, in controller 1\'s own colour');
    const effectsBefore = await page.evaluate(() => effects.length);
    feedback('11030d', 200, 40, 64, 0, 0);
    await page.waitForFunction(() => { const r = fakeHid.ds4.sent.at(-1).data; return r[3] === 40 && r[4] === 200 && r[5] === 255; });
    report = await lastReport(page, 'ds4');
    assert.deepEqual(report.data.slice(0, 8), [0x03, 0, 0, 40, 200, 255, 0, 0], 'small motor, large motor, the game\'s red');
    await page.waitForFunction(n => effects.length > n, effectsBefore);
    assert.equal(await page.evaluate(() => effects.every(([type]) => type === 'dual-rumble')), true, 'the Xbox pad keeps its own rumble');
    // Unchanged state is not sent again every frame.
    const count = await page.evaluate(() => fakeHid.ds4.sent.length);
    await page.waitForTimeout(200);
    assert.ok(await page.evaluate(() => fakeHid.ds4.sent.length) - count <= 1, 'unchanged reports were repeated');
    feedback('11030d', 0, 0, 64, 0, 0);
    await page.waitForFunction(() => fakeHid.ds4.sent.at(-1).data[4] === 0);
    // Unlinking leaves it still and dim, and closes it.
    await page.click('#expPads [data-link="0"]');
    await page.waitForFunction(() => fakeHid.ds4.closes === 1);
    assert.deepEqual((await lastReport(page, 'ds4')).data.slice(0, 8), [0x03, 0, 0, 0, 0, 0, 0, 64]);
    assert.equal(await page.evaluate(() => c4fTest.hidLinks.size), 0);
    // Bluetooth: report 0x11, the CRC over 0xa2, the ID and the rest.
    await page.evaluate(() => { fakeHid.next = fakeHid.ds4bt; });
    await page.click('#expPads [data-link="0"]');
    await page.waitForFunction(() => fakeHid.ds4bt.sent.length > 0);
    report = await lastReport(page, 'ds4bt');
    assert.equal(report.id, 0x11);
    assert.equal(report.data.length, 77);
    assert.deepEqual(report.data.slice(0, 10), [0xc4, 0, 0x03, 0, 0, 0, 0, 255, 0, 0]);
    assert.equal(le32(report.data, 73), crc32([0xa2, 0x11, ...report.data.slice(0, 73)]), 'Bluetooth CRC');
    console.log('PASS experimental: DualShock 4 by USB and Bluetooth: colour, both motors, unchanged state not resent, CRC, unlink');

    // DualSense by USB with trigger rumble: start-up light faded once, then the
    // newer rumble (firmware 2.32), the colour, and trigger effects only on change.
    await page.check('#expRows [data-experiment="triggers"]');
    await page.evaluate(() => { fakeHid.next = fakeHid.dualsense; });
    await page.click('#expPads [data-link="2"]');
    await page.waitForFunction(() => fakeHid.dualsense.sent.length >= 2);
    let sent = await page.evaluate(() => fakeHid.dualsense.sent);
    assert.equal(sent[0].id, 0x02);
    assert.equal(sent[0].data.length, 62);
    assert.deepEqual([sent[0].data[0], sent[0].data[1], sent[0].data[38], sent[0].data[41]], [0, 0, 0x02, 0x02], 'light bar setup alone first');
    report = sent[1];
    assert.equal(report.data[0] & 0x03, 0x02, 'firmware 2.32: no old-style rumble flag');
    assert.equal(report.data[38] & 0x04, 0x04, 'newer rumble flag');
    assert.equal(report.data[1], 0x04);
    assert.deepEqual(report.data.slice(44, 47), [255, 48, 64], 'controller 2\'s own colour');
    // Its touchpad's fingers reach the PS4, its 1080-high pad scaled to the PS4's 942.
    await page.evaluate(() => {
      const bytes = new Array(63).fill(0);
      bytes.fill(128, 0, 4); bytes[7] = 0x08; // sticks centred, D-pad released
      bytes[32] = 7; bytes[33] = 960 & 0xff; bytes[34] = (960 >> 8) | (1079 & 0x0f) << 4; bytes[35] = 1079 >> 4;
      bytes[36] = 0x80;
      fakeHid.dualsense.input(0x01, bytes);
    });
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1)?.params[8] === 1);
    assert.deepEqual((await page.evaluate(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1).params)).slice(8),
                     [1, 7, 960, 941]);
    await page.evaluate(() => {
      const bytes = new Array(63).fill(0);
      bytes.fill(128, 0, 4); bytes[7] = 0x08; bytes[32] = 0x87; bytes[36] = 0x80;
      fakeHid.dualsense.input(0x01, bytes);
    });
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1)?.params[8] === 0);
    // From here it reports its triggers all the time, as a real one does.
    await page.evaluate(() => {
      window.dsTriggers = [0, 0];
      window.dsTimer = setInterval(() => {
        const bytes = new Array(63).fill(0);
        bytes.fill(128, 0, 4); bytes[4] = dsTriggers[0]; bytes[5] = dsTriggers[1]; bytes[7] = 0x08; bytes[32] = 0x80; bytes[36] = 0x80;
        fakeHid.dualsense.input(0x01, bytes);
      }, 10);
    });
    feedback('12030d', 255, 128);
    await page.waitForFunction(() => fakeHid.dualsense.sent.at(-1).data[3] === 255);
    report = await lastReport(page, 'dualsense');
    assert.deepEqual([report.data[2], report.data[3]], [128, 255], 'small motor, large motor');
    assert.equal(report.data[0] & 0x0c, 0, 'no trigger effect while neither trigger is pulled');
    // The trigger being pulled vibrates, as strongly as the stronger motor (8),
    // slowly for the large one (30 Hz); the other is off.
    const lastEffects = () => page.evaluate(() => fakeHid.dualsense.sent.findLast(r => r.data[0] & 0x0c).data);
    const VIBRATE = [0x26, 0xff, 0x03, 0xff, 0xff, 0xff, 0x3f, 0, 0, 30, 0], OFF = [0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    await page.evaluate(() => { dsTriggers = [0, 200]; });
    await page.waitForFunction(() => fakeHid.dualsense.sent.findLast(r => r.data[0] & 0x0c)?.data[10] === 0x26);
    report = { data: await lastEffects() };
    assert.deepEqual([report.data.slice(10, 21), report.data.slice(21, 32)], [VIBRATE, OFF], 'R2 pulled: right trigger');
    // Aiming with L2 alone moves it there ...
    await page.evaluate(() => { dsTriggers = [200, 0]; });
    await page.waitForFunction(() => fakeHid.dualsense.sent.findLast(r => r.data[0] & 0x0c)?.data[21] === 0x26);
    report = { data: await lastEffects() };
    assert.deepEqual([report.data.slice(10, 21), report.data.slice(21, 32)], [OFF, VIBRATE], 'L2 only: left trigger');
    // ... and firing with R2 while aiming moves it to R2, the last one pulled, where it stays.
    await page.evaluate(() => { dsTriggers = [200, 200]; });
    await page.waitForFunction(() => fakeHid.dualsense.sent.findLast(r => r.data[0] & 0x0c)?.data[10] === 0x26);
    report = { data: await lastEffects() };
    assert.deepEqual([report.data.slice(10, 21), report.data.slice(21, 32)], [VIBRATE, OFF], 'R2 pulled while aiming: right trigger');
    const effectReports = await page.evaluate(() => fakeHid.dualsense.sent.filter(r => r.data[0] & 0x0c).length);
    await page.waitForTimeout(150);
    assert.equal(await page.evaluate(() => fakeHid.dualsense.sent.filter(r => r.data[0] & 0x0c).length), effectReports, 'unchanged effects re-sent');
    // A stronger small motor makes it quick (60 Hz).
    feedback('12030d', 64, 255);
    await page.waitForFunction(() => fakeHid.dualsense.sent.findLast(r => r.data[0] & 0x0c)?.data[19] === 60);
    // The light bar alone changing does not repeat the trigger effects.
    feedback('12030d', 64, 255, 0, 64, 0);
    await page.waitForFunction(() => fakeHid.dualsense.sent.at(-1).data[45] === 255);
    assert.equal((await lastReport(page, 'dualsense')).data[0] & 0x0c, 0, 'trigger effects re-sent without a change');
    feedback('12030d', 0, 0, 0, 64, 0);
    await page.waitForFunction(() => fakeHid.dualsense.sent.at(-1).data[3] === 0);
    report = await lastReport(page, 'dualsense');
    assert.deepEqual([report.data[0] & 0x0c, report.data[10], report.data[21]], [0x0c, 0x05, 0x05], 'trigger effects off when the rumble stops');
    await page.evaluate(() => { clearInterval(dsTimer); });
    // The Xbox pad rumbles in the trigger being pulled too, while the switch is on.
    const xboxTriggers = () => page.evaluate(() => effects.findLast(([type]) => type === 'trigger-rumble')?.[1]);
    feedback('11030d', 200, 100);
    await page.waitForFunction(() => effects.some(([type]) => type === 'trigger-rumble'));
    let params = await xboxTriggers();
    assert.deepEqual([params.leftTrigger, params.rightTrigger], [0, 0], 'neither trigger pulled: ' + JSON.stringify(params));
    await page.evaluate(() => { testPads[1].buttons[7].value = .9; });
    await page.waitForFunction(() => effects.findLast(([type]) => type === 'trigger-rumble')[1].rightTrigger > 0);
    params = await xboxTriggers();
    assert.ok(params.leftTrigger === 0 && Math.abs(params.rightTrigger - 200 / 255 * .6) < 1e-6, JSON.stringify(params));
    await page.evaluate(() => { testPads[1].buttons[7].value = 0; });
    feedback('11030d', 0, 0);
    console.log('PASS experimental: DualSense setup, newer rumble, colour, effects in the pulled trigger on change; Xbox trigger rumble');

    // DualSense over Bluetooth: report 0x31 with its sequence, tag and CRC. Once
    // linked the browser's gamepad goes quiet (here: frozen at rest), so the
    // controller's own reports carry its buttons, sticks and triggers.
    await page.click('#expPads [data-link="2"]');
    await page.waitForFunction(() => !c4fTest.hidLinks.has(2) && fakeHid.dualsense.closes === 1);
    await page.evaluate(() => { fakeHid.next = fakeHid.dualsensebt; });
    await page.click('#expPads [data-link="2"]');
    await page.waitForFunction(() => fakeHid.dualsensebt.sent.length >= 2);
    sent = await page.evaluate(() => fakeHid.dualsensebt.sent);
    for (const [i, r] of sent.slice(0, 2).entries()) {
      assert.equal(r.id, 0x31);
      assert.equal(r.data.length, 77);
      assert.deepEqual([r.data[0], r.data[1]], [i << 4, 0x10], 'sequence in the high nibble, then the tag');
      assert.equal(le32(r.data, 73), crc32([0xa2, 0x31, ...r.data.slice(0, 73)]), 'Bluetooth CRC');
    }
    assert.deepEqual([sent[0].data[2 + 38], sent[0].data[2 + 41]], [0x02, 0x02], 'light bar setup first');
    assert.deepEqual(sent[1].data.slice(2 + 44, 2 + 47), [0, 255, 0], 'the green the game set earlier');
    const btInput = (b0, b1, b2, x, r2) => {
      const bytes = new Array(77).fill(0);
      bytes.fill(128, 1, 5);
      bytes[1] = x; bytes[6] = r2; bytes[8] = b0; bytes[9] = b1; bytes[10] = b2;
      return bytes;
    };
    const pad1 = () => page.evaluate(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1)?.params);
    // Cross, D-pad up (hat 0), L1, PS; left stick fully left; R2 at 200.
    await page.evaluate(bytes => fakeHid.dualsensebt.input(0x31, bytes), btInput(0x20, 0x01, 0x01, 0, 200));
    await page.waitForFunction(() => (sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1)?.params[1] & 0x4000) !== 0);
    let u = await pad1();
    assert.equal(u[1], 0x4000 | 0x0010 | 0x0400 | 0x10000 | 0x0200, 'Cross, up, L1, PS and R2 pulled: ' + u[1].toString(16));
    assert.deepEqual([u[2], u[3], u[4], u[5], u[6], u[7]], [0, 128, 128, 128, 0, 200]);
    await page.evaluate(bytes => fakeHid.dualsensebt.input(0x31, bytes), btInput(0x08, 0, 0, 128, 0));
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1)?.params[1] === 0);
    // Unlinked, the browser's gamepad drives it again.
    await page.click('#expPads [data-link="2"]');
    await page.waitForFunction(() => !c4fTest.hidLinks.has(2));
    await page.evaluate(() => { testPads[2].buttons[0].value = 1; testPads[2].buttons[0].pressed = true; });
    await page.waitForFunction(() => (sentMessages.findLast(m => m.method === 'u' && m.params[0] === 1)?.params[1] & 0x4000) !== 0);
    await page.evaluate(() => { testPads[2].buttons[0].value = 0; testPads[2].buttons[0].pressed = false; });
    console.log('PASS experimental: DualSense by Bluetooth: reports and CRC; buttons, sticks and triggers from its own reports');

    // Motion: a phone held sideways, then a linked DualShock 4's own sensors.
    await page.check('#expRows [data-experiment="motion"]');
    await page.waitForFunction(() => motionListeners === 1, null, { timeout: 3000 }).catch(async () => {
      throw new Error('no devicemotion listener: ' + await page.evaluate(() => JSON.stringify({
        secure: isSecureContext, motion: 'DeviceMotionEvent' in window, on: c4fTest.experiments.motion,
        permission: typeof (window.DeviceMotionEvent && DeviceMotionEvent.requestPermission) })));
    });
    await page.evaluate(() => { c4fTest.openPad(0); });
    const motions = () => page.evaluate(() => sentMessages.filter(m => m.method === 'm').map(m => m.params));
    const shake = (beta, at) => page.evaluate(([beta, at]) => {
      const ev = new DeviceMotionEvent('devicemotion', { accelerationIncludingGravity: { x: 0, y: 9.80665, z: 0 },
        rotationRate: { alpha: 0, beta, gamma: 0 }, interval: 16 });
      Object.defineProperty(ev, 'timeStamp', { value: at });
      window.dispatchEvent(ev);
    }, [beta, at]);
    for (let i = 0; i < 6; i++) { await shake(0, 1000 + i * 20); await page.waitForTimeout(25); }
    await page.waitForFunction(() => sentMessages.some(m => m.method === 'm' && m.params[0] === 0));
    let m = (await motions()).filter(p => p[0] === 0).at(-1);
    assert.deepEqual(m.slice(1, 5), [0, 0, 0, 10000], 'held still and sideways: level');
    assert.deepEqual(m.slice(5, 8), [0, 1000, 0], 'gravity along y, like a DualShock 4 lying flat');
    await shake(90, 1200);
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'm')?.params[8] === 1571);
    m = (await motions()).at(-1);
    assert.ok(m[1] > 0 && m[4] < 10000, 'turning about x tilts the orientation: ' + m);
    // Leaving the controller view tells the PS4 the controller is still again.
    await page.evaluate(() => c4fTest.closePad());
    await page.waitForFunction(() => JSON.stringify(sentMessages.findLast(m => m.method === 'm')?.params) === '[0,0,0,0,10000,0,0,0,0,0,0]');
    // A linked DualShock 4's own gyro and accelerometer, calibrated. The
    // Bluetooth one is unlinked first; the USB one has calibration to read.
    await page.evaluate(() => c4fTest.openSettings('experimental'));
    assert.equal(await page.textContent('#expPads [data-link="0"]'), 'Unlink');
    await page.click('#expPads [data-link="0"]');
    await page.waitForFunction(() => !c4fTest.hidLinks.has(0) && fakeHid.ds4bt.closes === 1);
    await page.evaluate(() => { fakeHid.next = fakeHid.ds4; });
    await page.click('#expPads [data-link="0"]');
    await page.waitForFunction(() => c4fTest.hidLinks.get(0)?.calib);
    const report1 = (gyro, accel) => {
      const bytes = new Array(63).fill(0);
      bytes.fill(128, 0, 4); bytes[4] = 0x08; // sticks centred, D-pad released
      [...gyro, ...accel].forEach((v, i) => { bytes[12 + 2 * i] = v & 0xff; bytes[13 + 2 * i] = (v >> 8) & 0xff; });
      return bytes;
    };
    await page.evaluate(bytes => { for (let i = 0; i < 3; i++) fakeHid.ds4.input(0x01, bytes); }, report1([8 * 90, 0, 0], [0, 8192, 0]));
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'm' && m.params[0] === 0)?.params[8] === 1571);
    m = (await motions()).filter(p => p[0] === 0).at(-1);
    assert.deepEqual(m.slice(5, 11), [0, 1000, 0, 1571, 0, 0], '90 degrees per second about x, 1 G on y');
    // Its touchpad's fingers, which browsers' gamepads leave out: the newest of a
    // report's touch reports, after its timestamp byte.
    const touchReport = (contact, x, y) => {
      const bytes = new Array(63).fill(0);
      bytes.fill(128, 0, 4); bytes[4] = 0x08;
      bytes[32] = 1;
      bytes[34] = contact; bytes[35] = x & 0xff; bytes[36] = (x >> 8) | (y & 0x0f) << 4; bytes[37] = y >> 4;
      bytes[38] = 0x80;
      return bytes;
    };
    const lastInput = () => page.evaluate(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 0)?.params);
    await page.evaluate(bytes => fakeHid.ds4.input(0x01, bytes), touchReport(5, 1000, 500));
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 0)?.params[8] === 1);
    assert.deepEqual((await lastInput()).slice(8), [1, 5, 1000, 500]);
    await page.evaluate(bytes => fakeHid.ds4.input(0x01, bytes), touchReport(0x85, 1000, 500));
    await page.waitForFunction(() => sentMessages.findLast(m => m.method === 'u' && m.params[0] === 0)?.params[8] === 0);
    // Switching motion off: still again, and no more listening.
    await page.uncheck('#expRows [data-experiment="motion"]');
    await page.waitForFunction(() => JSON.stringify(sentMessages.findLast(m => m.method === 'm')?.params) === '[0,0,0,0,10000,0,0,0,0,0,0]');
    console.log('PASS experimental: phone motion (level, gravity on y, gyro), still on leaving; DualShock 4 sensors calibrated and touches');
    await context.close();
  } finally {
    if (browser) await browser.close();
    if (server) server.kill();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
