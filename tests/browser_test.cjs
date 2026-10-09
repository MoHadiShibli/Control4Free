// Optional real-browser integration checks, run in the Playwright Docker image.
// Test instrumentation is inserted into the page in memory, never shipped.
const { chromium } = require('playwright');
const fs = require('node:fs'), assert = require('node:assert/strict'), { spawn } = require('node:child_process');
const hook = '\nwindow.c4fTest = { ControllerMapping, mapper, profileStore, gamepadSources, pollGamepads, setGamepadPad, mergedState, renderGamepads, renderPads, openSettings, closeSettings, openPad, profileFor, loadProfiles, persistProfiles, clearDevice, applyStatus, status, getKeys: () => keymap };\n';
const html = fs.readFileSync('/src/client/index.html', 'utf8').replace('\n  })();', hook + '  })();');
fs.copyFileSync('/src/build/web-host-test', '/tmp/c4f-browser-service');
const server = spawn('/tmp/c4f-browser-service', [], { env: { ...process.env, C4F_TEST_KLOG_FD: '0' }, stdio: ['pipe', 'pipe', 'pipe'] });
server.on('error', error => console.error('Service harness:', error));
server.stderr.on('data', data => console.error(data.toString()));
server.stdout.on('data', data => { const rows = data.toString().split('\n').filter(line => !line.startsWith('FRAME') && line); if (rows.length) console.log(rows.join('\n')); });

// Click a control on the mapping view's picture: its tag, which every control has.
const control = (page, target) => page.click(`#mapChips [data-target="${target}"]`);
const listening = page => page.waitForFunction(() => c4fTest.mapper.capture?.ready);
const heard = page => page.waitForFunction(() => !c4fTest.mapper.capture);
const bindings = (page, output) => page.evaluate(o => c4fTest.mapper.draft.bindings.filter(b => b.output === o).map(b => ({ ...b.input, invert: b.invert })), output);

(async () => {
  let browser;
  try {
    browser = await chromium.launch({ headless: true });
    const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
    // A fulfilled test document has no network address-space metadata. Allow
    // its local test service explicitly, as a user would allow local access.
    await context.grantPermissions(['local-network-access']);
    await context.addInitScript(() => {
      window.testClaims = [];
      window.testInputs = [];
      const socketSend = WebSocket.prototype.send;
      WebSocket.prototype.send = function(data) {
        try {
          const message = JSON.parse(data);
          if (message.method === 'claim') testClaims.push(message.params);
          if (message.method === 'u') testInputs.push(message.params);
        } catch {}
        return socketSend.call(this, data);
      };
      window.testPads = [0, 1, 2].map(index => ({ index, id: index < 2 ? 'Synthetic standard controller' : 'Synthetic wheel / pedals',
        mapping: index < 2 ? 'standard' : '', connected: true, buttons: Array.from({ length: 24 }, () => ({ value: 0, pressed: false })), axes: Array(6).fill(0) }));
      Object.defineProperty(navigator, 'getGamepads', { value: () => { if (window.failPolling) throw Error('SecurityError'); return window.testPads; } });
    });
    const page = await context.newPage(), errors = [];
    page.on('websocket', ws => ws.on('socketerror', error => console.error('WebSocket:', error)));
    page.on('pageerror', e => { errors.push(e.message); console.error('Page error:', e.stack); });
    await page.route(/^http:\/\/127\.0\.0\.1:4264\/(?:\?.*)?$/, async r => r.fulfill({ response: await r.fetch(), body: html }));
    await page.goto('http://127.0.0.1:4264/');
    await page.waitForFunction(() => window.c4fTest?.gamepadSources.size === 3);
    await page.waitForFunction(() => document.querySelector('#conn').dataset.conn === 'open');
    assert.equal(await page.locator('#addressForm').isHidden(), true, 'a page served by the PS4 never asks for its address');
    // Sign the first player in, open/sign in the second, and press the first
    // gamepad's PS before, during and after that change. Assignment status and
    // the currently open controller must not move one button to another slot.
    await page.evaluate(() => { c4fTest.setGamepadPad(0, 0); c4fTest.openPad(0); });
    await page.waitForFunction(() => c4fTest.status[0].mine && c4fTest.status[0].state === 'select');
    server.stdin.write('DEVICE_OWNER_CHANGED [DeviceId:0x11030d][UserId:0x1a2b3c4d]\n');
    await page.waitForFunction(() => c4fTest.status[0].state === 'ready' && c4fTest.status[0].user === 'Alex');
    await page.waitForFunction(() => !c4fTest.gamepadSources.get(0).waitNeutral);
    await page.evaluate(() => { testPads[0].buttons[16].value = 1; });
    await page.waitForFunction(() => document.querySelector('#psBtn').classList.contains('on'));
    await page.evaluate(() => { testPads[0].buttons[16].value = 0; });
    await page.waitForFunction(() => !(c4fTest.mergedState(0).buttons & 0x10000));
    await page.evaluate(() => { c4fTest.setGamepadPad(1, 1); c4fTest.openPad(1); });
    await page.waitForFunction(() => c4fTest.status[1].mine && c4fTest.status[1].state === 'select');
    await page.evaluate(() => { testPads[0].buttons[16].value = 1; });
    await page.waitForFunction(() => !!(c4fTest.mergedState(0).buttons & 0x10000));
    assert.equal(await page.evaluate(() => c4fTest.mergedState(1).buttons & 0x10000), 0, 'PS moved while the second controller was choosing a user');
    server.stdin.write('DEVICE_OWNER_CHANGED [DeviceId:0x12030d][UserId:0x1a2b3c4e]\n');
    await page.waitForFunction(() => c4fTest.status[1].state === 'ready' && c4fTest.status[1].user.startsWith('Sam'));
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(0).pad), 0, 'signing in another user reassigned the first source');
    assert.equal(await page.evaluate(() => c4fTest.mergedState(1).buttons & 0x10000), 0, 'PS moved after the second controller signed in');
    assert.equal(await page.locator('#psBtn').evaluate(e => e.classList.contains('on')), false, 'the second controller picture displayed the first controller PS');
    await page.waitForFunction(() => !!(testInputs.findLast(p => p[0] === 0)?.[1] & 0x10000));
    assert.equal(await page.evaluate(() => testInputs.findLast(p => p[0] === 1)?.[1] & 0x10000), 0, 'the first signed-in gamepad sent PS to the second slot');
    await page.evaluate(() => { testPads[0].buttons[16].value = 0; });
    await page.waitForFunction(() => !(c4fTest.mergedState(0).buttons & 0x10000));
    await page.click('#backBtn');
    await page.evaluate(() => c4fTest.setGamepadPad(2, 0));
    console.log('PASS real browser: first PS stays with its signed-in player before, during and after the second sign-in');

    // Identical gamepads still have independent PS inputs. The open controller
    // view and physical enumeration order must not choose where a button goes.
    await page.waitForFunction(() => c4fTest.status[0].mine && c4fTest.status[1].mine);
    await page.evaluate(() => { c4fTest.setGamepadPad(2, -1); c4fTest.openPad(1); });
    for (const targets of [[0, 1], [1, 0]]) {
      for (const index of [0, 1]) {
        const selection = await page.evaluate(({ index, pad }) => {
          c4fTest.setGamepadPad(index, pad);
          return [...new Set([1, ...[...c4fTest.gamepadSources.values()].map(s => s.pad).filter(p => p >= 0)])];
        }, { index, pad: targets[index] });
        await page.waitForFunction(selection => c4fTest.status.every(p => p.mine === selection.includes(p.pad)), selection);
      }
      await page.waitForFunction(() => c4fTest.status[0].mine && c4fTest.status[1].mine &&
        !c4fTest.gamepadSources.get(0).waitNeutral && !c4fTest.gamepadSources.get(1).waitNeutral);
      for (const index of [0, 1]) {
        const target = targets[index], other = 1 - target;
        await page.evaluate(index => { testPads[index].buttons[16].value = 1; }, index);
        await page.waitForFunction(target => !!(c4fTest.mergedState(target).buttons & 0x10000), target);
        assert.equal(await page.evaluate(other => c4fTest.mergedState(other).buttons & 0x10000, other), 0, 'PS leaked into the other virtual controller');
        await page.waitForFunction(target => document.querySelector('#psBtn').classList.contains('on') === (target === 1), target);
        await page.waitForFunction(target => !!(testInputs.findLast(p => p[0] === target)?.[1] & 0x10000), target);
        assert.equal(await page.evaluate(other => testInputs.findLast(p => p[0] === other)?.[1] & 0x10000, other), 0, 'PS went to the wrong WebSocket slot');
        await page.evaluate(index => { testPads[index].buttons[16].value = 0; }, index);
        await page.waitForFunction(() => !(c4fTest.mergedState(0).buttons & 0x10000) && !(c4fTest.mergedState(1).buttons & 0x10000));
        await page.waitForFunction(() => !document.querySelector('#psBtn').classList.contains('on'));
      }
      for (const firstReleased of [0, 1]) {
        await page.evaluate(() => { testPads[0].buttons[16].value = testPads[1].buttons[16].value = 1; });
        await page.waitForFunction(() => (c4fTest.mergedState(0).buttons & 0x10000) && (c4fTest.mergedState(1).buttons & 0x10000));
        await page.evaluate(index => { testPads[index].buttons[16].value = 0; }, firstReleased);
        await page.waitForFunction(targets => !(c4fTest.mergedState(targets[0]).buttons & 0x10000) && !!(c4fTest.mergedState(targets[1]).buttons & 0x10000),
          [targets[firstReleased], targets[1 - firstReleased]]);
        await page.evaluate(() => { testPads[0].buttons[16].value = testPads[1].buttons[16].value = 0; });
        await page.waitForFunction(() => !(c4fTest.mergedState(0).buttons & 0x10000) && !(c4fTest.mergedState(1).buttons & 0x10000));
      }
    }
    await page.evaluate(() => { c4fTest.setGamepadPad(0, 0); c4fTest.setGamepadPad(1, 1); c4fTest.setGamepadPad(2, 0); });
    await page.click('#backBtn');
    console.log('PASS real browser: independent PS buttons on identical gamepads, reversed assignments, simultaneous holds, both release orders, display and WebSocket routing');

    // The two mapping tabs must read only the selected physical source, even
    // when identical pads arrive in a different enumeration order. Guide can
    // be exposed through pressed alone, rather than a positive analog value.
    await page.evaluate(() => { window.testPads = [testPads[1], null, testPads[0], testPads[2]]; });
    for (const selected of [0, 1]) {
      await page.evaluate(index => c4fTest.mapper.open(index), selected);
      await page.click('#mapTabs [data-tab="inputs"]');
      for (const physical of [0, 1]) {
        for (const value of [1, 0]) {
          const changedAt = await page.evaluate(({ physical, value }) => {
            const button = testPads.find(p => p?.index === physical).buttons[16];
            button.value = value; button.pressed = true;
            return performance.now();
          }, { physical, value });
          await page.waitForFunction(({ physical, changedAt }) =>
            c4fTest.gamepadSources.get(physical).snapshot.buttons[16].pressed &&
            c4fTest.mapper.renderedAt > changedAt, { physical, changedAt });
          const expected = physical === selected;
          assert.equal(await page.locator('#mapInputs [data-input="button:16"]').evaluate(e => e.classList.contains('on')), expected,
            `raw Guide from gamepad ${physical} appeared on gamepad ${selected}`);
          await page.click('#mapTabs [data-tab="pad"]');
          await page.waitForFunction(() => c4fTest.mapper.renderedAt > performance.now() - 50);
          assert.equal(await page.locator('.map-area [data-btn="PS"]').evaluate(e => e.classList.contains('on')), expected,
            `mapped Guide from gamepad ${physical} appeared on gamepad ${selected}`);
          await page.evaluate(physical => {
            const button = testPads.find(p => p?.index === physical).buttons[16];
            button.value = 0; button.pressed = false;
          }, physical);
          await page.waitForFunction(() => !document.querySelector('.map-area [data-btn="PS"]').classList.contains('on'));
          await page.click('#mapTabs [data-tab="inputs"]');
          await page.waitForFunction(() => !document.querySelector('#mapInputs [data-input="button:16"]').classList.contains('on'));
        }
      }
      await page.click('#mapBack');
    }
    await page.evaluate(() => { window.testPads = testPads.filter(Boolean).sort((a, b) => a.index - b.index); });
    console.log('PASS real browser: both mapping tabs isolate each PS input, including pressed-only Guide and sparse/reordered device enumeration');

    // The gamepad's row opens its mapping. While it's mapped it doesn't play;
    // the other gamepads do.
    await page.click('.gp-row[data-index="0"] [data-focus="configure"]');
    assert.equal(await page.locator('#mapView').isVisible(), true);
    await page.fill('#mapName', 'Shared paddles');
    await control(page, 'CROSS');
    await page.click('#mapPanel button:has-text("Add another")');
    await listening(page);
    await page.evaluate(() => { testPads[0].buttons[23].value = 1; testPads[1].buttons[0].value = 1; });
    await heard(page);
    assert.deepEqual((await bindings(page, 'CROSS')).map(i => i.index).sort((a, b) => a - b), [0, 23], 'Add another keeps the old input');
    await page.waitForFunction(() => c4fTest.gamepadSources.get(1).buttons === 0x4000);
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(0).buttons), 0, 'a gamepad being mapped never plays');
    await page.waitForFunction(() => document.querySelector('.map-area [data-btn="CROSS"].on') !== null);
    await page.click('#mapSave');
    const originalId = await page.evaluate(() => c4fTest.gamepadSources.get(0).profileId);
    assert.notEqual(originalId, 'default');
    await page.click('#mapBack');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(0).waitNeutral), true, 'a held paddle doesn\'t press anything on leaving');
    await page.evaluate(() => { testPads[0].buttons[23].value = 0; testPads[1].buttons[0].value = 0; });
    await page.waitForFunction(() => !c4fTest.gamepadSources.get(0).waitNeutral);
    await page.evaluate(id => { c4fTest.mapper.open(1); c4fTest.mapper.choose(id); }, originalId);
    await page.click('#mapSave');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(1).profileId), originalId);
    await page.click('#mapBack');
    console.log('PASS real browser: mapping on the picture, Add another, the mapped gamepad muted, others playing, shared profile');

    // Change, then press: the input moves from the control that had it. A stick
    // is two moves, right then down, reversed when moved the other way.
    await page.evaluate(() => c4fTest.mapper.open(0));
    await control(page, 'CIRCLE');
    await page.click('#mapPanel button:has-text("Change")');
    await listening(page);
    await page.evaluate(() => { testPads[0].buttons[0].value = 1; });
    await heard(page);
    assert.deepEqual((await bindings(page, 'CIRCLE')).map(i => i.index), [0]);
    assert(!(await bindings(page, 'CROSS')).some(i => i.index === 0), 'button 0 left Cross');
    await page.evaluate(() => { testPads[0].buttons[0].value = 0; });
    await control(page, 'LS');
    await page.click('#mapPanel button:has-text("Change")');
    await listening(page);
    await page.evaluate(() => { testPads[0].axes[2] = -1; });
    await page.waitForFunction(() => c4fTest.mapper.capture?.output === 'ly' && !c4fTest.mapper.capture.ready);
    await page.evaluate(() => { testPads[0].axes[2] = 0; });
    await listening(page);
    await page.evaluate(() => { testPads[0].axes[3] = 1; });
    await heard(page);
    const lx = await bindings(page, 'lx'), ly = await bindings(page, 'ly');
    assert.deepEqual(lx, [{ kind: 'axis', index: 2, mode: 'center', invert: true }], 'moved left when asked for right');
    assert.deepEqual(ly, [{ kind: 'axis', index: 3, mode: 'center', invert: false }]);
    assert.deepEqual(await bindings(page, 'rx'), [], 'axis 2 left the right stick');
    await page.evaluate(() => { testPads[0].axes[3] = 0; });
    await page.click('#mapSave');
    await page.click('#mapBack');
    await page.waitForFunction(() => !c4fTest.gamepadSources.get(0).waitNeutral);
    await page.evaluate(() => { testPads[0].buttons[0].value = 1; });
    await page.waitForFunction(() => c4fTest.gamepadSources.get(0).buttons === 0x2000);
    await page.evaluate(() => { testPads[0].buttons[0].value = 0; });
    console.log('PASS real browser: Change then press remaps a button and a two-step stick, taking the input from its old control');

    // Gamepad Controls: an axis made to drive a stick from the picture, then
    // its range set a step at a time; a pedal; a D-pad hat.
    await page.evaluate(() => c4fTest.mapper.open(2));
    await page.click('#mapTabs [data-tab="inputs"]');
    assert.equal(await page.textContent('#mapPanel .map-panel-title'), 'Gamepad Controls');
    assert.match(await page.textContent('#mapPanel'), /buttons and axes your browser detects/);
    assert.match(await page.textContent('#mapStatus'), /Select a button or axis/);
    await page.click('.in-axis[data-input="axis:4"]');
    await page.click('#mapTabs [data-tab="pad"]');
    assert.equal(await page.textContent('#mapPanel .map-panel-title'), 'DS4 Mapping', 'switching tabs clears the previous selection');
    await control(page, 'CROSS');
    await page.click('#mapTabs [data-tab="inputs"]');
    assert.equal(await page.textContent('#mapPanel .map-panel-title'), 'Gamepad Controls', 'a selected DS4 control cannot hide the input guide');
    await page.waitForFunction(() => getComputedStyle(document.querySelector('#mapTabs [data-tab="inputs"]')).backgroundColor === 'rgb(255, 255, 255)');
    await page.screenshot({ path: '/src/build/gamepad-controls-guide.png' });
    await page.setViewportSize({ width: 390, height: 844 });
    await page.locator('#mapPanel').scrollIntoViewIfNeeded();
    assert(await page.evaluate(() => document.querySelector('#mapView').scrollWidth <= innerWidth), 'input guide overflows on mobile');
    await page.screenshot({ path: '/src/build/gamepad-controls-guide-mobile.png' });
    await page.setViewportSize({ width: 1440, height: 1000 });
    await page.click('.in-axis[data-input="axis:4"]');
    await page.click('#mapPanel button:has-text("Make it drive")');
    await control(page, 'RS');
    await page.click('#mapPanel button:has-text("Left and right")');
    assert((await bindings(page, 'rx')).some(i => i.kind === 'axis' && i.index === 4 && i.mode === 'center'), 'the axis drives the stick, beside what already did');
    await page.click('#mapTabs [data-tab="inputs"]');
    await page.click('.in-axis[data-input="axis:4"]');
    await page.click('#mapPanel button:has-text("Set its range")');
    await page.click('#mapPanel button:has-text("A stick or wheel")');
    for (const [value, button] of [[.04, 'middle'], [-.9, 'Holding'], [.95, 'Done']]) {
      await page.evaluate(v => { testPads[2].axes[4] = v; }, value);
      await page.waitForTimeout(60);
      await page.click(`#mapPanel button:has-text("${button}")`);
    }
    assert.deepEqual(await page.evaluate(() => c4fTest.mapper.draft.calibration[4]), { type: 'center', min: -.9, center: .04, max: .95 });
    await page.evaluate(() => { testPads[2].axes[4] = 0; testPads[2].axes[5] = -1; });
    await page.click('#mapTabs [data-tab="pad"]');
    await control(page, 'R2');
    await page.click('#mapPanel button:has-text("Change")');
    await listening(page);
    await page.evaluate(() => { testPads[2].axes[5] = 1; });
    await heard(page);
    assert.deepEqual(await page.evaluate(() => c4fTest.mapper.draft.calibration[5]), { type: 'pedal', released: -1, full: 1 }, 'a pedal gets its whole travel');
    await page.evaluate(() => { testPads[2].axes[5] = -1; testPads[2].axes[1] = 3.2857; });
    await control(page, 'UP');
    await page.click('#mapPanel button:has-text("Change")');
    await listening(page);
    await page.evaluate(() => { testPads[2].axes[1] = -1; });
    await heard(page);
    assert.deepEqual(await bindings(page, 'UP'), [{ kind: 'hat', index: 1, direction: 'UP', invert: false }]);
    await page.evaluate(() => { testPads[2].axes[1] = 3.2857; });
    console.log('PASS real browser: inputs tab, picking a stick, the range wizard, a pedal and a D-pad hat');

    // Polling stops while listening: listening stops. Back with changes asks once.
    await control(page, 'CROSS');
    await page.click('#mapPanel button:has-text("Change")');
    await listening(page);
    await page.evaluate(() => { window.failPolling = true; });
    await heard(page);
    await page.evaluate(() => { window.failPolling = false; });
    await page.click('#mapBack');
    assert.match(await page.textContent('#mapStatus'), /aren't saved/);
    await page.click('#mapBack');
    assert.equal(await page.locator('#mapView').isHidden(), true, 'the second Back throws the changes away');
    assert.equal(await page.evaluate(() => c4fTest.profileFor(c4fTest.gamepadSources.get(2)).id), 'default');
    await page.evaluate(() => { testPads[2] = { ...testPads[2], id: 'Replacement wheel' }; });
    await page.waitForFunction(() => c4fTest.gamepadSources.get(2).signature.id === 'Replacement wheel');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(2).profileId), 'default');
    console.log('PASS real browser: listening stops with polling, Back asks before throwing changes away, browser-index reuse');

    // Import a profile made for another gamepad: fit it, save it, and it's there after a reload.
    await page.evaluate(() => c4fTest.mapper.open(2));
    const imported = await page.evaluate(() => c4fTest.ControllerMapping.exportProfile(c4fTest.mapper.draft));
    imported.profile.signature.id = 'Other model';
    imported.profile.bindings.push({ ...imported.profile.bindings[0], input: { kind: 'button', index: 99, mode: 'digital' } });
    imported.profile.signature.buttons = 100;
    await page.setInputFiles('#mapImport', { name: 'import.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(imported)) });
    await page.waitForFunction(() => c4fTest.mapper.draft.signature.id === 'Other model');
    await page.click('#mapSave');
    assert.match(await page.textContent('#mapStatus'), /different gamepad/);
    await page.click('#mapBanner button:has-text("Fit it to this gamepad")');
    assert.equal(await page.evaluate(() => c4fTest.mapper.draft.bindings.some(b => b.input.index === 99)), false);
    await page.fill('#mapName', 'Imported wheel');
    await page.click('#mapSave');
    assert.equal(await page.evaluate(() => c4fTest.profileFor(c4fTest.gamepadSources.get(2)).name), 'Imported wheel');
    await page.click('#mapBack');
    await page.reload();
    await page.waitForFunction(() => c4fTest.gamepadSources.size === 3);
    assert.equal(await page.evaluate(() => c4fTest.profileFor(c4fTest.gamepadSources.get(0)).name), 'Shared paddles', 'saved profiles come back for the same kind of gamepad');
    assert(await page.evaluate(() => c4fTest.profileStore.profiles.some(p => p.name === 'Imported wheel')), 'the imported profile is kept');
    console.log('PASS real browser: import, fit to this gamepad, profile persistence');

    // Export, Save as new, Delete with its second press.
    await page.evaluate(() => c4fTest.mapper.open(0));
    await page.click('#mapMenuBtn');
    const downloadPromise = page.waitForEvent('download'); await page.click('#mapExport');
    const exported = JSON.parse(fs.readFileSync(await (await downloadPromise).path(), 'utf8'));
    assert.equal(exported.format, 'control4free-controller-profile'); assert(exported.profile.bindings.length);
    await page.fill('#mapName', 'Branched');
    await page.click('#mapMenuBtn'); await page.click('#mapSaveNew');
    const branched = await page.evaluate(() => c4fTest.gamepadSources.get(0).profileId);
    assert.notEqual(branched, originalId);
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(1).profileId), originalId, 'Save as new leaves the others');
    await page.click('#mapMenuBtn'); await page.click('#mapDelete');
    assert.equal(await page.evaluate(id => c4fTest.profileStore.profiles.some(p => p.id === id), branched), true, 'one press only asks');
    assert.equal(await page.textContent('#mapDelete'), 'Press again to delete it');
    await page.click('#mapDelete');
    assert.equal(await page.evaluate(id => c4fTest.profileStore.profiles.some(p => p.id === id), branched), false);
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(0).profileId), 'default');
    await page.locator('#mapView').evaluate(e => { e.scrollTop = 0; });
    await control(page, 'LS');
    await page.screenshot({ path: '/src/build/mapping-desktop.png' });
    await page.click('#mapTabs [data-tab="inputs"]');
    await page.click('.in-tile[data-input="button:0"]');
    assert.equal(await page.getAttribute('#mapTabs [data-tab="inputs"]', 'aria-selected'), 'true');
    await page.screenshot({ path: '/src/build/mapping-inputs.png' });
    await page.click('#mapTabs [data-tab="pad"]');
    await page.setViewportSize({ width: 390, height: 844 });
    await page.screenshot({ path: '/src/build/mapping-mobile.png', fullPage: true });
    assert(await page.evaluate(() => document.querySelector('#mapView').scrollWidth <= innerWidth), 'mobile horizontal overflow');
    await page.click('#mapList [data-target="CIRCLE"]');
    assert.equal(await page.evaluate(() => c4fTest.mapper.target), 'CIRCLE', 'the list picks controls on a phone');
    await page.setViewportSize({ width: 1440, height: 1000 });
    await page.click('#mapBack');
    console.log('PASS real browser: export, Save as new, Delete asks first, layouts on desktop and phone');

    // The keyboard on the same picture: modifiers refused, a key moves between controls.
    await page.click('#keysLink');
    assert.equal(await page.evaluate(() => c4fTest.mapper.source), 'keyboard');
    await control(page, 'CROSS');
    await page.click('#mapPanel .key-btn');
    await page.keyboard.press('Control');
    assert.equal(await page.evaluate(() => c4fTest.mapper.draft.CROSS), 'Space', 'Ctrl is refused');
    await page.keyboard.press('KeyQ');
    assert.equal(await page.evaluate(() => c4fTest.mapper.draft.CROSS), 'KeyQ');
    assert.equal(await page.evaluate(() => c4fTest.mapper.draft.L1), '', 'Q left L1');
    await page.keyboard.down('KeyQ');
    await page.waitForFunction(() => document.querySelector('.map-area [data-btn="CROSS"].on') !== null);
    await page.keyboard.up('KeyQ');
    await page.click('#mapSave');
    assert.equal(await page.evaluate(() => c4fTest.getKeys().CROSS), 'KeyQ');
    await page.click('#mapReset');
    await page.click('#mapSave');
    assert.equal(await page.evaluate(() => c4fTest.getKeys().CROSS), 'Space');
    await page.screenshot({ path: '/src/build/mapping-keys.png' });
    await page.click('#mapBack');
    await page.evaluate(() => {
      c4fTest.openPad(0);
      const pad = document.querySelector('#touchpad'), r = pad.getBoundingClientRect();
      const fields = { pointerId: 77, bubbles: true, clientX: r.x + r.width / 2, clientY: r.y + r.height / 2 };
      pad.dispatchEvent(new PointerEvent('pointerdown', fields)); pad.dispatchEvent(new PointerEvent('pointercancel', fields));
    });
    assert.equal(await page.evaluate(() => c4fTest.mergedState(0).buttons & 0x100000), 0, 'canceled gesture generated a click');
    console.log('PASS real browser: keys on the picture, modifier refused, a key moves between controls, canceled touch');

    // Pointer capture is on the pad view, so PS feedback must follow the input
    // state rather than relying on the browser's :active pseudo-class.
    const ps = page.locator('#psBtn'), psBox = await ps.boundingBox();
    await page.mouse.move(psBox.x + psBox.width / 2, psBox.y + psBox.height / 2);
    await page.mouse.down();
    await page.waitForFunction(() => document.querySelector('#psBtn').classList.contains('on'));
    await page.waitForFunction(() => new DOMMatrix(getComputedStyle(document.querySelector('#psBtn')).transform).a < .95);
    await page.screenshot({ path: '/src/build/ps-button-held.png' });
    await page.mouse.up();
    await page.waitForFunction(() => !document.querySelector('#psBtn').classList.contains('on'));
    await page.keyboard.down('KeyP');
    await page.waitForFunction(() => document.querySelector('#psBtn').classList.contains('on'));
    await page.waitForFunction(() => new DOMMatrix(getComputedStyle(document.querySelector('#psBtn')).transform).a < .95);
    assert.equal(await ps.evaluate(e => e.matches(':active')), false, 'keyboard feedback does not depend on :active');
    await page.keyboard.up('KeyP');
    await page.waitForFunction(() => !document.querySelector('#psBtn').classList.contains('on'));
    await page.evaluate(() => {
      c4fTest.setGamepadPad(0, 0);
      c4fTest.pollGamepads(performance.now()); // Let the assignment's held-input gate observe neutral first.
      testPads[0].buttons[16].value = 1;
    });
    await page.waitForFunction(() => document.querySelector('#psBtn').classList.contains('on'));
    await page.evaluate(() => { testPads[0].buttons[16].value = 0; });
    await page.waitForFunction(() => !document.querySelector('#psBtn').classList.contains('on'));
    await page.waitForFunction(() => getComputedStyle(document.querySelector('#psBtn')).transform === 'none');
    await page.screenshot({ path: '/src/build/ps-button-idle.png' });
    await page.evaluate(() => { c4fTest.mapper.open(0); testPads[0].buttons[16].value = 1; });
    await page.waitForFunction(() => document.querySelector('.map-area .ps-btn').classList.contains('on'));
    await page.waitForFunction(() => new DOMMatrix(getComputedStyle(document.querySelector('.map-area .ps-btn')).transform).a < .95);
    await page.evaluate(() => { testPads[0].buttons[16].value = 0; c4fTest.mapper.back(); });
    console.log('PASS real browser: PS press/release feedback from pointer, keyboard, gamepad and mapping tester');
    // Earlier capture/index-reuse checks release the second source. Connect it
    // again so ownership loss is tested with another player actually playing.
    await page.evaluate(() => { c4fTest.setGamepadPad(1, 1); c4fTest.setGamepadPad(2, 2); });
    await page.waitForFunction(() => c4fTest.status.slice(0, 3).every(p => p.mine) && !c4fTest.gamepadSources.get(1).waitNeutral);
    await page.evaluate(() => { testPads[0].buttons[16].value = 1; testPads[1].buttons[0].value = 1; });
    await page.waitForFunction(() => c4fTest.gamepadSources.get(0).buttons && c4fTest.gamepadSources.get(1).buttons);
    const otherButtons = await page.evaluate(() => c4fTest.gamepadSources.get(1).buttons);
    await page.evaluate(() => {
      const h = c4fTest;
      testClaims.length = 0;
      h.applyStatus({ pads: h.status.map((p, i) => i === 0 || i === 2 ? { ...p, state: 'free', mine: false, open: false, connected: false, clients: 0, user: '', uid: 'unassigned-00000000' } : { ...p }) });
    });
    assert.equal(await page.locator('#home').isVisible(), true, 'logout closes the controller screen');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(0).pad), -1, 'logout clears the physical source selection');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(0).buttons), 0, 'logout clears held input');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(2).pad), -1, 'logout clears another affected controller');
    assert.deepEqual(await page.evaluate(() => testClaims), [[1]], 'the remaining claim must not recreate a later logged-out slot');
    assert.equal(await page.getAttribute('#gamepadList .gp-row[data-index="0"] [data-focus="pad--1"]', 'aria-checked'), 'true');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(1).pad), 1, 'another player stays selected');
    assert.equal(await page.evaluate(() => c4fTest.gamepadSources.get(1).buttons), otherButtons, 'another player keeps their held input');
    await page.evaluate(() => { testPads[0].buttons[16].value = 0; testPads[1].buttons[0].value = 0; });
    console.log('PASS real browser: logout/ownership loss returns home and switches affected gamepads Off without dropping another player');
    assert.deepEqual(errors, []);

    // Actual served HTTP document works without instrumentation or grants.
    const plain = await browser.newContext(), smoke = await plain.newPage();
    await smoke.goto('http://127.0.0.1:4264/');
    await smoke.waitForFunction(() => document.querySelector('#conn').dataset.conn === 'open');
    await plain.close();
    const saved = await browser.newContext(), filePage = await saved.newPage();
    await saved.grantPermissions(['local-network-access']);
    await filePage.goto('file:///src/client/index.html');
    assert.equal(await filePage.locator('#addressForm').isVisible(), true, 'a saved copy asks for the address');
    await filePage.fill('#address', '127.0.0.1');
    await filePage.click('#addressForm button');
    await filePage.waitForFunction(() => document.querySelector('#conn').dataset.conn === 'open');
    await saved.close();

    await page.evaluate(() => localStorage.setItem('c4f.gamepadProfiles', '{"schema":99,"future":"preserve"}'));
    await page.reload(); await page.waitForFunction(() => !!window.c4fTest);
    await page.evaluate(() => c4fTest.persistProfiles());
    assert.equal(await page.evaluate(() => localStorage.getItem('c4f.gamepadProfiles')), '{"schema":99,"future":"preserve"}');
    await page.evaluate(() => {
      localStorage.setItem('c4f.gamepadProfiles', '{broken');
      localStorage.setItem('c4f.keymap', '{"CROSS":23,"CIRCLE":{},"SQUARE":"ControlLeft"}');
    });
    await page.reload(); await page.waitForFunction(() => c4fTest.gamepadSources.size === 3);
    assert.equal(await page.evaluate(() => c4fTest.profileStore.profiles.length), 0);
    assert.equal(await page.evaluate(() => c4fTest.getKeys().CROSS), 'Space');
    await page.evaluate(() => { Storage.prototype.setItem = () => { throw Error('quota'); }; c4fTest.persistProfiles(); });
    assert.match(await page.textContent('#toastText'), /session-only/);
    console.log('PASS real browser: the address box only for a saved copy, unknown/corrupt storage and session-only fallback');
  } finally { if (browser) await browser.close(); server.kill(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
