// Exercise the shipped offline message catalogue and language preference logic.
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const page = fs.readFileSync('client/index.html', 'utf8');
const from = page.indexOf('  // Interface language.');
const end = page.indexOf('\n  }());', from) + '\n  }());'.length;
assert(from > 0 && end > from);
const source = page.slice(from,end);
assert(!source.includes('MutationObserver'), 'interface localization must not inspect arbitrary runtime data');
function load({languages = ['en-US'], saved = null} = {}) {
  const ctx = { navigator: {languages, language: languages[0]},
    localStorage: {getItem: () => saved, setItem() {}},
    document: { documentElement: {}, querySelectorAll: () => [], getElementById: () => null } };
  ctx.window = ctx;
  vm.createContext(ctx); vm.runInContext(source,ctx);
  return ctx;
}
for (const [languages,saved,expected] of [
  [['en-US'],null,'en'], [['pt-PT'],null,'pt-BR'], [['ar-SA'],null,'ar'],
  [['en-GB','ar'],null,'en'], [['ar'],'"en"','en'], [['en'],'"pt-BR"','pt-BR'],
  [['en'],'"ar"','ar'], [['ar'],'"auto"','ar'], [['ar'],'"unsupported"','ar'],
  [['ar'],'invalid json','ar']
]) assert.equal(load({languages,saved}).c4fLang,expected);
const en=load(), pt=load({languages:['pt-BR']}), ar=load({languages:['ar']});
assert.equal(en.document.documentElement.dir,'ltr');
assert.equal(ar.document.documentElement.dir,'rtl');
assert.equal(pt.c4fI18n.t('Settings'),'Configurações');
assert.equal(ar.c4fI18n.t('Settings'),'الإعدادات');
assert.equal(ar.c4fI18n.t('Controller {number}',{number:2}),'يد التحكم \u20682\u2069');
for (const ctx of [en,pt,ar]) {
  const {t,messages}=ctx.c4fI18n;
  for (const name of ['Left','Esquerda','Left. Right.','Settings','أحمد DS4','<b>Settings</b>']) {
    assert(t('Saved as {name}.',{name}).includes(name), 'inserted names stay unchanged');
    assert.equal(t('Unknown service error 173'),'Unknown service error 173');
  }
  for (const key of Object.keys(messages['pt-BR'])) {
    assert(messages.ar[key], 'missing Arabic: '+key);
    for (const locale of ['pt-BR','ar']) assert.deepEqual(
      (messages[locale][key].match(/\{\w+\}/g)||[]).sort(),
      (key.match(/\{\w+\}/g)||[]).sort(), 'template values: '+key);
  }
}
for (const match of page.slice(0,from).matchAll(/data-i18n(?:-(?:aria-label|title|placeholder))?="([^"]+)"/g))
  assert(ar.c4fI18n.messages.ar[match[1].replace(/&quot;/g,'"').replace(/&amp;/g,'&')], 'markup key: '+match[1]);
assert(!/[\u0660-\u0669\u06f0-\u06f9]/.test(JSON.stringify(ar.c4fI18n.messages.ar)), 'Arabic uses 0–9');
assert.equal((page.match(/data:font\/ttf;base64,/g)||[]).length,2, 'both Arabic weights work offline');
console.log('PASS offline English/Portuguese/Arabic catalogues, template boundaries, preferences, RTL and embedded fonts');

// A partial SST failure must retry only the missing weight, retaining the loaded one.
(async () => {
  const ctx = load({ languages: ['ar'] }), faces = new Set(), created = [], attempts = {};
  ctx.URL = URL;
  ctx.document.fonts = { add: face => faces.add(face), delete: face => faces.delete(face) };
  ctx.FontFace = class {
    constructor(family, source, options) {
      Object.assign(this, options, { family, source, status: 'unloaded' }); created.push(this);
    }
    load() {
      attempts[this.weight] = (attempts[this.weight] || 0) + 1;
      if (this.weight === '400' && attempts[this.weight] === 1) return Promise.reject(Error('temporary font failure'));
      this.status = 'loaded'; return Promise.resolve(this);
    }
  };
  await ctx.c4fI18n.loadConsoleFonts('http://c4f.test/');
  assert.equal(faces.size, 1);
  const retained = [...faces][0];
  await ctx.c4fI18n.loadConsoleFonts('http://c4f.test/');
  assert.equal(faces.size, 2);
  assert(faces.has(retained), 'the successfully loaded weight remains during retry');
  assert.deepEqual(attempts, { 300: 1, 400: 2 });
  await ctx.c4fI18n.loadConsoleFonts('http://c4f.test/');
  assert.equal(created.length, 3, 'complete font sets are not requested again');
  await ctx.c4fI18n.loadConsoleFonts('http://second-c4f.test/');
  assert.equal(faces.size, 2, 'changing consoles replaces both old faces without growth');
  assert(!faces.has(retained));
  assert([...faces].every(face => face.source.includes('http://second-c4f.test/fonts/')));
  console.log('PASS missing SST weight retries, loaded weight retention and bounded font replacement');
})().catch(error => { console.error(error); process.exitCode = 1; });
