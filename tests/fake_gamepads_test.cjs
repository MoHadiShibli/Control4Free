// Researched Gamepad API emulations, run only in the Playwright Docker image.
// The controller page is instrumented in memory; the shipped HTML is unchanged.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const { createLabServer } = require('./fake_gamepads_server.cjs');

const root = '/src', profileDir = path.join(root, 'tests/fake_gamepads');
const profiles = fs.readdirSync(profileDir).filter(file => file.endsWith('.json') && file !== 'manifest.json')
  .sort().map(file => JSON.parse(fs.readFileSync(path.join(profileDir, file), 'utf8')));
assert(profiles.length, 'No researched profiles found');
const fakeScript = fs.readFileSync(path.join(root, 'tests/fake_gamepads.js'), 'utf8');
const hook = '\nwindow.c4fTest = { ControllerMapping, mapper, profileStore, gamepadSources, pollGamepads, setGamepadPad, mergedState, renderGamepads, renderPads, openSettings, closeSettings, openPad, profileFor, loadProfiles, persistProfiles, clearDevice };\n';
const originalHtml = fs.readFileSync(path.join(root, 'client/index.html'), 'utf8');
const html = originalHtml.replace('\n  })();', hook + '  })();');
assert.notEqual(html, originalHtml, 'In-memory controller-page hook was not inserted');
const outputs = {
  CROSS: ['Cross', 0x4000], CIRCLE: ['Circle', 0x2000], SQUARE: ['Square', 0x8000],
  TRIANGLE: ['Triangle', 0x1000], L1: ['L1', 0x400], R1: ['R1', 0x800],
  L2: ['L2', 0x100], R2: ['R2', 0x200], L3: ['L3', 0x2], R3: ['R3', 0x4],
  SHARE: ['Share', 0x1], OPTIONS: ['Options', 0x8], PS: ['PS button', 0x10000],
  TOUCH: ['Touchpad click', 0x100000], UP: ['D-pad up', 0x10], RIGHT: ['D-pad right', 0x20],
  DOWN: ['D-pad down', 0x40], LEFT: ['D-pad left', 0x80],
  lx: ['Left stick, left and right'], ly: ['Left stick, up and down'],
  rx: ['Right stick, left and right'], ry: ['Right stick, up and down']
};
const lab = createLabServer();
fs.copyFileSync(path.join(root, 'build/web-host-test'), '/tmp/c4f-fake-gamepad-service');
const service = spawn('/tmp/c4f-fake-gamepad-service', [], { stdio: ['ignore', 'ignore', 'pipe'] });
let serviceError;
service.on('error', error => { serviceError = error; });
service.stderr.on('data', data => console.error(data.toString().trim()));

function resolvedControl(profile, name) {
  const seen = new Set();
  let control = profile.controls[name];
  while (control?.kind === 'alias') {
    assert(!seen.has(control.target), 'Circular control alias');
    seen.add(control.target); control = profile.controls[control.target];
  }
  return control;
}
const buttonNames = profile => Object.keys(profile.controls).filter(name => resolvedControl(profile, name)?.kind === 'button');
const controlAt = (profile, index) => buttonNames(profile).find(name => resolvedControl(profile, name).index === index);
// Click a control on the mapping view's picture, then Change: it listens for the gamepad.
const pictureTarget = output => ({ lx: 'LS', ly: 'LS', rx: 'RS', ry: 'RS' })[output] || output;
async function changeControl(owner, output) {
  await owner.click('#mapChips [data-target="' + pictureTarget(output) + '"]');
  await owner.click('#mapPanel button:has-text("Change")');
}
const poll = page => page.evaluate(() => c4fTest.pollGamepads(performance.now()));
const press = (page, name) => page.evaluate(name => { fakePads.press(0, name); c4fTest.pollGamepads(performance.now()); }, name);
const release = (page, name) => page.evaluate(name => { fakePads.release(0, name); c4fTest.pollGamepads(performance.now()); }, name);
const hat = (page, direction, name) => page.evaluate(({ direction, name }) => {
  fakePads.hat(0, direction, name); c4fTest.pollGamepads(performance.now());
}, { direction, name });
async function mapButton(page, profile, name, output) {
  await changeControl(page, output);
  await page.waitForFunction(() => c4fTest.mapper.capture?.ready);
  await press(page, name);
  await page.waitForFunction(() => !c4fTest.mapper.capture);
  const inputs = await page.evaluate(output => c4fTest.mapper.draft.bindings
    .filter(binding => binding.output === output).map(binding => binding.input), output);
  assert.deepEqual(inputs, [{ kind: 'button', index: resolvedControl(profile, name).index, mode: 'digital' }], name + ' captured through Change');
  await release(page, name);
}
async function openEditor(page) {
  await page.click('.gp-row[data-index="0"] [data-focus="configure"]');
  await page.waitForFunction(() => c4fTest.mapper.draft !== null);
}
async function sourceReady(page, profile) {
  await page.waitForFunction(id => window.c4fTest?.gamepadSources.get(0)?.signature.id === id, profile.gamepad.id.value);
}
async function checkApi(page, profile) {
  const snapshot = await page.evaluate(() => {
    const slots = navigator.getGamepads(), first = slots[0], timestamp = first.timestamp, next = navigator.getGamepads()[0];
    return {
      slotCount: slots.length, holes: slots.slice(1),
      id: next.id, index: next.index, mapping: next.mapping, connected: next.connected,
      axes: next.axes, buttons: next.buttons.map(button => ({ value: button.value, pressed: button.pressed, touched: button.touched })),
      actuator: next.vibrationActuator ? { type: next.vibrationActuator.type, effects: [...next.vibrationActuator.effects] } : null,
      actuatorPresent: Object.hasOwn(next, 'vibrationActuator'), stableObject: first === next,
      timestampIncreased: next.timestamp > timestamp,
      gamepadTag: Object.prototype.toString.call(next), buttonTag: Object.prototype.toString.call(next.buttons[0]),
      events: window.fakeEvents
    };
  });
  assert.equal(snapshot.slotCount, 4);
  assert.deepEqual(snapshot.holes, [null, null, null]);
  assert.equal(snapshot.id, profile.gamepad.id.value);
  assert.equal(snapshot.index, 0);
  assert.equal(snapshot.mapping, profile.gamepad.mapping.value);
  assert.equal(snapshot.connected, true);
  assert.deepEqual(snapshot.axes, profile.gamepad.axesRest.value, 'exact documented resting values');
  assert.deepEqual(snapshot.buttons, Array.from({ length: profile.gamepad.buttons.value }, () => ({ value: 0, pressed: false, touched: false })));
  assert.deepEqual(snapshot.actuator, profile.gamepad.vibration.value);
  assert.equal(snapshot.actuatorPresent, profile.gamepad.vibration.presence ?
    profile.gamepad.vibration.presence !== 'absent' : !!profile.gamepad.vibration.value);
  assert(snapshot.stableObject, 'polls refresh the browser slot object');
  assert.equal(snapshot.timestampIncreased, profile.gamepad.timestampPolicy?.value !== 'change', 'idle timestamp follows the documented backend policy');
  assert.equal(snapshot.gamepadTag, '[object Gamepad]');
  assert.equal(snapshot.buttonTag, '[object GamepadButton]');
  assert.deepEqual(snapshot.events, [{ type: 'gamepadconnected', index: 0, id: profile.gamepad.id.value, connected: true }]);

  const analog = buttonNames(profile).find(name => resolvedControl(profile, name).analog);
  if (analog) {
    const control = resolvedControl(profile, analog);
    const changes = await page.evaluate(({ name, index, threshold, inclusive, steps }) => {
    const old = navigator.getGamepads()[0];
    fakePads.set(0, name, .05);
    const beforePoll = old.buttons[index].value;
    const light = { ...navigator.getGamepads()[0].buttons[index] };
    const refreshedOld = old.buttons[index].value;
    fakePads.set(0, name, .75);
    const heavy = { ...navigator.getGamepads()[0].buttons[index] };
    fakePads.release(0, name);
    const expected = value => steps ? Math.round(value * steps) / steps : value;
    return { beforePoll, refreshedOld, light: { value: light.value, touched: light.touched, pressed: light.pressed },
      heavy: { value: heavy.value, touched: heavy.touched, pressed: heavy.pressed },
      expectedLight: expected(.05), expectedHeavy: expected(.75), threshold, inclusive };
  }, { name: analog, index: control.index, threshold: control.pressedThreshold ?? .5, inclusive: !!control.pressedInclusive, steps: control.steps });
  assert.equal(changes.beforePoll, 0, 'the browser object keeps its previous sample until polled');
  assert.equal(changes.refreshedOld, changes.expectedLight, 'polling refreshes retained Gamepad references');
  for (const [state, expected] of [[changes.light, changes.expectedLight], [changes.heavy, changes.expectedHeavy]]) {
    const pressed = changes.inclusive ? expected >= changes.threshold : expected > changes.threshold;
    assert.equal(state.value, expected);
    assert.equal(state.pressed, pressed);
    assert.equal(state.touched, control.touchedPolicy === 'pressed' ? pressed : expected > 0);
  }
  }
  const digital = controlAt(profile, 0);
  const digitalCheck = await page.evaluate(name => {
    let error = '';
    try { fakePads.set(0, name, .25); } catch (caught) { error = caught.message; }
    fakePads.press(0, name);
    const pressed = navigator.getGamepads()[0].buttons[0].value;
    fakePads.release(0, name);
    return { error, pressed };
  }, digital);
  assert(digitalCheck.error, 'digital buttons reject invented analog pressure');
  assert.equal(digitalCheck.pressed, 1);
  const stickName = Object.keys(profile.controls).find(name => resolvedControl(profile, name)?.kind === 'stick');
  if (stickName) {
    const stick = resolvedControl(profile, stickName);
    const moved = await page.evaluate(name => { fakePads.tilt(0, name, .6, -.7); return [...navigator.getGamepads()[0].axes]; }, stickName);
    assert.equal(moved[stick.x], .6); assert.equal(moved[stick.y], -.7);
    await release(page, stickName);
  }
  if (profile.gamepad.vibration.value) {
    const feedback = await page.evaluate(async () => {
      const actuator = navigator.getGamepads()[0].vibrationActuator, results = [];
      for (const effect of actuator.effects) results.push(await actuator.playEffect(effect, { duration: 20, strongMagnitude: .25 }));
      results.push(await actuator.reset());
      let rejected = '';
      try { await actuator.playEffect('unresearched-effect'); } catch (error) { rejected = error.name; }
      const invalid = await actuator.playEffect(actuator.effects[0], { duration: -1 });
      const unsupportedEffect = ['dual-rumble', 'trigger-rumble'].find(effect => !actuator.effects.includes(effect));
      const unsupported = unsupportedEffect ? await actuator.playEffect(unsupportedEffect) : null;
      return { results, rejected, invalid, unsupported, calls: fakePads.feedback() };
    });
    assert(feedback.results.every(result => result === 'complete'));
    assert.equal(feedback.rejected, 'TypeError', 'unknown haptic enum values match browser rejection');
    assert.equal(feedback.invalid, 'invalid-parameter');
    if (profile.gamepad.vibration.value.effects.length === 1) assert.equal(feedback.unsupported, 'not-supported');
    assert.equal(feedback.calls.length, profile.gamepad.vibration.value.effects.length + 1);
  }
  const strum = Object.keys(profile.controls).find(name => /^strum up$/i.test(name));
  const dpadUp = Object.keys(profile.controls).find(name => /^d-pad up$/i.test(name));
  if (strum && dpadUp && resolvedControl(profile, strum).index === resolvedControl(profile, dpadUp).index) {
    const values = await page.evaluate(({ strum, dpadUp, index }) => {
      fakePads.press(0, strum); fakePads.press(0, dpadUp);
      fakePads.release(0, strum);
      const stillHeld = navigator.getGamepads()[0].buttons[index].value;
      fakePads.release(0, dpadUp);
      return [stillHeld, navigator.getGamepads()[0].buttons[index].value];
    }, { strum, dpadUp, index: resolvedControl(profile, strum).index });
    assert.deepEqual(values, [1, 0], 'releasing strum preserves the shared D-pad contribution until both controls release');
  }
  for (const [name, spec] of Object.entries(profile.switches || {})) {
    if (spec.kind === 'unreported') {
      const error = await page.evaluate(name => { try { fakePads.switch(0, name, 'unknown'); return ''; } catch (error) { return error.message; } }, name);
      assert(error, name + ' must not invent a browser signal');
    } else if (spec.kind === 'state') {
      for (const value of spec.values) {
        const state = await page.evaluate(({ name, value }) => {
          const before = navigator.getGamepads()[0]; fakePads.switch(0, name, value);
          return { value: fakePads.switchState(0)[name], id: navigator.getGamepads()[0].id, axes: navigator.getGamepads()[0].axes, before: before.axes };
        }, { name, value });
        assert.equal(state.value, value); assert.equal(state.id, profile.gamepad.id.value);
        assert.deepEqual(state.axes, state.before, 'physical switch with no documented browser effect adds no guessed input');
      }
    } else if (spec.kind === 'profile') {
      for (const [value, target] of Object.entries(spec.values)) {
        const expected = profiles.find(profile => profile.name === target);
        assert(expected, 'Mode targets must have researched profiles');
        const id = await page.evaluate(({ name, value }) => { fakePads.switch(0, name, value); return navigator.getGamepads()[0].id; }, { name, value });
        assert.equal(id, expected.gamepad.id.value);
        await page.evaluate(name => { fakePads.disconnect(0); fakePads.connect(name); }, profile.name);
      }
    }
  }
  await page.evaluate(() => { fakePads.reset(0); c4fTest.pollGamepads(performance.now()); });
}
async function runProfile(browser, profile) {
  console.log('START fake profile: ' + profile.name);
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  await context.grantPermissions(['local-network-access']);
  // A reload starts the same physical signature, with saved mappings retained.
  await context.addInitScript({ content: fakeScript + '\n' +
    'window.fakePads.loadProfiles(' + JSON.stringify(profiles).replace(/</g, '\\u003c') + ');\n' +
    'window.fakeEvents = [];\n' +
    'for (const type of ["gamepadconnected","gamepaddisconnected"]) window.addEventListener(type, event => fakeEvents.push({type,index:event.gamepad.index,id:event.gamepad.id,connected:event.gamepad.connected}));\n' +
    'window.fakePads.connect(' + JSON.stringify(profile.name) + ');\n' });
  const page = await context.newPage(), errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route(/^http:\/\/127\.0\.0\.1:4264\/(?:\?.*)?$/, async route => route.fulfill({ response: await route.fetch(), body: html }));
  try {
    await page.goto('http://127.0.0.1:4264/');
    await sourceReady(page, profile);
    await page.waitForFunction(() => document.querySelector('#conn').dataset.conn === 'open');
    await checkApi(page, profile);
    const row = page.locator('.gp-row[data-index="0"]');
    assert.equal(await row.locator('.gp-name').textContent(), profile.gamepad.id.value.replace(/\s*\(.*?\)\s*/g, ' ').trim());
    assert.equal((await row.textContent()).includes('buttons may be mixed up'), profile.gamepad.mapping.value === '');
    assert((await row.textContent()).includes('Standard layout'));
    await row.locator('[data-focus="pad-0"]').click();
    await poll(page);
    const button0 = controlAt(profile, 0);
    assert(button0, profile.name + ' has no real name for raw button 0');
    await press(page, button0);
    await page.waitForFunction(() => !!(c4fTest.mergedState(0).buttons & 0x4000));
    await release(page, button0);
    await page.waitForFunction(() => !(c4fTest.mergedState(0).buttons & 0x4000));
    await page.screenshot({ path: root + '/build/fake-' + profile.name + '-home.png', fullPage: true });

    await openEditor(page);
    await page.fill('#mapName', 'Local ' + profile.name);
    const special = Object.keys(profile.controls).filter(name => /^(P[1-4])$|paddle shifter|fret|strum/i.test(name));
    const availableOutputs = ['CIRCLE', 'SQUARE', 'TRIANGLE', 'L1', 'R1', 'L3', 'R3', 'SHARE', 'OPTIONS', 'PS', 'TOUCH'];
    let assigned = 0;
    for (const name of special) {
      const control = resolvedControl(profile, name);
      if (control.kind === 'unreported') {
        const error = await page.evaluate(name => { try { fakePads.press(0, name); return ''; } catch (error) { return error.message; } }, name);
        assert.match(error, /not independently exposed/, name + ' is honestly unavailable');
      } else {
        assert.equal(control.kind, 'button', 'Special hardware control needs an explicit tested capture action');
        const output = availableOutputs[assigned++];
        assert(output, 'Add output targets for additional distinct hardware controls');
        await mapButton(page, profile, name, output);
      }
    }
    if (!assigned) {
      const second = controlAt(profile, 1);
      assert(second); await mapButton(page, profile, second, 'CIRCLE');
    }

    if (profile.controls.Wheel) {
      await changeControl(page, 'lx');
      await page.waitForFunction(() => c4fTest.mapper.capture?.ready);
      await page.evaluate(() => { fakePads.set(0, 'Wheel', .8); c4fTest.pollGamepads(performance.now()); });
      // A stick is two moves; a wheel has only the first.
      await page.waitForFunction(() => c4fTest.mapper.capture?.output === 'ly');
      await page.click('#mapPanel button:has-text("Cancel")');
      await page.waitForFunction(() => !c4fTest.mapper.capture);
      const binding = await page.evaluate(() => c4fTest.mapper.draft.bindings.find(binding => binding.output === 'lx'));
      assert.deepEqual(binding.input, { kind: 'axis', index: profile.controls.Wheel.index, mode: 'center' });
      assert.equal(binding.invert, false, 'moving the wheel right binds positive lx');
      await release(page, 'Wheel');
    }
    const hatName = Object.keys(profile.controls).find(name => resolvedControl(profile, name)?.kind === 'hat');
    const hasDirections = ['up', 'right', 'down', 'left'].every(direction => Object.keys(profile.controls).some(name => name.toLowerCase() === 'd-pad ' + direction));
    if (hatName || hasDirections) {
      for (const direction of ['up', 'right', 'down', 'left']) {
        await changeControl(page, direction.toUpperCase());
        await page.waitForFunction(() => c4fTest.mapper.capture?.ready);
        await hat(page, direction, hatName);
        await page.waitForFunction(() => !c4fTest.mapper.capture);
        const binding = await page.evaluate(output => c4fTest.mapper.draft.bindings.find(binding => binding.output === output), direction.toUpperCase());
        if (hatName) {
          assert.deepEqual(binding.input, { kind: 'hat', index: resolvedControl(profile, hatName).index, direction: direction.toUpperCase() });
          const calibration = await page.evaluate(index => c4fTest.mapper.draft.calibration[index], resolvedControl(profile, hatName).index);
          const slot = { up: 1, right: 3, down: 5, left: 7 }[direction];
          assert.equal(calibration.values[0], resolvedControl(profile, hatName).neutral);
          assert.equal(calibration.values[slot], resolvedControl(profile, hatName).directions[direction]);
        } else assert.equal(binding.input.index, resolvedControl(profile, Object.keys(profile.controls).find(name => name.toLowerCase() === 'd-pad ' + direction)).index);
        await hat(page, 'none', hatName);
      }
    }
    const draft = await page.evaluate(() => c4fTest.ControllerMapping.copy(c4fTest.mapper.draft));
    const heldBinding = draft.bindings.find(binding => binding.input.kind === 'button' && binding.input.mode === 'digital' && outputs[binding.output]?.[1] && controlAt(profile, binding.input.index));
    assert(heldBinding, 'A named digital mapping is required for the hold gate');
    const heldName = controlAt(profile, heldBinding.input.index), heldMask = outputs[heldBinding.output][1];
    const otherBinding = draft.bindings.find(binding => binding.input.kind === 'button' && binding.input.mode === 'digital' &&
      binding.input.index !== heldBinding.input.index && binding.output !== heldBinding.output && outputs[binding.output]?.[1] && controlAt(profile, binding.input.index));
    assert(otherBinding, 'A second digital input is required to check independent release gates');
    const otherName = controlAt(profile, otherBinding.input.index), otherMask = outputs[otherBinding.output][1];
    await page.evaluate(() => document.querySelector('#toast').classList.remove('show'));
    await page.locator('#mapView').evaluate(element => { element.scrollTop = 0; });
    await page.screenshot({ path: root + '/build/fake-' + profile.name + '-mapping.png', fullPage: true });
    if (profile.controls.Wheel) {
      await page.setViewportSize({ width: 390, height: 844 });
      await page.screenshot({ path: root + '/build/fake-' + profile.name + '-mapping-mobile.png', fullPage: true });
      assert(await page.evaluate(() => document.querySelector('#mapView').scrollWidth <= innerWidth), 'wheel mapping has no mobile horizontal overflow');
      await page.setViewportSize({ width: 1440, height: 1000 });
    }
    await press(page, heldName);
    assert.equal(await page.evaluate(mask => c4fTest.mergedState(0).buttons & mask, heldMask), 0, 'editing never plays a draft');
    await page.click('#mapSave');
    await page.click('#mapBack');
    await page.waitForFunction(() => c4fTest.mapper.index === null);
    await poll(page);
    assert.equal(await page.evaluate(mask => c4fTest.mergedState(0).buttons & mask, heldMask), 0, 'held mapped input is suppressed immediately after Save');
    await press(page, otherName);
    await page.waitForFunction(mask => !!(c4fTest.mergedState(0).buttons & mask), otherMask);
    assert.equal(await page.evaluate(mask => c4fTest.mergedState(0).buttons & mask, heldMask), 0, 'another input cannot release the held binding');
    const rests = await page.evaluate(() => [...navigator.getGamepads()[0].axes]);
    assert.deepEqual(rests, profile.gamepad.axesRest.value, 'nonzero trigger/whammy/pedal rests do not block another input');
    await release(page, otherName);
    await release(page, heldName);
    await press(page, heldName);
    await page.waitForFunction(mask => !!(c4fTest.mergedState(0).buttons & mask), heldMask);
    await release(page, heldName);
    if (profile.controls.Wheel) {
      await page.evaluate(() => { fakePads.set(0, 'Wheel', .8); c4fTest.pollGamepads(performance.now()); });
      assert((await page.evaluate(() => c4fTest.mergedState(0).lx)) > 180, 'quick-mapped wheel drives the merged left-stick X');
      await release(page, 'Wheel');
    }
    if (hatName || hasDirections) {
      for (const direction of ['up', 'right', 'down', 'left']) {
        await hat(page, direction, hatName);
        assert(await page.evaluate(mask => !!(c4fTest.mergedState(0).buttons & mask), outputs[direction.toUpperCase()][1]), 'mapped D-pad ' + direction + ' reaches mergedState');
        await hat(page, 'none', hatName);
      }
    }
    const saved = await page.evaluate(() => c4fTest.ControllerMapping.copy(c4fTest.profileFor(c4fTest.gamepadSources.get(0))));
    assert.notEqual(saved.id, 'default');
    await page.reload();
    await sourceReady(page, profile);
    const reloaded = await page.evaluate(() => c4fTest.ControllerMapping.copy(c4fTest.profileFor(c4fTest.gamepadSources.get(0))));
    assert.deepEqual(reloaded, saved, 'same browser ID/signature reloads the saved full bindings and calibration');
    await page.locator('.gp-row[data-index="0"] [data-focus="pad-0"]').click();
    await poll(page);
    await press(page, heldName);
    await page.waitForFunction(mask => !!(c4fTest.mergedState(0).buttons & mask), heldMask);
    await release(page, heldName);
    if (profile.controls.Wheel) {
      // Intentional stress input: this is not a measured unplugged-pedal rest.
      // The researched default remains +1; exercise the opposite axis endpoint.
      const pedal = Object.keys(profile.controls).find(name => /pedal/i.test(name) && resolvedControl(profile, name)?.kind === 'axis');
      assert(pedal);
      await page.evaluate(name => { fakePads.set(0, name, -1); c4fTest.pollGamepads(performance.now()); }, pedal);
      await openEditor(page);
      await press(page, heldName);
      await page.click('#mapBack');
      await poll(page);
      assert.equal(await page.evaluate(mask => c4fTest.mergedState(0).buttons & mask, heldMask), 0, 'held input waits after Save with a persistent -1 stress axis');
      await press(page, otherName);
      assert(await page.evaluate(mask => !!(c4fTest.mergedState(0).buttons & mask), otherMask), 'a persistent -1 stress axis never blocks another mapped button');
      await release(page, otherName);
      await release(page, heldName);
      await press(page, heldName);
      assert(await page.evaluate(mask => !!(c4fTest.mergedState(0).buttons & mask), heldMask), 'held input works after release despite the -1 stress axis');
      await release(page, heldName);
      await release(page, pedal);
    }
    await openEditor(page);
    await changeControl(page, 'CROSS');
    await page.waitForFunction(() => c4fTest.mapper.capture?.ready);
    const disconnectedReference = await page.evaluate(() => {
      const retained = navigator.getGamepads()[0]; fakePads.disconnect(0); c4fTest.pollGamepads(performance.now());
      return retained.connected;
    });
    assert.equal(disconnectedReference, false, 'a retained Gamepad reports its disconnection');
    await page.waitForFunction(() => c4fTest.gamepadSources.size === 0 && c4fTest.mapper.index === null && !c4fTest.mapper.capture);
    assert.equal(await page.evaluate(() => navigator.getGamepads()[0]), null, 'disconnect retains a null slot');
    assert.equal(await page.evaluate(() => fakeEvents.at(-1).type), 'gamepaddisconnected');
    assert.equal(await page.evaluate(() => fakeEvents.at(-1).connected), false);
    assert.equal(await page.evaluate(name => fakePads.connect(name), profile.name), 0, 'the browser slot is reused');
    await sourceReady(page, profile);
    assert.deepEqual(await page.evaluate(() => c4fTest.profileFor(c4fTest.gamepadSources.get(0)).bindings), saved.bindings);
    assert.deepEqual(errors, [], 'no page errors, including disconnect mid-edit');
    console.log('PASS fake profile: ' + profile.name + ' — exact snapshots, controls, quick mapping, release gates, persistence, disconnect');
  } catch (error) {
    await page.screenshot({ path: root + '/build/fake-' + profile.name + '-failure.png', fullPage: true }).catch(() => {});
    error.message = profile.name + ': ' + error.message;
    throw error;
  } finally { await context.close(); }
}
async function runManualHarness(browser) {
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  await context.grantPermissions(['local-network-access']);
  const page = await context.newPage(), errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://127.0.0.1:8000/client/index.html', async route => route.fulfill({ response: await route.fetch(), body: html }));
  try {
    await page.goto('http://127.0.0.1:8000/tests/fake_gamepads.html');
    await page.waitForFunction(() => !document.querySelector('#profile').disabled);
    assert.equal(await page.locator('#profile option').count(), profiles.length);
    const chosen = profiles.find(profile => profile.controls.Wheel) || profiles[0];
    await page.selectOption('#profile', chosen.name);
    await page.click('#connect');
    const frame = page.frames().find(frame => frame.parentFrame());
    assert(frame, 'Manual harness needs its controller iframe');
    await frame.waitForFunction(() => window.c4fTest?.gamepadSources.size === 1);
    await frame.waitForFunction(() => document.querySelector('#conn').dataset.conn === 'open');
    assert.equal(await frame.evaluate(() => navigator.getGamepads()[0].id), chosen.gamepad.id.value);
    const refused = await frame.evaluate(() => {
      try { new WebSocket('wss://example.invalid/ws'); return ''; } catch (error) { return error.message; }
    });
    assert.match(refused, /only connects to the local test service/, 'the local manual harness rejects external WebSocket destinations');
    await frame.click('.gp-row [data-focus="configure"]');
    await changeControl(frame, 'CROSS');
    await frame.waitForFunction(() => c4fTest.mapper.capture?.ready);
    const name = controlAt(chosen, 0), index = Object.keys(chosen.controls).indexOf(name);
    const hold = page.locator('#controls .control').nth(index).getByRole('button', { name: 'Hold', exact: true });
    await hold.focus(); await page.keyboard.down('Space');
    await frame.waitForFunction(() => !c4fTest.mapper.capture);
    await page.keyboard.up('Space');
    assert.equal(await frame.evaluate(() => c4fTest.mapper.draft.bindings.find(binding => binding.output === 'CROSS').input.index), 0, 'manual Hold drives the actual quick capture');
    await page.click('#reset');
    assert.deepEqual(await frame.evaluate(() => [...navigator.getGamepads()[0].axes]), chosen.gamepad.axesRest.value);
    await page.screenshot({ path: root + '/build/fake-manual-harness.png', fullPage: true });
    await page.click('#disconnect');
    await frame.waitForFunction(() => c4fTest.gamepadSources.size === 0);
    assert.equal(await frame.evaluate(() => navigator.getGamepads()[0]), null);
    assert.deepEqual(errors, []);
    console.log('PASS fake manual harness: local profile loading, iframe connection, Hold capture, reset and disconnect');
  } finally { await context.close(); }
}
async function screenshotOverview(browser) {
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
  try {
    const cards = profiles.flatMap(profile => ['home', 'mapping'].map(screen => {
      const image = fs.readFileSync(root + '/build/fake-' + profile.name + '-' + screen + '.png').toString('base64');
      const label = (profile.name + ' — ' + screen).replace(/[&<>"']/g, character =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
      return '<figure><figcaption>' + label + '</figcaption><img src="data:image/png;base64,' + image + '"></figure>';
    })).join('');
    await page.setContent('<!doctype html><html><head><meta charset="utf-8"><title>Fake gamepad screenshots</title>' +
      '<style>*{box-sizing:border-box}body{margin:0;background:#101827;color:#f3f6fa;font:13px system-ui}' +
      'main{display:grid;grid-template-columns:repeat(4,400px)}figure{margin:0;padding:0 0 12px}' +
      'figcaption{padding:10px;min-height:54px;overflow-wrap:anywhere}img{display:block;width:400px;height:auto;object-fit:contain}</style>' +
      '</head><body><main>' + cards + '</main></body></html>');
    await page.evaluate(() => Promise.all([...document.images].map(image => image.decode())));
    await page.screenshot({ path: root + '/build/fake-profiles-overview.png', fullPage: true });
  } finally { await page.close(); }
}
(async () => {
  let browser;
  try {
    await new Promise((resolve, reject) => { lab.once('error', reject); lab.listen(8000, '127.0.0.1', resolve); });
    browser = await chromium.launch({ headless: true });
    if (serviceError) throw serviceError;
    for (const profile of profiles) await runProfile(browser, profile);
    await runManualHarness(browser);
    await screenshotOverview(browser);
    if (!profiles.some(profile => /crkd|guitar/i.test(profile.name))) console.log('NOT FOUND: no sourced CRKD guitar profile; fret/strum hardware scenarios were not fabricated');
  } finally {
    if (browser) await browser.close();
    await lab.stop();
    service.kill();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
