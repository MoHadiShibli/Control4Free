// Runs the shipped page's own claim and Gamepad-polling functions under Node,
// with small state mocks. No browser and no console.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const page = fs.readFileSync('client/index.html', 'utf8');
const extract = (start, end) => {
  const from = page.indexOf(start);
  assert.notEqual(from, -1, `page no longer contains ${JSON.stringify(start)}`);
  const to = page.indexOf(end, from);
  assert.notEqual(to, -1, `page no longer contains ${JSON.stringify(end)}`);
  return page.slice(from, to);
};
const sendClaimSource = extract("    let lastClaim = '';", '    const lastSent =');
const pollSource = extract('    function pollGamepads(t)', '    function setGamepadPad(');

function padStatus(fields) {
  return Object.assign({ known: true, mine: false, open: false }, fields);
}

(async () => {
  // A claim is answered as a whole. One controller taken on another device must
  // not disconnect the gamepad that was playing as a different one.
  const claims = [];
  const ctx = {
    conn: { state: 'open' },
    keepAwake: { update() {} },
    primaryPad: -1,
    status: [padStatus({ open: true }), padStatus({ mine: true, open: true }), padStatus({}), padStatus({})],
    claimedPads: () => [...ctx.gamepadSources.values()].map(s => s.pad).filter(p => p >= 0).sort(),
    gamepadSources: new Map([[0, { pad: 0 }], [1, { pad: 1 }]]),
    call(method, params) {
      claims.push([method, params]);
      if (method === 'claim' && claims.filter(c => c[0] === 'claim').length === 1)
        return Promise.reject({ message: 'Controller is in use on another device' });
      return Promise.resolve({ pads: [] });
    },
    applyStatus() {}, toast() {}, closePad() { ctx.primaryPad = -1; }, renderGamepads() {},
  };
  vm.createContext(ctx);
  vm.runInContext(sendClaimSource, ctx);
  vm.runInContext('sendClaim(true)', ctx);
  for (let i = 0; i < 5; i++) await new Promise(setImmediate);
  assert.equal(ctx.gamepadSources.get(0).pad, -1, 'the controller taken elsewhere is given up');
  assert.equal(ctx.gamepadSources.get(1).pad, 1, 'the controller we own is kept');
  assert.deepEqual(claims.filter(c => c[0] === 'claim').map(c => c[1]), [[0, 1], [1]],
                   'the remaining controller is claimed again');
  console.log('PASS a refused claim gives up only the controllers owned elsewhere');

  // getGamepads() can start throwing after it has worked. Held buttons must not
  // stay in the source: nothing would be left to release them.
  const pads = {
    gamepadsAvailable: true,
    primaryPad: -1,
    navigator: { getGamepads() { throw Error('SecurityError'); } },
    gamepadSources: new Map([[0, { pad: 0, buttons: 0x4000, lx: 0, ly: 255, rx: 0, ry: 0, l2: 255, r2: 255, touches: [{}] }]]),
    blankSource: pad => ({ pad, buttons: 0, lx: 128, ly: 128, rx: 128, ry: 128, l2: 0, r2: 0, touches: [] }),
    renderGamepads() {}, sendClaim() {}, $: () => null,
  };
  vm.createContext(pads);
  vm.runInContext(pollSource, pads);
  vm.runInContext('pollGamepads(100)', pads);
  const src = pads.gamepadSources.get(0);
  assert.deepEqual(
    { buttons: src.buttons, lx: src.lx, ly: src.ly, rx: src.rx, ry: src.ry, l2: src.l2, r2: src.r2, touches: src.touches },
    { buttons: 0, lx: 128, ly: 128, rx: 128, ry: 128, l2: 0, r2: 0, touches: [] });
  assert.equal(src.pad, 0, 'the controller assignment is kept');
  console.log('PASS failed Gamepad polling blanks every source');

  // The full-screen button, in the three places it behaves differently.
  const fsSource = extract('    // Full screen. Safari on iPhone', '    // Options menu');
  async function fullScreen({ standalone, displayMode = false, enabled }) {
    const button = { hidden: false, handlers: {}, addEventListener(type, fn) { this.handlers[type] = fn; } };
    const ctx = {
      button, toasts: [], requested: 0,
      navigator: standalone === undefined ? {} : { standalone },
      window: {},
      matchMedia: () => ({ matches: displayMode }),
      screen: {},
      document: {
        fullscreenEnabled: enabled,
        documentElement: { requestFullscreen() { ctx.requested++; return Promise.resolve(); } },
      },
      $: () => button,
      toast(message) { ctx.toasts.push(message); },
    };
    ctx.window.matchMedia = ctx.matchMedia;
    vm.createContext(ctx);
    vm.runInContext(fsSource, ctx);
    if (!button.hidden) await button.handlers.click();
    return ctx;
  }
  // Safari on iPhone: no page full screen, so the button explains the home screen.
  let fs = await fullScreen({ standalone: false, enabled: false });
  assert.equal(fs.button.hidden, false);
  assert.equal(fs.requested, 0);
  assert.match(fs.toasts[0], /Add to Home Screen/);
  // Opened from the iPhone home screen: already without bars, nothing to offer.
  fs = await fullScreen({ standalone: true, enabled: false });
  assert.equal(fs.button.hidden, true);
  // Installed elsewhere (display-mode standalone): same.
  fs = await fullScreen({ displayMode: true, enabled: true });
  assert.equal(fs.button.hidden, true);
  // A browser that allows it: the button really goes full screen.
  fs = await fullScreen({ enabled: true });
  assert.equal(fs.button.hidden, false);
  assert.equal(fs.requested, 1);
  assert.deepEqual(fs.toasts, []);
  // Neither possible nor iOS: no button that would do nothing.
  fs = await fullScreen({ enabled: false });
  assert.equal(fs.button.hidden, true);
  console.log('PASS full screen: real where allowed, a home-screen hint on iPhone, gone when standalone');

  // Which gamepads the page says it can rumble.
  const rumbleCtx = {};
  vm.createContext(rumbleCtx);
  vm.runInContext(extract('    function canRumble(gp)', '    function renderGamepads()'), rumbleCtx);
  const can = gp => vm.runInContext('canRumble(gp)', Object.assign(rumbleCtx, { gp }));
  const play = () => Promise.resolve();
  assert.equal(can({ vibrationActuator: { playEffect: play, effects: ['dual-rumble', 'trigger-rumble'] } }), true);
  assert.equal(can({ vibrationActuator: { playEffect: play, type: 'dual-rumble' } }), true, 'older Chrome');
  assert.equal(can({ vibrationActuator: null }), false, 'Chrome with a DualSense');
  assert.equal(can({}), false, 'Firefox');
  assert.equal(can({ vibrationActuator: { playEffect: play, effects: [] } }), false);
  console.log('PASS gamepads that cannot rumble are named as such');

  // The Invite panel draws the console's grid as runs of dark modules.
  const qrCtx = {};
  vm.createContext(qrCtx);
  vm.runInContext(extract('    function qrPath(size, rows, quiet)', '    // The address this page reached'), qrCtx);
  // 5 wide: row 0 = 11011, row 1 = 00100 -> hex d8, 20.
  assert.equal(vm.runInContext("qrPath(5, ['d8', '20'], 4)", qrCtx),
               'M4 4h2v1h-2zM7 4h2v1h-2zM6 5h1v1h-1z');
  assert.equal(vm.runInContext("qrPath(3, ['0', '0', '0'], 4)", qrCtx), '');
  console.log('PASS the invite QR is drawn from the console grid');
})();
