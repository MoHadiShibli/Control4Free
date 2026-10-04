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
})();
