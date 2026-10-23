// Real DOM regressions for translation boundaries and the narrow-screen picker.
// No service or console: instrument the shipped page in memory and block network.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const { chromium } = require('playwright');
const hook = '\nwindow.c4fTest = { ControllerMapping, mapper, profileStore, gamepadSources, renderGamepads, renderPads, renderPadLabel, status, setPrimary: value => { primaryPad = value; } };\n';
const original = fs.readFileSync('client/index.html', 'utf8');
const html = original.replace('    connect(address);', '    // No signaling in this offline fixture.')
  .replace('\n  })();', hook + '  })();');
assert.notEqual(html, original);
const settle = page => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const width of [320, 360, 390, 1440]) {
      const context = await browser.newContext({ viewport: { width, height: 900 }, locale: 'pt-BR' });
      await context.addInitScript(() => {
        window.mockPad = { index: 0, id: 'Left', mapping: 'standard', connected: true,
          buttons: Array.from({ length: 18 }, () => ({ value: 0, pressed: false })), axes: [0, 0, 0, 0] };
        Object.defineProperty(navigator, 'getGamepads', { value: () => [window.mockPad] });
      });
      const page = await context.newPage(), errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', route => route.request().url() === 'http://c4f.test/'
        ? route.fulfill({ contentType: 'text/html', body: html }) : route.abort());
      await page.goto('http://c4f.test/');
      if (width === 1440) assert(await page.locator('.screen-sub').evaluate(e => e.getBoundingClientRect().width > 800),
        'introductory copy uses the available width instead of an English-sized character cap');
      await page.waitForFunction(() => window.c4fTest?.gamepadSources.size === 1);
      await page.click('#openSettings');
      await page.locator('#langSelect').scrollIntoViewIfNeeded();
      const bounds = await page.locator('#langSelect').evaluate(select => {
        const row = select.closest('.row'), a = select.getBoundingClientRect(), b = row.getBoundingClientRect();
        return { left: a.left, right: a.right, rowLeft: b.left, rowRight: b.right, scroll: row.scrollWidth, width: row.clientWidth };
      });
      assert(bounds.left >= bounds.rowLeft - 1 && bounds.right <= bounds.rowRight + 1, `${width}: picker fits ${JSON.stringify(bounds)}`);
      assert(bounds.scroll <= bounds.width + 1, `${width}: language row does not overflow`);

      // The actual observer must skip both raw text and attributes, even after mutation.
      await page.evaluate(() => {
        const box = document.createElement('div');
        box.id = 'rawFixture'; box.translate = false;
        box.innerHTML = '<span title="Left" aria-label="Settings">Left</span>';
        document.body.appendChild(box);
      });
      await settle(page);
      assert.deepEqual(await page.locator('#rawFixture span').evaluate(e => [e.textContent, e.title, e.getAttribute('aria-label')]), ['Left', 'Left', 'Settings']);
      await page.locator('#rawFixture span').evaluate(e => { e.textContent = 'Right'; e.title = 'Settings'; });
      await settle(page);
      assert.deepEqual(await page.locator('#rawFixture span').evaluate(e => [e.textContent, e.title]), ['Right', 'Settings']);

      for (const name of ['Left', 'Esquerda', 'Left. Right.', '<b>Settings</b>']) {
        await page.evaluate(name => {
          const t = c4fTest, src = t.gamepadSources.get(0);
          mockPad.id = name; src.signature.id = name;
          const profile = t.ControllerMapping.browserDefault(src.signature);
          profile.id = 'custom'; profile.name = name;
          t.profileStore.profiles = [profile]; src.profileId = profile.id;
          Object.assign(t.status[0], { known: true, user: name, state: 'ready', open: true, connected: true });
          t.renderPads(); t.renderGamepads(); t.setPrimary(0); t.renderPadLabel();
          t.mapper.open(0);
        }, name);
        await settle(page);
        assert.equal(await page.locator('.tile-name').first().textContent(), name);
        assert.equal(await page.locator('#gamepadList .gp-name').first().textContent(), name);
        assert.equal(await page.locator('#gamepadList .gp-profile').first().textContent(), name);
        assert.equal(await page.locator('#mapProfile option[value="custom"]').textContent(), name);
        assert.equal(await page.locator('#mapSource option[value="0"]').textContent(), 'Gamepad 1 · ' + name);
        assert.equal(await page.locator('#gamepadList [data-focus="pad-0"]').getAttribute('title'), 'Controle 1 · ' + name);
        assert((await page.locator('#padLabel').textContent()).startsWith('Controle 1 · ' + name));
        await page.evaluate(name => {
          c4fTest.mapper.draft.name = name;
          document.querySelector('#mapName').value = name;
          c4fTest.mapper.save(true);
        }, name);
        await settle(page);
        assert.equal(await page.locator('#mapStatus').textContent(), 'Salvo como ' + name + '.');
        await page.evaluate(() => c4fTest.mapper.remove());
        await settle(page);
        assert.equal(await page.locator('#mapStatus').textContent(), 'Aperte Excluir de novo para excluir ' + name + '.');
        await page.evaluate(() => { c4fTest.mapper.backArmed = 0; c4fTest.mapper.deleteArmed = 0; c4fTest.mapper.close(); });
      }
      assert.deepEqual(errors, [], `${width}: page errors`);
      await context.close();
      console.log(`PASS ${width}px: raw names, options, tooltips, messages, observer boundaries and language picker`);
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
