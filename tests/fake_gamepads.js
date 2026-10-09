/* Local browser test double. This file is never loaded by client/index.html.
 * Inject this script and call fakePads.loadProfiles(researched JSON profiles).
 * Example: fakePads.press(fakePads.connect(profileName), 'Green fret').
 * API contract: https://w3c.github.io/gamepad/#dom-navigator-getgamepads
 * Slots retain null holes; button pressure/touch and timestamps update on polls.
 * These snapshots emulate the documented profiles, not hardware compatibility.
 */
(() => {
  'use strict';
  // Chromium Gamepads::kItemsLengthCap and Windows XInput both cap at four.
  // https://chromium.googlesource.com/chromium/src/+/main/device/gamepad/public/cpp/gamepads.h
  const profiles = new Map(), devices = Array(4).fill(null), feedback = [];
  const copy = value => JSON.parse(JSON.stringify(value));
  const finite = value => {
    if (!Number.isFinite(value)) throw new TypeError('Input must be a finite number');
    return value;
  };
  const clamp = (value, min, max) => Math.max(min, Math.min(max, finite(value)));
  const valueOf = entry => entry.value;
  const evidence = (entry, label) => {
    if (!entry || !['confirmed', 'inferred'].includes(entry.status) || !entry.sources?.length ||
        entry.sources.some(source => !/^https?:\/\//.test(source.url) || !source.says) ||
        entry.status === 'inferred' && !entry.reason) throw new Error('Missing evidence: ' + label);
  };
  function validate(profile) {
    if (!profile?.name || !profile.label || !profile.environment || !profile.controls) throw new Error('Invalid profile');
    for (const key of ['id', 'mapping', 'buttons', 'axesRest', 'vibration']) evidence(profile.gamepad?.[key], profile.name + '.' + key);
    const pad = profile.gamepad, count = valueOf(pad.buttons), axes = valueOf(pad.axesRest);
    if (typeof valueOf(pad.id) !== 'string' || !['', 'standard'].includes(valueOf(pad.mapping)) ||
        !Number.isInteger(count) || count < 1 || !Array.isArray(axes) || axes.some(v => !Number.isFinite(v))) throw new Error('Invalid layout: ' + profile.name);
    for (const [name, control] of Object.entries(profile.controls)) {
      evidence(control.evidence, profile.name + '.' + name);
      if (control.kind === 'button' && (!Number.isInteger(control.index) || control.index < 0 || control.index >= count)) throw new Error('Button out of range: ' + name);
      if (['axis', 'hat'].includes(control.kind) && (!Number.isInteger(control.index) || control.index < 0 || control.index >= axes.length)) throw new Error('Axis out of range: ' + name);
      if (control.kind === 'stick' && [control.x, control.y].some(index => !Number.isInteger(index) || index < 0 || index >= axes.length)) throw new Error('Stick out of range: ' + name);
      if (control.kind === 'alias' && !profile.controls[control.target]) throw new Error('Unknown alias target: ' + name);
      if (!['button', 'axis', 'stick', 'hat', 'alias', 'unreported'].includes(control.kind)) throw new Error('Unknown control kind: ' + name);
      if (control.kind === 'hat' && (!Number.isFinite(control.neutral) || !control.directions)) throw new Error('Invalid hat: ' + name);
    }
    for (const [name, spec] of Object.entries(profile.switches || {})) evidence(spec.evidence, profile.name + '.' + name);
    return copy(profile);
  }
  function device(index) {
    const found = devices[index];
    if (!found) throw new Error('No connected fake gamepad at index ' + index);
    return found;
  }
  function controlFor(pad, name, seen = new Set()) {
    const control = pad.profile.controls[name];
    if (!control) throw new Error('Unknown control: ' + name);
    if (seen.has(name)) throw new Error('Circular control alias: ' + name);
    seen.add(name);
    if (control.kind === 'alias') return controlFor(pad, control.target, seen);
    if (control.kind === 'unreported') throw new Error(name + ' is not independently exposed: ' + (control.reason || pad.profile.notes?.join(' ') || 'see the profile evidence'));
    return control;
  }
  function snapshot(pad, connected = true) {
    const spec = pad.profile.gamepad;
    pad.sample = { connected, timestamp: pad.timestamp, buttons: [...pad.buttons], axes: Object.freeze([...pad.axes]) };
    if (!pad.public) {
      const buttons = Object.freeze(pad.buttons.map((_, index) => {
        const control = Object.values(pad.profile.controls).find(c => c.kind === 'button' && c.index === index);
        const threshold = control?.pressedThreshold ?? .5;
        return Object.freeze({
          get pressed() { const value = pad.sample.buttons[index]; return control?.pressedInclusive ? value >= threshold : value > threshold; },
          get touched() {
            const value = pad.sample.buttons[index];
            if (control?.touchedPolicy === 'pressed') return control.pressedInclusive ? value >= threshold : value > threshold;
            return value > 0;
          },
          get value() { return pad.sample.buttons[index]; }, [Symbol.toStringTag]: 'GamepadButton'
        });
      }));
      const result = {
        id: valueOf(spec.id), index: pad.index, mapping: valueOf(spec.mapping),
        get connected() { return pad.sample.connected; }, get timestamp() { return pad.sample.timestamp; },
        buttons, get axes() { return pad.sample.axes; }, [Symbol.toStringTag]: 'Gamepad'
      };
      if (valueOf(spec.vibration)) result.vibrationActuator = pad.actuator;
      else if (spec.vibration.presence === 'null') result.vibrationActuator = null;
      pad.public = Object.freeze(result);
    }
    return pad.public;
  }
  function fire(type, pad, connected) {
    // GamepadEvent's constructor rejects JS test doubles as native Gamepads.
    // The page sees the same event name and read-only gamepad property.
    const event = new Event(type);
    Object.defineProperty(event, 'gamepad', { value: snapshot(pad, connected), enumerable: true });
    window.dispatchEvent(event);
  }
  function connectAt(name, index) {
    const profile = profiles.get(name);
    if (!profile) throw new Error('Unknown profile: ' + name);
    const pad = { profile, index, timestamp: performance.now(), dirty: false, contributions: new Map(), buttons: Array(valueOf(profile.gamepad.buttons)).fill(0),
      axes: [...valueOf(profile.gamepad.axesRest)], switches: {} };
    const vibration = valueOf(profile.gamepad.vibration);
    if (vibration) {
      pad.actuator = Object.freeze({ type: vibration.type, effects: Object.freeze([...vibration.effects]),
        playEffect(type, parameters = {}) {
          if (!['dual-rumble', 'trigger-rumble'].includes(type)) throw new TypeError('Unknown haptic effect');
          if (!vibration.effects.includes(type)) return Promise.resolve('not-supported');
          // Blink playEffect parameter ranges; defaults are zero.
          // https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/modules/gamepad/gamepad_haptic_actuator.cc
          if (['duration', 'startDelay'].some(key => parameters[key] !== undefined && (!Number.isFinite(parameters[key]) || parameters[key] < 0)) ||
              ['strongMagnitude', 'weakMagnitude', 'leftTrigger', 'rightTrigger'].some(key => parameters[key] !== undefined &&
                (!Number.isFinite(parameters[key]) || parameters[key] < 0 || parameters[key] > 1))) return Promise.resolve('invalid-parameter');
          feedback.push({ index, type, parameters: copy(parameters) });
          // Capture stub: no motor timing, preemption, or physical validation.
          return Promise.resolve('complete');
        },
        reset() { feedback.push({ index, type: 'reset' }); return Promise.resolve('complete'); },
        [Symbol.toStringTag]: 'GamepadHapticActuator' });
    }
    devices[index] = pad;
    fire('gamepadconnected', pad, true);
    return index;
  }
  function set(index, name, value) {
    const pad = device(index), control = controlFor(pad, name);
    if (control.kind === 'button') {
      const pressure = clamp(value, 0, 1);
      if (!control.analog && ![0, 1].includes(pressure)) throw new TypeError(name + ' is a digital button');
      pad.contributions.set(name, { index: control.index, value: control.steps ? Math.round(pressure * control.steps) / control.steps : pressure });
      const next = Math.max(0, ...[...pad.contributions.values()].filter(c => c.index === control.index).map(c => c.value));
      pad.dirty ||= pad.buttons[control.index] !== next; pad.buttons[control.index] = next;
    }
    else if (control.kind === 'axis') axisValue(pad, control.index, clamp(value, control.min ?? -1, control.max ?? 1));
    else throw new Error('Use tilt or hat for ' + name);
  }
  function axisValue(pad, index, value) { pad.dirty ||= pad.axes[index] !== value; pad.axes[index] = value; }
  function release(index, name) {
    const pad = device(index), control = controlFor(pad, name);
    if (control.kind === 'button') set(index, name, 0);
    else if (control.kind === 'axis') axisValue(pad, control.index, control.rest ?? valueOf(pad.profile.gamepad.axesRest)[control.index]);
    else if (control.kind === 'stick') {
      axisValue(pad, control.x, valueOf(pad.profile.gamepad.axesRest)[control.x]);
      axisValue(pad, control.y, valueOf(pad.profile.gamepad.axesRest)[control.y]);
    } else if (control.kind === 'hat') axisValue(pad, control.index, control.neutral);
  }
  function directionKey(direction) { return direction.toLowerCase().replace(/\s+/g, '-'); }
  function hat(index, direction, name) {
    const pad = device(index);
    name ||= Object.keys(pad.profile.controls).find(key => pad.profile.controls[key].kind === 'hat') || 'D-pad';
    const control = pad.profile.controls[name] ? controlFor(pad, name) : null, key = directionKey(direction);
    if (control?.kind === 'hat') {
      if (key === 'none') axisValue(pad, control.index, control.neutral);
      else if (Object.hasOwn(control.directions, key)) axisValue(pad, control.index, control.directions[key]);
      else throw new Error('Unknown hat direction: ' + direction);
      return;
    }
    // A browser-standard D-pad is four buttons, including diagonals.
    const names = ['up', 'right', 'down', 'left'];
    if (!['none', ...names, 'up-right', 'down-right', 'down-left', 'up-left'].includes(key)) throw new Error('Unknown D-pad direction');
    for (const part of names) {
      const keyName = Object.keys(pad.profile.controls).find(n => n.toLowerCase() === 'd-pad ' + part);
      if (!keyName) throw new Error('No D-pad control in this profile');
      set(index, keyName, key.split('-').includes(part) ? 1 : 0);
    }
  }
  const api = {
    loadProfiles(list) {
      if (!Array.isArray(list)) list = [list];
      const checked = list.map(validate);
      for (const profile of checked) profiles.set(profile.name, profile);
    },
    profiles: () => [...profiles.values()].map(copy),
    describe: index => copy(device(index).profile),
    connect(name) { const index = devices.findIndex(pad => !pad); if (index < 0) throw new Error('All four fake gamepad slots are occupied'); return connectAt(name, index); },
    disconnect(index) { const pad = device(index); devices[index] = null; pad.timestamp = performance.now(); fire('gamepaddisconnected', pad, false); },
    press(index, name) {
      const pad = device(index), control = controlFor(pad, name);
      if (control.kind === 'button') set(index, name, 1);
      else if (control.kind === 'axis') set(index, name, control.full ?? (control.rest > 0 ? -1 : 1));
      else throw new Error('Use tilt or hat for ' + name);
    },
    release, set,
    tilt(index, name, x, y) {
      const pad = device(index), control = controlFor(pad, name);
      if (control.kind !== 'stick') throw new Error(name + ' is not a stick');
      axisValue(pad, control.x, clamp(x, -1, 1)); axisValue(pad, control.y, clamp(y, -1, 1));
    },
    hat,
    switch(index, name, value) {
      const pad = device(index), spec = pad.profile.switches?.[name];
      if (!spec) throw new Error('Unknown switch: ' + name);
      if (spec.kind === 'unreported') throw new Error(name + ': ' + (spec.reason || 'browser effect not found; see evidence'));
      if (spec.kind === 'profile') {
        const target = spec.values[value];
        if (!target || !profiles.has(target)) throw new Error('No researched profile for mode ' + value);
        api.disconnect(index); return connectAt(target, index);
      }
      if (spec.kind !== 'state' || spec.effect !== 'none' || !spec.values.includes(value)) throw new Error('Unsupported switch value');
      pad.switches[name] = value;
      return index;
    },
    switchState: index => copy(device(index).switches),
    feedback: () => copy(feedback),
    reset(index) {
      const pad = device(index);
      pad.dirty ||= pad.buttons.some(v => v !== 0) || pad.axes.some((v, i) => v !== valueOf(pad.profile.gamepad.axesRest)[i]);
      pad.contributions.clear();
      pad.buttons.fill(0); pad.axes = [...valueOf(pad.profile.gamepad.axesRest)];
    }
  };
  // New snapshots expose the latest input only when sampled, like getGamepads.
  Object.defineProperty(navigator, 'getGamepads', { configurable: true, value: () => devices.map(pad => {
    if (!pad) return null;
    if (pad.dirty || valueOf(pad.profile.gamepad.timestampPolicy || { value: 'poll' }) === 'poll') pad.timestamp = Math.max(performance.now(), pad.timestamp + .001);
    pad.dirty = false;
    return snapshot(pad);
  }) });
  Object.defineProperty(window, 'fakePads', { value: Object.freeze(api), configurable: true });
})();
