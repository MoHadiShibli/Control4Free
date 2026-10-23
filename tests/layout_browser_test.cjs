// Controller layout editing through real pointer events, in an offline page.
// Only the test's in-memory copy exposes state; the shipped page has no hooks.
const fs = require('node:fs');
const assert = require('node:assert/strict');

const faceButtons = ['TRIANGLE', 'CIRCLE', 'CROSS', 'SQUARE'];
const faceSelector = button => `#padView .face-btn[data-btn="${button}"]`;
const settle = async page => {
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  await page.waitForFunction(() => [...document.querySelectorAll('#padView .face-btn')].every(node => getComputedStyle(node).transform === 'none'));
};
const snapshot = (page, root = '#padView') => page.evaluate(({ buttons, root }) => Object.fromEntries(buttons.map(button => {
  const node = document.querySelector(`${root} .face-btn[data-btn="${button}"]`);
  const r = node.getBoundingClientRect();
  return [button, { x: r.x + r.width / 2, y: r.y + r.height / 2, width: r.width, height: r.height, hidden: node.hidden }];
})), { buttons: faceButtons, root });

async function drag(page, button, dx, dy) {
  const r = await page.locator(faceSelector(button)).boundingBox();
  assert(r && r.width > 0, button + ' has a visible hit target');
  await page.mouse.move(r.x + r.width / 2, r.y + r.height / 2);
  await page.mouse.down();
  await page.mouse.move(r.x + r.width / 2 + dx, r.y + r.height / 2 + dy, { steps: 5 });
  await page.mouse.up();
  await settle(page);
}

async function touchDrag(page, button, dx, dy) {
  const r = await page.locator(faceSelector(button)).boundingBox();
  const cdp = await page.context().newCDPSession(page);
  const x = r.x + r.width / 2, y = r.y + r.height / 2;
  const point = (x, y) => [{ x, y, id: 1, radiusX: 5, radiusY: 5, force: 1 }];
  try {
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: point(x, y) });
    for (let step = 1; step <= 5; step++)
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: point(x + dx * step / 5, y + dy * step / 5) });
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await settle(page);
  } finally { await cdp.detach(); }
}

function closeTo(actual, expected, label, tolerance = 1) {
  assert(Math.abs(actual - expected) <= tolerance, `${label}: expected ${expected}, got ${actual}`);
}

function sameButtons(actual, expected, except = '') {
  for (const button of faceButtons.filter(button => button !== except)) for (const key of ['x', 'y', 'width', 'height'])
    closeTo(actual[button][key], expected[button][key], button + ' ' + key);
}

async function resize(page, percent) {
  await page.locator('#lbSize').evaluate((slider, percent) => {
    slider.value = percent; slider.dispatchEvent(new Event('input', { bubbles: true }));
  }, String(percent));
  await settle(page);
}

async function checkComponentResets(browser, fixture) {
  const context = await browser.newContext({ viewport: { width: 1000, height: 600 }, locale: 'en-US' });
  const page = await context.newPage(), errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('**/*', fixture);
  const layout = () => page.evaluate(() => c4fLayoutTest.getLayout());
  const select = id => page.evaluate(id => c4fLayoutTest.editor.select(id), id);
  const open = () => page.evaluate(() => c4fLayoutTest.editor.open());
  async function move(id, dx, dy) {
    const r = await page.locator(`#padView [data-ctl="${id}"]`).boundingBox();
    await page.mouse.move(r.x + r.width / 2, r.y + r.height / 2);
    await page.mouse.down();
    await page.mouse.move(r.x + r.width / 2 + dx, r.y + r.height / 2 + dy, { steps: 5 });
    await page.mouse.up();
    await settle(page);
  }
  try {
    await page.goto('http://layout.test/');
    await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => c4fLayoutTest.openPad(0));
    await settle(page);
    const defaults = await layout(), defaultButtons = await snapshot(page);
    await open();
    await select('r1'); await resize(page, 125); await page.click('#lbHide');
    // Buttons, sticks, D-pad and touchpad all use the same scoped Reset action.
    for (const id of ['l1', 'lstick', 'dpad', 'touchpad']) {
      await move(id, 30, 15); await select(id); await resize(page, 150); await page.click('#lbHide');
      const before = await layout();
      await page.click('#lbResetControl');
      const after = await layout();
      assert.deepEqual(after[id], defaults[id], id + ': Reset restores position, size and visibility');
      for (const other of Object.keys(before).filter(other => other !== id))
        assert.deepEqual(after[other], before[other], id + ': Reset preserves ' + other);
      assert.equal(await page.locator('#lbSize').inputValue(), '100');
      assert.equal(await page.evaluate(() => c4fLayoutTest.editor.selected), id);
    }
    // A local reset is still an unsaved edit: Cancel restores saved custom data.
    await select('l1'); await resize(page, 150); await page.click('#lbHide');
    await page.click('#lbDone');
    const saved = await layout();
    await open(); await select('l1'); await page.click('#lbResetControl'); await page.click('#lbCancel');
    assert.deepEqual(await layout(), saved, 'Cancel undoes a component reset');
    await open(); await select('l1'); await page.click('#lbResetControl'); await page.click('#lbDone');
    const resetSaved = await layout();
    assert.deepEqual(resetSaved.l1, defaults.l1);
    assert.deepEqual(resetSaved.r1, saved.r1);
    await page.reload(); await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => c4fLayoutTest.openPad(0)); await settle(page);
    assert.deepEqual(await layout(), resetSaved, 'Done persists only the component reset across reload');

    // Group scaling and rebasing must not make an individual reset move siblings.
    await open(); await select('face'); await resize(page, 150);
    await page.selectOption('#lbFaceMode', 'separate');
    await drag(page, 'CROSS', -100, 15);
    await select('face:CROSS'); await resize(page, 125); await page.click('#lbHide');
    await select('face:TRIANGLE'); await page.click('#lbHide');
    const custom = await layout(), customButtons = await snapshot(page);
    await select('face:CROSS'); await page.click('#lbResetControl');
    const reset = await layout(), resetButtons = await snapshot(page);
    sameButtons(resetButtons, customButtons, 'CROSS');
    for (const key of ['x', 'y', 'width', 'height'])
      closeTo(resetButtons.CROSS[key], defaultButtons.CROSS[key], 'Cross reset restores standard physical ' + key);
    assert.equal(reset.face.buttons.CROSS.hide, false);
    assert.equal(reset.face.buttons.TRIANGLE.hide, true);
    assert.equal(reset.face.separate, true, 'scoped reset preserves separate editing');
    for (const id of Object.keys(reset).filter(id => id !== 'face')) assert.deepEqual(reset[id], custom[id]);
    await page.click('#lbDone');
    assert.equal(await page.locator(faceSelector('TRIANGLE')).isHidden(), true);
    assert.equal(await page.locator(faceSelector('CROSS')).isVisible(), true);
    const faceSaved = await layout();
    await page.reload(); await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => c4fLayoutTest.openPad(0)); await settle(page);
    assert.deepEqual(await layout(), faceSaved, 'compensated individual size and position survive reload');

    // Resetting a group resets its members while preserving its editing mode.
    await open(); await select('face'); await page.click('#lbResetControl');
    assert.deepEqual((await layout()).face, { ...defaults.face, separate: true });
    for (const mode of ['together', 'separate']) {
      await page.selectOption('#lbFaceMode', mode); await select('face'); await resize(page, 125);
      await page.click('#lbResetControl');
      assert.deepEqual((await layout()).face, { ...defaults.face, separate: mode === 'separate' });
    }
    // One child can be restored from a hidden group without exposing its siblings.
    await page.click('#lbHide'); await select('face:CROSS'); await page.click('#lbResetControl');
    await page.click('#lbDone');
    assert.equal(await page.locator(faceSelector('CROSS')).isVisible(), true);
    for (const button of faceButtons.filter(button => button !== 'CROSS'))
      assert.equal(await page.locator(faceSelector(button)).isHidden(), true, button + ': hidden sibling stays hidden');
    assert.equal(await page.evaluate(() => c4fLayoutTest.screenSource.buttons), 0, 'Reset never presses controller inputs');
    assert.deepEqual(errors, []);
    console.log('PASS component Reset: default position/size/visibility, unchanged siblings, scaled face groups, hidden groups, mode preservation, Cancel and saved reload');
  } finally { await context.close(); }
}

module.exports = async function checkControllerLayout(browser) {
  // The fixture deliberately does not connect or create a virtual controller.
  const hook = '\nwindow.c4fLayoutTest = { editor, openPad, mapper, getLayout: () => JSON.parse(JSON.stringify(layout)), screenSource, mergedState };\n';
  const original = fs.readFileSync('/src/client/index.html', 'utf8');
  const html = original.replace('    connect(address);', '    // No signaling in this offline fixture.')
    .replace('\n  })();', hook + '  })();');
  assert.notEqual(html, original);
  const fixture = route => {
    const url = route.request().url();
    if (/^http:\/\/layout\.test\/(?:\?.*)?$/.test(url)) return route.fulfill({ contentType: 'text/html', body: html });
    const font = url === 'http://layout.test/fonts/arabic-light.otf' ? 'SSTArabic-Light.otf'
      : url === 'http://layout.test/fonts/arabic-regular.otf' ? 'SSTArabic-Roman.otf' : '';
    // Optional private fixtures validate the real console font without copying
    // Sony data into the repository or requiring console/network access.
    if (font && process.env.C4F_TEST_PS4_FONTS) return route.fulfill({ contentType: 'font/otf',
      body: fs.readFileSync(process.env.C4F_TEST_PS4_FONTS + '/' + font) });
    return route.abort();
  };
  const context = await browser.newContext({ viewport: { width: 1000, height: 600 }, locale: 'en-US' });
  const page = await context.newPage(), errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('**/*', fixture);
  try {
    await page.goto('http://layout.test/');
    await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => c4fLayoutTest.openPad(0));
    await settle(page);
    await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
    assert.equal(await page.locator('#lbFaceMode').inputValue(), 'together', 'existing layouts keep grouped editing by default');
    let before = await snapshot(page);
    await drag(page, 'TRIANGLE', -80, -20);
    let after = await snapshot(page);
    for (const button of faceButtons) {
      closeTo(after[button].x - before[button].x, -80, button + ' grouped horizontal drag', 3);
      closeTo(after[button].y - before[button].y, -20, button + ' grouped vertical drag', 3);
    }
    assert.equal(await page.evaluate(() => c4fLayoutTest.screenSource.buttons), 0, 'editing does not press game buttons');

    before = after;
    await page.selectOption('#lbFaceMode', 'separate');
    await settle(page);
    sameButtons(await snapshot(page), before);
    await drag(page, 'CROSS', 20, 25);
    after = await snapshot(page);
    sameButtons(after, before, 'CROSS');
    closeTo(after.CROSS.x - before.CROSS.x, 20, 'Cross independent horizontal drag', 3);
    closeTo(after.CROSS.y - before.CROSS.y, 25, 'Cross independent vertical drag', 3);
    assert.equal(await page.evaluate(() => c4fLayoutTest.editor.selected), 'face:CROSS');
    assert.equal(await page.evaluate(() => c4fLayoutTest.screenSource.buttons), 0, 'independent editing cannot generate input');
    before = after;
    await resize(page, 150);
    after = await snapshot(page);
    sameButtons(after, before, 'CROSS');
    closeTo(after.CROSS.width / before.CROSS.width, 1.5, 'Cross independent size', .02);
    await page.click('#lbDone');
    const saved = await page.evaluate(() => c4fLayoutTest.getLayout());
    assert.equal(saved.face.separate, true);
    assert.equal(saved.face.buttons.CROSS.size, 150);
    const savedBounds = await snapshot(page);
    assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem('c4f.layout.landscape'))), saved);
    await page.reload();
    await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => c4fLayoutTest.openPad(0));
    await settle(page);

    if (process.env.C4F_LAYOUT_PREVIEW_DIR) {
      fs.mkdirSync(process.env.C4F_LAYOUT_PREVIEW_DIR, { recursive: true });
      await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face:CROSS'); document.querySelector('#toast').style.visibility = 'hidden'; });
      await page.screenshot({ path: process.env.C4F_LAYOUT_PREVIEW_DIR + '/en-layout-landscape.png' });
      await page.click('#lbCancel');
    }
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), saved, 'individual positions and sizes survive a reload');
    sameButtons(await snapshot(page), savedBounds);

    // The mapping picture is a standard DS4 reference, regardless of the
    // persisted touch layout that was cloned when this document initialized.
    await page.evaluate(() => c4fLayoutTest.mapper.open('keyboard'));
    await settle(page);
    assert.equal(await page.locator('#mapView').isVisible(), true);
    const picture = await snapshot(page, '#mapView');
    for (const button of faceButtons) {
      closeTo(picture[button].width, picture.TRIANGLE.width, button + ': mapping picture retains standard size');
      closeTo(picture[button].height, picture.TRIANGLE.height, button + ': mapping picture retains standard height');
    }
    closeTo(picture.TRIANGLE.x, picture.CROSS.x, 'mapping picture vertical column');
    closeTo(picture.SQUARE.y, picture.CIRCLE.y, 'mapping picture horizontal row');
    closeTo(picture.TRIANGLE.x, (picture.SQUARE.x + picture.CIRCLE.x) / 2, 'mapping picture horizontal centre');
    closeTo(picture.SQUARE.y, (picture.TRIANGLE.y + picture.CROSS.y) / 2, 'mapping picture vertical centre');
    assert(picture.TRIANGLE.y < picture.CROSS.y && picture.SQUARE.x < picture.CIRCLE.x, 'mapping picture retains its standard diamond');
    await page.evaluate(() => c4fLayoutTest.mapper.close());
    await settle(page);
    assert.equal(await page.locator('#padView').isVisible(), true);
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), saved, 'leaving mapping retains the saved custom touch arrangement');
    sameButtons(await snapshot(page), savedBounds);

    // The physical buttons retain their original input bits after repositioning.
    for (const [button, bit] of [['TRIANGLE', 0x1000], ['CIRCLE', 0x2000], ['CROSS', 0x4000], ['SQUARE', 0x8000]]) {
      const r = await page.locator(faceSelector(button)).boundingBox();
      await page.mouse.move(r.x + r.width / 2, r.y + r.height / 2);
      await page.mouse.down();
      assert.equal(await page.evaluate(() => c4fLayoutTest.screenSource.buttons), bit, button + ' keeps its input mapping');
      await page.mouse.up();
      await page.waitForFunction(() => c4fLayoutTest.screenSource.buttons === 0);
    }
    await settle(page);

    // Regrouping must keep the custom arrangement and transform it as one unit.
    await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
    before = await snapshot(page);
    await page.selectOption('#lbFaceMode', 'together');
    await settle(page);
    sameButtons(await snapshot(page), before);
    await drag(page, 'TRIANGLE', -35, -15);
    after = await snapshot(page);
    for (const button of faceButtons) {
      closeTo(after[button].x - before[button].x, -35, button + ' regrouped horizontal drag', 3);
      closeTo(after[button].y - before[button].y, -15, button + ' regrouped vertical drag', 3);
    }
    before = after;
    await resize(page, 125);
    after = await snapshot(page);
    for (const button of faceButtons) closeTo(after[button].width / before[button].width, 1.25, button + ' group resize', .02);
    await page.click('#lbCancel');
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), saved, 'Cancel restores mode, child positions and sizes');
    sameButtons(await snapshot(page), savedBounds);

    // Individual hiding affects one hit target and can be undone from the editor.
    await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face:CROSS'); });
    await page.click('#lbHide');
    await page.click('#lbDone');
    assert.equal(await page.locator(faceSelector('CROSS')).isHidden(), true);
    for (const button of faceButtons.filter(button => button !== 'CROSS')) assert.equal(await page.locator(faceSelector(button)).isVisible(), true);
    await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face:CROSS'); });
    assert.equal(await page.locator(faceSelector('CROSS')).isVisible(), true, 'hidden buttons remain editable');
    await page.click('#lbHide');
    await page.click('#lbDone');

    // Leaving the orientation saves the active edit in its original orientation.
    await page.evaluate(() => c4fLayoutTest.editor.open());
    await drag(page, 'CROSS', 20, -10);
    const landscape = await page.evaluate(() => c4fLayoutTest.getLayout());
    await page.setViewportSize({ width: 600, height: 1000 });
    await page.waitForFunction(() => !c4fLayoutTest.editor.active && matchMedia('(orientation: portrait)').matches);
    assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem('c4f.layout.landscape'))), landscape);
    assert.equal(await page.evaluate(() => c4fLayoutTest.getLayout().face.separate), false, 'portrait starts with its own grouped default');
    if (await page.locator('#portraitOk').isVisible()) await page.click('#portraitOk');
    await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
    await page.selectOption('#lbFaceMode', 'separate');
    await drag(page, 'SQUARE', -20, 25);
    await page.click('#lbDone');
    const portrait = await page.evaluate(() => c4fLayoutTest.getLayout());
    await page.setViewportSize({ width: 1000, height: 600 });
    await page.waitForFunction(() => !matchMedia('(orientation: portrait)').matches);
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), landscape, 'landscape retains its separate positions');
    assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem('c4f.layout.portrait'))), portrait);

    // Reset remains a two-tap operation; Cancel also restores a reset layout.
    await page.evaluate(() => c4fLayoutTest.editor.open());
    await page.click('#lbReset');
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), landscape, 'first reset tap is non-destructive');
    await page.click('#lbReset');
    assert.equal(await page.evaluate(() => c4fLayoutTest.getLayout().face.separate), false);
    await page.click('#lbCancel');
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), landscape);
    await page.evaluate(() => c4fLayoutTest.editor.open());
    await page.click('#lbReset'); await page.click('#lbReset'); await page.click('#lbDone');
    assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem('c4f.layout.landscape')).face.separate), false, 'Done persists the reset');

    // Old layout records have no child metadata: preserve their grouped position.
    await page.evaluate(() => localStorage.setItem('c4f.layout.landscape', JSON.stringify({ face: { x: 73, y: 51, w: 32, h: 32, hide: false } })));
    await page.reload(); await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => { c4fLayoutTest.openPad(0); c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
    const legacy = await page.evaluate(() => c4fLayoutTest.getLayout().face);
    assert.deepEqual([legacy.x, legacy.y, legacy.w, legacy.h, legacy.hide, legacy.separate], [73, 51, 32, 32, false, false]);
    before = await snapshot(page);
    await page.selectOption('#lbFaceMode', 'separate');
    await settle(page);
    sameButtons(await snapshot(page), before);

    // Individually moved controls and their regrouped bounds stay on screen.
    await drag(page, 'CROSS', -2000, -2000);
    const edge = await snapshot(page);
    assert(edge.CROSS.x - edge.CROSS.width / 2 >= -.6 && edge.CROSS.y - edge.CROSS.height / 2 >= -.6, 'individual drag clamps the entire button');
    sameButtons(edge, before, 'CROSS');
    await page.selectOption('#lbFaceMode', 'together');
    await drag(page, 'CROSS', 30, 30);
    const outsideDrag = await snapshot(page);
    for (const button of faceButtons) {
      closeTo(outsideDrag[button].x - edge[button].x, 30, button + ': grouped drag starts on an out-of-parent child', 3);
      closeTo(outsideDrag[button].y - edge[button].y, 30, button + ': out-of-parent child moves the group vertically', 3);
    }
    await drag(page, 'TRIANGLE', 2000, 2000);
    for (const [button, r] of Object.entries(await snapshot(page)))
      assert(r.x - r.width / 2 >= -.6 && r.y - r.height / 2 >= -.6 && r.x + r.width / 2 <= 1000.6 && r.y + r.height / 2 <= 600.6, button + ': regrouped custom arrangement stays on screen');
    await resize(page, 200);
    for (const [button, r] of Object.entries(await snapshot(page)))
      assert(r.x - r.width / 2 >= -.6 && r.y - r.height / 2 >= -.6 && r.x + r.width / 2 <= 1000.6 && r.y + r.height / 2 <= 600.6, button + ': grouped resize clamps the full custom arrangement');
    await page.click('#lbCancel');
    const stable = await page.evaluate(() => c4fLayoutTest.getLayout());
    await page.setViewportSize({ width: 1200, height: 700 });
    await settle(page);
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), stable, 'same-orientation resize preserves the saved layout');
    await page.setViewportSize({ width: 900, height: 620 });
    await settle(page);
    assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), stable);
    for (const [button, r] of Object.entries(await snapshot(page)))
      assert(r.x - r.width / 2 >= -.6 && r.y - r.height / 2 >= -.6 && r.x + r.width / 2 <= 900.6 && r.y + r.height / 2 <= 620.6, button + ': same-orientation resize keeps the hit target in view');

    // A desktop arrangement spanning most of the screen also fits a phone in
    // landscape, where --u and the viewport change by different proportions.
    await page.setViewportSize({ width: 1920, height: 1080 });
    await page.evaluate(() => localStorage.removeItem('c4f.layout.landscape'));
    await page.reload(); await page.waitForFunction(() => !!window.c4fLayoutTest);
    await page.evaluate(() => { c4fLayoutTest.openPad(0); c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
    await page.selectOption('#lbFaceMode', 'separate');
    await drag(page, 'CROSS', -5000, 0);
    await page.click('#lbDone');
    await page.setViewportSize({ width: 844, height: 390 });
    await settle(page);
    for (const [button, r] of Object.entries(await snapshot(page))) {
      assert(r.x - r.width / 2 >= -.6 && r.y - r.height / 2 >= -.6 && r.x + r.width / 2 <= 844.6 && r.y + r.height / 2 <= 390.6, button + ': saved desktop edge position remains accessible on a landscape phone');
      assert.equal(await page.evaluate(({ button, x, y }) => document.elementFromPoint(x, y)?.closest('.face-btn')?.dataset.btn === button,
        { button, x: r.x, y: r.y }), true, button + ': phone button centre receives pointer input');
    }

    // A hidden ancestor produces zero child rectangles, even when the child
    // itself has no hidden attribute. Safe-area offsets must not turn those
    // zero rectangles into false overflow and repeatedly move the saved group.
    await page.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
    await page.click('#lbHide'); await page.click('#lbDone');
    assert.equal(await page.locator('#padView [data-ctl="face"]').isHidden(), true);
    const hiddenLayout = await page.evaluate(() => c4fLayoutTest.getLayout());
    await page.locator('#padView .pad-area').evaluate(node => { node.style.inset = '20px'; });
    for (const [width, height] of [[1000, 500], [900, 450], [1200, 700], [844, 390], [1000, 500], [844, 390]]) {
      await page.setViewportSize({ width, height });
      await settle(page);
      assert.deepEqual(await page.evaluate(() => c4fLayoutTest.getLayout()), hiddenLayout,
        'hidden face group never drifts during viewport resize with 20px safe-area inset');
    }
    assert.deepEqual(errors, []);
    console.log('PASS controller layout: grouped compatibility, separate drag/size/hide and input, regrouping, save/reload, Cancel/reset, orientation, landscape phone fitting and hidden safe-area stability');
  } finally { await context.close(); }

  await checkComponentResets(browser, fixture);

  // Compact editor controls fit translated labels; controller geometry stays LTR.
  for (const locale of ['en-US', 'pt-BR', 'ar-SA']) for (const width of [320, 390, 1000]) {
    const narrow = await browser.newContext({ viewport: { width, height: width === 1000 ? 600 : 900 }, locale, hasTouch: width < 1000 });
    const p = await narrow.newPage();
    await p.route('**/*', fixture);
    try {
      await p.goto('http://layout.test/'); await p.waitForFunction(() => !!window.c4fLayoutTest);
      if (locale === 'ar-SA') {
        await p.evaluate(() => c4fI18n.loadConsoleFonts('http://layout.test/'));
        if (process.env.C4F_TEST_PS4_FONTS) assert.deepEqual(await p.evaluate(() =>
          [...document.fonts].filter(font => font.family.replaceAll(String.fromCharCode(34), '') === 'SST Arabic' && font.status === 'loaded').map(font => font.weight).sort()), ['300', '400'],
          'both actual console font weights load before Arabic panel measurements');
        await p.evaluate(() => document.fonts.ready);
      }
      await p.evaluate(() => c4fLayoutTest.openPad(0));
      if (await p.locator('#portraitOk').isVisible()) await p.click('#portraitOk');
      await p.evaluate(() => { c4fLayoutTest.editor.open(); c4fLayoutTest.editor.select('face'); });
      await p.selectOption('#lbFaceMode', 'separate');
      for (const id of [...Object.keys(await p.evaluate(() => c4fLayoutTest.getLayout())), ...faceButtons.map(button => 'face:' + button)]) {
        await p.evaluate(id => c4fLayoutTest.editor.select(id), id);
        const r = await p.locator('#layoutPanel').boundingBox();
        assert(r.x >= -1 && r.x + r.width <= width + 1, locale + ': ' + id + ' panel fits ' + width + 'px');
        assert.equal(await p.locator('#layoutPanel').evaluate(node => node.scrollWidth <= node.clientWidth + 1), true,
          locale + ': ' + id + ' panel has no internal overflow');
        assert.equal(await p.locator('#lbResetControl').isVisible(), true, locale + ': ' + id + ' has its own Reset');
      }
      await p.evaluate(() => c4fLayoutTest.editor.select('face:CROSS'));
      await settle(p);
      const panel = await p.locator('#layoutPanel').boundingBox();
      assert(panel.x >= -1 && panel.x + panel.width <= width + 1, locale + ': selection panel fits ' + width + 'px');
      assert.equal(await p.locator('#layoutPanel').evaluate(e => e.scrollWidth <= e.clientWidth + 1), true, locale + ': no internal panel overflow');
      assert.equal(await p.locator('#lbResetControl').isVisible(), true, locale + ': component Reset is visible beside Hide');
      assert.equal(await p.locator('#lbFaceMode').evaluate(select => {
        const style = getComputedStyle(select), canvas = document.createElement('canvas');
        const context = canvas.getContext('2d'); context.font = style.font;
        const text = Math.max(...[...select.options].map(option => context.measureText(option.textContent).width));
        const padding = parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
        return text + padding + 20 <= select.clientWidth + 1;
      }), true, locale + ': full mode labels fit beside the native selector arrow');
      const positions = await snapshot(p);
      assert(positions.TRIANGLE.y < positions.CROSS.y && positions.SQUARE.x < positions.CIRCLE.x, locale + ': face button positions never mirror');
      await drag(p, 'CROSS', -25, 15);
      const moved = await snapshot(p);
      sameButtons(moved, positions, 'CROSS');
      closeTo(moved.CROSS.x - positions.CROSS.x, -25, locale + ': physical horizontal drag', 2);
      if (width === 320) {
        await touchDrag(p, 'CROSS', 15, 10);
        const touched = await snapshot(p);
        sameButtons(touched, moved, 'CROSS');
        closeTo(touched.CROSS.x - moved.CROSS.x, 15, locale + ': real touch independently moves Cross horizontally', 2);
        closeTo(touched.CROSS.y - moved.CROSS.y, 10, locale + ': real touch independently moves Cross vertically', 2);
        assert.equal(await p.evaluate(() => c4fLayoutTest.screenSource.buttons), 0, 'touch editing does not press Cross');
      }
      if (process.env.C4F_LAYOUT_PREVIEW_DIR && locale !== 'pt-BR') {
        await p.evaluate(() => document.querySelector('#toast').style.visibility = 'hidden');
        await p.screenshot({ path: process.env.C4F_LAYOUT_PREVIEW_DIR + '/' + (locale === 'ar-SA' ? 'ar' : 'en') + '-layout-' + width + '.png' });
      }
      await p.click('#lbDone');
      assert.equal(await p.evaluate(() => c4fLayoutTest.getLayout().face.separate), true);
    } finally { await narrow.close(); }
  }
  console.log('PASS controller layout: English/Portuguese/Arabic controls fit 320/390px and landscape; real touch dragging preserves physical directions and input neutrality');
};

if (require.main === module) (async () => {
  const browser = await require('playwright').chromium.launch({ headless: true });
  try { await module.exports(browser); } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
