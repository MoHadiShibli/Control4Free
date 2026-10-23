// Runs the page's own language layer under Node: which language it picks,
// what it shows in Brazilian Portuguese, and that the markup's texts all have
// a translation. No browser and no console.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const page = fs.readFileSync('client/index.html', 'utf8');
const from = page.indexOf('  // Interface language.');
assert.notEqual(from, -1, 'page no longer contains its language layer');
const to = page.indexOf('\n  }());', from);
assert.notEqual(to, -1);
const start = '    fillPickers();\n    walk(document.documentElement);';
const source = page.slice(from, to + 8);
assert(source.includes(start), 'language layer no longer starts with fillPickers and walk');

function load({ languages = ['en-US'], saved = null } = {}) {
  const observers = [];
  const ctx = {
    navigator: { languages, language: languages[0] },
    localStorage: { getItem: key => (key === 'c4f.lang' ? saved : null), setItem() {} },
    document: { documentElement: { lang: 'en' }, getElementById: () => null },
    MutationObserver: class { constructor() { observers.push(this); } observe() {} },
    location: { reload() {} },
    confirm: message => message,
  };
  ctx.window = ctx;
  vm.createContext(ctx);
  vm.runInContext(source.replace(start, '    window.translate = translate;\n' + start.replace('walk(', 'if (0) walk(')), ctx);
  return { ctx, observers };
}

// Which language: the saved choice, otherwise the browser's first one.
for (const [options, expected] of [
  [{ languages: ['en-US'] }, 'en'], [{ languages: ['pt-BR', 'en'] }, 'pt-BR'], [{ languages: ['pt-PT'] }, 'pt-BR'],
  [{ languages: ['en-GB', 'pt-BR'] }, 'en'], [{ languages: ['pt-BR'], saved: '"en"' }, 'en'],
  [{ languages: ['en-US'], saved: '"pt-BR"' }, 'pt-BR'], [{ languages: ['pt-BR'], saved: '"auto"' }, 'pt-BR'],
  [{ languages: ['pt-BR'], saved: 'not json' }, 'pt-BR'],
]) assert.equal(load(options).ctx.c4fLang, expected, JSON.stringify(options));
// English leaves the page alone: nothing watches or changes it.
const english = load();
assert.equal(english.observers.length, 0);
assert.equal(english.ctx.document.documentElement.lang, 'en');
assert.equal(english.ctx.translate, undefined);
console.log('PASS the saved choice, then the browser language, picks the page language; English changes nothing');

const { ctx, observers } = load({ languages: ['pt-BR'] });
const T = ctx.translate;
assert.equal(observers.length, 1);
assert.equal(ctx.document.documentElement.lang, 'pt-BR');
for (const [en, pt] of [
  ['Connecting', 'Conectando'],
  [' to 192.168.1.20:4264', ' a 192.168.1.20:4264'],
  ['Controller 2', 'Controle 2'],
  ['Controller 1 · Alex', 'Controle 1 · Alex'],
  ['+3 devices', '+3 dispositivos'],
  ['Gamepad 2 connected. Pick its controller on the home screen.', 'Gamepad 2 conectado. Escolha o controle dele na tela inicial.'],
  ['16 buttons, 4 axes · the browser knows its layout', '16 botões, 4 eixos · o navegador conhece o layout dele'],
  ['Cross is Enter, taken from Triangle, Square.', 'X: Enter (tirada de Triângulo, Quadrado).'],
  ['Left and right: Axis 0, reversed', 'Esquerda e direita: Eixo 0, invertido'],
  ['Cross now uses Button 6, pressure, taken from Square.', 'X agora usa Botão 6, pressão, tirado de Quadrado.'],
  ["That input can't be used for Cross (Invalid input mode).", 'Essa entrada não pode ser usada para X (Modo de entrada inválido).'],
  ['Saved as My. profile.', 'Salvo como My. profile.'],
  ['Saved as Pad. Storage unavailable. Profile changes are session-only.',
    'Salvo como Pad. Armazenamento indisponível. As alterações de perfil valem só nesta sessão.'],
  ['Profile deleted. ', 'Perfil excluído. '],
  ['Controller is in use on another device', 'O controle está em uso em outro dispositivo'],
]) assert.equal(T(en), pt, en);
// Names, numbers and texts it doesn't know are left as they are.
for (const text of ['Xbox Wireless Controller', 'Control4Free 1.3.0 · 12 ms', '0.25', '—', '', '  ', 'Lucas'])
  assert.equal(T(text), text);
assert.equal(T(T('Options')), 'Opções', 'a translation is not translated again');
assert.equal(ctx.confirm('Disconnect your controllers and stop Control4Free on the PS4?'),
  'Desconectar seus controles e parar o Control4Free no PS4?');
console.log('PASS Brazilian Portuguese: fixed texts, texts with values, several sentences and untranslated names');

// Every text in the markup has a translation, so new ones are noticed.
const markup = page.slice(page.indexOf('<body>'), page.indexOf('<script>'));
const kept = new Set(['Control4Free', 'Free Palestine', 'SHARE', 'OPTIONS', 'Share', 'Touchpad', 'Gamepads',
  'Experimental', 'L1', 'L2', 'L3', 'R1', 'R2', 'R3']);
const texts = new Set();
for (const m of markup.matchAll(/>([^<>]+)</g)) texts.add(m[1].trim());
for (const m of markup.matchAll(/\b(?:aria-label|title|placeholder)="([^"]+)"/g)) texts.add(m[1].trim());
const missing = [...texts].filter(t => /[A-Za-z]{2}/.test(t) && !kept.has(t) && !/^[\d.:]+$/.test(t) && T(t) === t);
assert.deepEqual(missing, [], 'markup texts without a translation');
console.log(`PASS all ${texts.size} markup texts are translated or deliberately kept`);
