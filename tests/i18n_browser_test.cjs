// Real DOM regressions for translation boundaries and the narrow-screen picker.
// No service or console: instrument the shipped page in memory and block network.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const { chromium } = require('playwright');
const hook = '\nwindow.c4fTest = { ControllerMapping, mapper, profileStore, gamepadSources, renderGamepads, renderPads, renderPadLabel, status, keymap, setPrimary: value => { primaryPad = value; } };\n';
const original = fs.readFileSync('client/index.html', 'utf8');
const html = original.replace('    connect(address);', '    // No signaling in this offline fixture.')
  .replace('\n  })();', hook + '  })();');
assert.notEqual(html, original);
const clean = value => value.replace(/[\u2066-\u2069]/g, '');
const settle = page => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const locale of ['en-US', 'pt-BR', 'ar-SA']) for (const width of [320, 360, 390, 1440]) {
      const language = locale.startsWith('ar') ? 'ar' : locale.startsWith('pt') ? 'pt-BR' : 'en';
      const controller = language === 'ar' ? 'يد التحكم' : language === 'pt-BR' ? 'Controle' : 'Controller';
      const gamepad = language === 'ar' ? 'يد متصلة' : 'Gamepad';
      const context = await browser.newContext({ viewport: { width, height: 900 }, locale });
      await context.addInitScript(() => {
        window.mockPad = { index: 0, id: 'Left', mapping: 'standard', connected: true,
          buttons: Array.from({ length: 18 }, () => ({ value: 0, pressed: false })), axes: [0, 0, 0, 0] };
        Object.defineProperty(navigator, 'getGamepads', { value: () => [window.mockPad] });
      });
      const page = await context.newPage(), errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', route => {
        const url = route.request().url();
        if (url === 'http://c4f.test/') return route.fulfill({ contentType: 'text/html', body: html });
        const font = url.endsWith('/fonts/arabic-light.otf') ? 'SSTArabic-Light.otf' : url.endsWith('/fonts/arabic-regular.otf') ? 'SSTArabic-Roman.otf' : '';
        if (font && process.env.C4F_TEST_PS4_FONTS) return route.fulfill({ contentType: 'font/otf', body: fs.readFileSync(process.env.C4F_TEST_PS4_FONTS + '/' + font) });
        return route.abort();
      });
      await page.goto('http://c4f.test/');
      if (width === 1440) assert(await page.locator('.screen-sub').evaluate(e => e.getBoundingClientRect().width > 800),
        'introductory copy uses the available width instead of an English-sized character cap');
      await page.waitForFunction(() => window.c4fTest?.gamepadSources.size === 1);
      assert.equal(await page.evaluate(() => c4fTest.keymap.CROSS), 'Space', 'keyboard event codes never follow the interface language');
      if (language === 'ar') {
        // The normal CI fixture denies font endpoints: embedded offline fallback remains usable.
        // Optional local fixture uses console-supplied files without redistributing Sony bytes.
        await page.evaluate(() => c4fI18n.loadConsoleFonts('http://c4f.test/'));
        if (process.env.C4F_TEST_PS4_FONTS) assert.deepEqual(await page.evaluate(() =>
          [...document.fonts].filter(font => font.family.replaceAll(String.fromCharCode(34), '') === 'SST Arabic' && font.status === 'loaded').map(font => font.weight).sort()), ['300', '400'],
          'both console SST weights are actually loaded, rather than only available through fallback');
        await page.evaluate(() => document.fonts.ready);
        await settle(page);
      }
      assert.equal(clean(await page.locator('html').getAttribute('lang')), language);
      assert.equal(clean(await page.locator('html').getAttribute('dir')), language === 'ar' ? 'rtl' : 'ltr');
      assert.equal(clean(await page.locator('#openSettings').getAttribute('aria-label')), language === 'ar' ? 'الإعدادات' : language === 'pt-BR' ? 'Configurações' : 'Settings');
      if (language === 'ar' && (width === 390 || width === 1440)) {
        fs.mkdirSync('dev/web-preview', { recursive: true });
        await page.evaluate(() => { document.querySelector('#toast').classList.remove('show'); document.querySelector('#toast').style.visibility = 'hidden'; });
        await page.screenshot({ path: 'dev/web-preview/ar-home-' + width + '.png' });
      }
      await page.click('#openSettings');
      await page.locator('#langSelect').scrollIntoViewIfNeeded();
      await settle(page);
      const bounds = await page.locator('#langSelect').evaluate(select => {
        const row = select.closest('.row'), a = select.getBoundingClientRect(), b = row.getBoundingClientRect();
        return { left: a.left, right: a.right, rowLeft: b.left, rowRight: b.right, scroll: row.scrollWidth, width: row.clientWidth };
      });
      assert(bounds.left >= bounds.rowLeft - 1 && bounds.right <= bounds.rowRight + 1, `${width}: picker fits ${JSON.stringify(bounds)}`);
      assert(bounds.scroll <= bounds.width + 1, `${width}: language row does not overflow`);

      if (language === 'ar' && (width === 390 || width === 1440)) {
        await page.screenshot({ path: 'dev/web-preview/ar-settings-' + width + '.png' });
        for (const section of ['keyboard', 'gamepads', 'experimental']) {
          await page.click('[data-page="' + section + '"]');
          await page.screenshot({ path: 'dev/web-preview/ar-' + section + '-' + width + '.png' });
        }
      }
      // Runtime data has no localization observer, including after later mutations.
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

      for (const name of ['Left', 'Esquerda', 'Left. Right.', '<b>Settings</b>', 'أحمد DS4']) {
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
        if (language === 'ar' && width === 1440 && name === 'أحمد DS4') await page.screenshot({path:'dev/web-preview/ar-mapping-desktop.png'});
        assert.equal(clean(await page.locator('.tile-name').first().textContent()), name);
        assert.equal(clean(await page.locator('#gamepadList .gp-name').first().textContent()), name);
        assert.equal(clean(await page.locator('#gamepadList .gp-profile').first().textContent()), name);
        assert.equal(clean(await page.locator('#mapProfile option[value="custom"]').textContent()), name);
        assert.equal(clean(await page.locator('#mapSource option[value="0"]').textContent()), gamepad + ' 1 · ' + name);
        assert.equal(clean(await page.locator('#gamepadList [data-focus="pad-0"]').getAttribute('title')), controller + ' 1 · ' + name);
        assert(clean(await page.locator('#padLabel').textContent()).startsWith(controller + ' 1 · ' + name));
        await page.evaluate(name => {
          c4fTest.mapper.draft.name = name;
          document.querySelector('#mapName').value = name;
          c4fTest.mapper.save(true);
        }, name);
        await settle(page);
        assert.equal(clean(await page.locator('#mapStatus').textContent()), language === 'ar' ? 'حُفظ باسم ' + name + '.' : language === 'pt-BR' ? 'Salvo como ' + name + '.' : 'Saved as ' + name + '.');
        await page.evaluate(() => c4fTest.mapper.remove());
        await settle(page);
        assert.equal(clean(await page.locator('#mapStatus').textContent()), language === 'ar' ? 'اضغط «حذف» مجددًا لحذف ' + name + '.' : language === 'pt-BR' ? 'Aperte Excluir de novo para excluir ' + name + '.' : 'Press Delete again to delete ' + name + '.');
        await page.evaluate(() => { c4fTest.mapper.backArmed = 0; c4fTest.mapper.deleteArmed = 0; c4fTest.mapper.close(); });
      }
      const mapping = await page.evaluate(() => {
        const m = c4fTest.mapper;
        m.open(0);
        m.draft = c4fTest.ControllerMapping.browserDefault(c4fTest.gamepadSources.get(0).signature);
        m.draft.bindings.find(binding => binding.output === 'lx').invert = true;
        m.target = 'LS'; m.render();
        const reversed = document.querySelector('#mapPanel .map-row-label').textContent;
        m.draft.calibration[1] = { type: 'pedal', released: -1, full: 1 };
        m.tab = 'inputs'; m.render();
        const pedal = document.querySelector('#mapInputs [data-input="axis:1"] .in-name').textContent;
        m.openInput({kind: 'button', index: 0});
        const output = document.querySelector('#mapPanel .map-section h4').textContent;
        const type = m.draft.calibration[1].type;
        m.close();
        return { reversed, pedal, output, type };
      });
      assert(mapping.reversed.includes(language === 'ar' ? 'معكوس' : language === 'pt-BR' ? 'invertido' : 'reversed'));
      assert(mapping.pedal.includes(language === 'ar' ? 'دواسة' : 'pedal'));
      assert.equal(mapping.output, language === 'ar' ? '×' : language === 'pt-BR' ? 'X' : 'Cross');
      assert.equal(mapping.type, 'pedal', 'calibration machine values are not translated');
      // Closed and open drawers must each use the proper RTL side and transform.
      const drawer = page.locator('#optionsMenu');
      await drawer.evaluate(element => { element.style.transition = 'none'; });
      const closed = await drawer.boundingBox();
      if (language === 'ar') assert(closed.x + closed.width <= 1, 'RTL drawer is hidden beyond the left edge');
      else assert(closed.x >= width - 1, 'LTR drawer is hidden beyond the right edge');
      await page.locator('#padMenuBtn').evaluate(button => button.click());
      const open = await drawer.boundingBox();
      assert(open.x >= -1 && open.x + open.width <= width + 1, 'open drawer fits the viewport');
      assert(Math.abs(open.x - (language === 'ar' ? 0 : width - open.width)) < 1, 'drawer follows the interface direction');
      await page.locator('#closeOptions').evaluate(button => button.click());
      assert.equal(await page.locator('#mapPad .pad-area').evaluate(e => getComputedStyle(e).direction), 'ltr');
      assert.deepEqual(errors, [], `${width}: page errors`);
      if (width === 1440) {
        for (const choice of ['ar', 'pt-BR', 'en', 'auto']) {
          await page.locator('#langSelect').evaluate((select, value) => { select.value = value; select.dispatchEvent(new Event('change')); }, choice);
          const expected = choice === 'auto' ? language : choice;
          await page.waitForFunction(expected => document.documentElement.lang === expected, expected);
          assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem('c4f.lang'))), choice);
        }
      }
      await context.close();
      console.log(`PASS ${language} ${width}px: raw names, options, tooltips, messages, explicit boundaries and language picker`);
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
