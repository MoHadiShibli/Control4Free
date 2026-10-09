// Synthetic device snapshots exercise the shipped pure mapper, not a test copy.
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const page = fs.readFileSync('client/index.html', 'utf8');
const ctx = {};
vm.createContext(ctx);
const extract = (start, end) => page.slice(page.indexOf(start), page.indexOf(end, page.indexOf(start)));
vm.runInContext(extract('    const B = {', '    const VALID_BUTTONS') +
  extract('    const ControllerMapping =', '    const profileStore =') + '\nthis.M = ControllerMapping; this.B = B;', ctx);
const { M, B } = ctx;
const device = (buttons = 24, axes = 6) => ({ id: 'Synthetic controller', mapping: '', buttons: Array.from({ length: buttons }, () => ({ value: 0, pressed: false })), axes: Array(axes).fill(0) });
const gp = device(), sig = M.signature(gp);
const fresh = () => ({ id: 'test', name: 'Test', signature: sig, calibration: {}, bindings: [] });
const button = (i, output, mode = 'digital', options = {}) => M.binding({ kind: 'button', index: i, mode }, output, options);
const axis = (i, output, mode = 'center', options = {}) => M.binding({ kind: 'axis', index: i, mode }, output, options);
const evaluate = (p, g = gp, state) => M.toSource(M.evaluate(M.read(g), p, state));
let p = fresh();
p.bindings = [button(0, 'CROSS'), button(23, 'CROSS'), button(23, 'SQUARE')];
for (const ordering of [[0, 23], [23, 0]]) {
  gp.buttons.forEach(b => { b.value = 0; b.pressed = false; });
  for (const i of ordering) { gp.buttons[i].value = 1; assert(evaluate(p).buttons & B.CROSS); }
  gp.buttons[ordering[0]].value = 0; assert(evaluate(p).buttons & B.CROSS);
  gp.buttons[ordering[1]].value = 0; assert.equal(evaluate(p).buttons, 0);
}
gp.buttons[23].value = 1; assert.equal(evaluate(p).buttons, B.CROSS | B.SQUARE); gp.buttons[23].value = 0;
console.log('PASS extra buttons, duplicate press/release orderings and one-to-many bindings');

p = M.browserDefault(sig);
for (let n = -1000; n <= 1000; n++) {
  gp.axes[0] = n / 1000;
  const expected = Math.abs(gp.axes[0]) < .06 ? 128 : Math.round((gp.axes[0] + 1) / 2 * 255);
  assert.equal(evaluate(p).lx, expected);
}
for (let i = 0; i < 18; i++) {
  gp.buttons.forEach(b => b.value = 0); gp.buttons[i].value = 1;
  const s = evaluate(p);
  if (i === 6 || i === 7) assert.equal(s[i === 6 ? 'l2' : 'r2'], 255);
  else assert(s.buttons & B[p.bindings[i].output]);
}
gp.buttons.forEach(b => b.value = 0); gp.axes.fill(0);
gp.buttons[6].value = .25; assert.equal(evaluate(p).l2, 64); assert.equal(evaluate(p).buttons & B.L2, 0);
gp.buttons[6].value = 0;
const legacyConflict = M.copy(p); legacyConflict.bindings = legacyConflict.bindings.filter(b => b.input.kind === 'axis');
legacyConflict.bindings[1].output = 'lx'; gp.axes[0] = .5; gp.axes[1] = -.5;
assert.equal(evaluate(legacyConflict).lx, 128, 'legacy transfer must also merge before quantization'); gp.axes.fill(0);
console.log('PASS standard layout and original stick transfer parity; analog trigger pressure stays independent');
for (let byte = 0; byte <= 255; byte++) {
  const original = { buttons: 0, lx: byte, ly: byte, rx: byte, ry: byte, l2: byte, r2: byte };
  const result = M.toSource(M.fromSource(original));
  assert.equal(result.lx, byte); assert.equal(result.l2, byte);
}

p = fresh(); p.calibration = { 0: { type: 'center', min: -.8, center: .2, max: .9 },
  1: { type: 'pedal', released: -1, full: 1 }, 2: { type: 'pedal', released: 1, full: -1 } };
p.bindings = [axis(0, 'lx'), axis(1, 'R2', 'pedal'), axis(2, 'L2', 'pedal')];
gp.axes = [.2, -1, 1, 0, 0, 0]; assert.equal(evaluate(p).lx, 128); assert.equal(evaluate(p).r2, 0);
gp.axes[0] = -.8; assert.equal(evaluate(p).lx, 0); gp.axes[0] = .9; assert.equal(evaluate(p).lx, 255);
gp.axes[1] = 0; gp.axes[2] = -1; assert.equal(evaluate(p).r2, 128); assert.equal(evaluate(p).l2, 255);
p = fresh(); p.bindings = [axis(0, 'R2', 'positive'), axis(0, 'L2', 'negative')];
gp.axes[0] = -.5; assert.equal(evaluate(p).l2, 128); assert.equal(evaluate(p).r2, 0);
gp.axes[0] = .5; assert.equal(evaluate(p).l2, 0); assert.equal(evaluate(p).r2, 128);
p = fresh(); p.bindings = [axis(0, 'lx', 'center', { deadzone: .1, saturation: .9, exponent: 2, invert: true })];
gp.axes[0] = .5; assert.equal(evaluate(p).lx, 96); gp.axes[0] = .05; assert.equal(evaluate(p).lx, 128);
console.log('PASS asymmetric wheels, separate/reversed/combined pedals, inversion, deadzones, saturation and curves');

p = fresh(); p.bindings = [axis(0, 'CROSS', 'positive')]; const transient = {};
for (const [v, expected] of [[.49, 0], [.51, B.CROSS], [.45, B.CROSS], [.39, 0], [.45, 0]]) {
  gp.axes[0] = v; assert.equal(evaluate(p, gp, transient).buttons, expected);
}
gp.axes[0] = NaN; assert.equal(evaluate(p, gp, transient).buttons, 0); assert.equal(transient[0], undefined);
p = fresh(); p.calibration[5] = { type: 'hat', values: [3, -1, -.75, -.5, -.25, 0, .25, .5, .75], tolerance: .05 };
p.bindings = ['UP', 'RIGHT', 'DOWN', 'LEFT'].map(direction => M.binding({ kind: 'hat', index: 5, direction }, direction));
assert.equal(M.validate(p), '');
for (let i = 0; i < 9; i++) { gp.axes[5] = p.calibration[5].values[i]; assert.equal(evaluate(p).buttons, M.hats[i].reduce((n, key) => n | B[key], 0)); }
gp.axes[5] = 2; assert.equal(evaluate(p).buttons, 0);
console.log('PASS hysteresis, invalid readings and all nine encoded-hat positions');

p = fresh(); p.bindings = [button(0, 'LS_LEFT'), button(1, 'LS_RIGHT')];
gp.buttons[0].value = 1; gp.buttons[1].value = 1; assert.equal(evaluate(p).lx, 128);
const other = device(); other.buttons[1].value = 1; gp.buttons[1].value = 0;
assert.equal(M.toSource(M.merge([M.evaluate(M.read(gp), p), M.evaluate(M.read(other), p)])).lx, 128);
p = fresh(); p.bindings = [axis(0, 'lx'), axis(1, 'lx')]; gp.axes[0] = -.5; gp.axes[1] = .5; assert.equal(evaluate(p).lx, 128);
gp.axes[1] = .8; assert.equal(evaluate(p).lx, 230);
p = fresh(); p.bindings = [button(2, 'L2', 'pressure'), button(3, 'L2', 'pressure')]; gp.buttons[2].value = .3; gp.buttons[3].value = .6; assert.equal(evaluate(p).l2, 153);
p = fresh(); p.bindings = [button(0, 'CROSS'), button(1, 'SQUARE'), axis(4, 'ry'), button(2, 'UP'), button(3, 'DOWN')];
gp.axes[4] = .75; assert.equal(evaluate(p).ry, 223); assert(evaluate(p).buttons & B.CROSS);
console.log('PASS digital conflicts, analog conflicts, cross-device merging, trigger maximum and guitar-style sources');

const exported = M.exportProfile(p), imported = M.importProfile(JSON.parse(JSON.stringify(exported)));
assert.equal(JSON.stringify(imported), JSON.stringify(p)); imported.name = 'Independent draft'; assert.notEqual(imported.name, p.name);
for (const bad of [null, {}, { ...exported, schema: 99 }, { ...exported, profile: { ...p, bindings: [{}] } }]) assert.throws(() => M.importProfile(bad));
const badCalibration = M.copy(p); badCalibration.calibration[0] = { type: 'pedal', released: 1, full: 1 }; assert(M.validate(badCalibration));
const mismatch = { ...sig, buttons: 1, axes: 0 }; assert(M.unresolved(p, mismatch));
gp.buttons.forEach(b => b.value = 0); gp.axes.fill(0);
p = fresh(); p.calibration[0] = { type: 'pedal', released: -1, full: 1 }; p.bindings = [axis(0, 'R2', 'pedal')];
const held = (p, values) => { gp.axes.splice(0, values.length, ...values); const hold = M.holdBack(p); return M.letGo(M.read(gp), p, hold); };
assert.deepEqual(Object.keys(held(p, [-1])), [], 'a released pedal plays at once');
assert.deepEqual(Object.keys(held(p, [-.5])), ['0'], 'a half-pressed pedal waits');
assert.deepEqual(Object.keys(held(p, [null])), ['0'], 'an unreadable input waits');
console.log('PASS JSON round-trip, draft copying, schema/data rejection, unavailable inputs and held-input gating');

// Only the binding whose input is still held waits. A worn stick resting off
// centre, or an axis resting at one end, never silences the rest of the gamepad.
gp.buttons.forEach(b => b.value = 0); gp.axes.fill(0);
p = M.browserDefault(sig);
gp.axes[0] = .12; gp.axes[2] = -1; gp.buttons[1].value = 1;
let hold = M.letGo(M.read(gp), p, M.holdBack(p));
assert.deepEqual(Object.keys(hold).map(i => p.bindings[i].output), ['CIRCLE'], 'only the held button waits');
gp.buttons[0].value = 1;
let out = M.toSource(M.evaluate(M.read(gp), p, {}, hold));
assert.equal(out.buttons, B.CROSS, 'other buttons play while one is held');
assert.notEqual(out.lx, 128, 'a stick off centre still plays');
gp.buttons[1].value = 0; M.letGo(M.read(gp), p, hold); gp.buttons[1].value = 1;
assert.equal(M.toSource(M.evaluate(M.read(gp), p, {}, hold)).buttons, B.CROSS | B.CIRCLE, 'a let-go button plays again');
const inverted = fresh(); inverted.bindings = [button(0, 'CROSS', 'digital', { invert: true })];
gp.buttons[0].value = 0;
assert.deepEqual(Object.keys(M.letGo(M.read(gp), inverted, M.holdBack(inverted))), [], 'a reversed button at rest is not held');
gp.buttons.forEach(b => b.value = 0); gp.axes.fill(0);
console.log('PASS a held input waits on its own; drifting sticks and resting axes never block the gamepad');

// Parse the entire script to catch errors outside the extracted functions.
new vm.Script(page.match(/<script>([\s\S]*?)<\/script>/)[1]);

// Setting a range a step at a time: a stick from its rest and both ends, a hat
// from its rest and four directions, with the diagonals between them.
const range = M.centeredRange(.02, .97, -.93);
assert.deepEqual({ ...range }, { type: 'center', min: -.93, center: .02, max: .97 });
assert.equal(M.centeredRange(0, .05, -1), null, 'one end too close to the rest');
const hat = M.hatFromCardinals(3.2857, -1, -3 / 7, 1 / 7, 5 / 7);
const usual = [3.2857, -1, -5 / 7, -3 / 7, -1 / 7, 1 / 7, 3 / 7, 5 / 7, 1];
hat.values.forEach((v, i) => assert(Math.abs(v - usual[i]) < 1e-9, 'hat position ' + i));
p = fresh(); p.calibration[0] = { ...hat, values: [...hat.values] };
assert.equal(M.validate(Object.assign(p, { bindings: ['UP', 'RIGHT', 'DOWN', 'LEFT'].map(direction => M.binding({ kind: 'hat', index: 0, direction }, direction)) })), '');
gp.axes[0] = -5 / 7;
assert.equal(evaluate(p).buttons, B.UP | B.RIGHT, 'a recorded hat reads its diagonal');
gp.axes[0] = 0;
assert.equal(M.hatFromCardinals(3, 0, 0, 1, -1), null, 'directions that read the same are refused');
console.log('PASS ranges set a step at a time: sticks, and hats with their diagonals');
