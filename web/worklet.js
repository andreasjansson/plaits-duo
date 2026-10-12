// Copyright 2026 Andreas Jansson. MIT license (see LICENSE).
//
// The audio thread of the web demo: runs the firmware (plaits.wasm, or its
// JavaScript translation plaits_fallback.js when the page cannot compile
// WebAssembly) one block of 12 samples at a time, as the module does, with a
// sequencer playing into its jacks. The page sends the knobs, buttons and
// the sequencer settings, and receives the LEDs and the state of the jacks.

const SAMPLE_RATE = 47872;  // The sample rate of the module.
const BLOCK = 12;
const BLOCKS_PER_RENDER = 8;
const REPORT_INTERVAL = 1 / 30;

// Jacks, in the order of the firmware (CvAdcChannel).
const MODEL = 0, V_OCT = 1, FM = 2, HARMONICS = 3, TIMBRE = 4, MORPH = 5,
    TRIGGER = 6, LEVEL = 7;

// The sequencer plays a loop (see web/loops.js):
//   bpm, div: steps per beat (0.5: a step every two beats).
//   swing: the off-beat steps start late, by this fraction of a step.
//   gate: a trigger on TRIGGER on every note (otherwise TRIGGER is
//     unpatched, and the low-pass gate stays open).
//   glide: the time constant of the pitch, in seconds.
//   a, b: the notes of the two voices, for V/OCT and LEVEL, in semitones
//     above the pitch set by FREQUENCY (null: a rest, the pitch is held).
//     They can have different lengths.
//   mods: CVs, added together, for each target: pitch (both voices), pitch_a
//     (the first voice) and pitch_b (the second voice), in semitones; timbre,
//     morph and harmonics, in volts:
//     { lfo: 'sine' | 'tri' | 'saw' | 'ramp' | 'square', beats (or seconds),
//       depth, offset, phase }
//     { steps: [one value per step], slew, perNote }: with perNote, the
//       value of the step where the last note started, held until the next.
//     { random: depth, every (steps), seed, slew }: the same on every cycle.
//     { env: peak, decay (seconds), accents }: on every note, scaled by the
//       accent of the step where it started (accents: one per step).
//     { vibrato: depth, hz, delay, fade (seconds), voice: 'a' | 'b' }: from
//       each note of that voice, after `delay`, growing over `fade`.

const DORIAN = [0, 2, 3, 5, 7, 9, 10];
const deg = d => { const o = Math.floor(d / 7); return DORIAN[d - 7 * o] + 12 * o; };
const CLASSIC_MODS = {
  timbre: [{ lfo: 'tri', seconds: 9.3, depth: 2.5 }],
  morph: [{ lfo: 'sine', seconds: 13.7, depth: 2.5 }],
};
const BASS = [-7, -7, -5, -4, -7, -3, -4, -5, -7, -7, -5, -5, -4, -4, -6, -6];
// The loops the page could choose by name before it sent loops as data.
const BUILT_IN = {
  melody: {
    bpm: 104, div: 2, gate: true, mods: CLASSIC_MODS,
    a: [0, 2, 4, 3, 2, 4, 6, 7, 6, 4, 3, 2, 1, 2, 3, 1].map(deg),
    b: [4, 4, 6, 7, 6, 6, 8, 9, 8, 6, 5, 4, 3, 4, 5, 4].map(deg),
  },
  arpeggio: {
    bpm: 104, div: 4, gate: true, mods: CLASSIC_MODS,
    a: [].concat(...[0, -1, 3, 4].map(r => [0, 2, 4, 7, 9, 7, 4, 2].map(k => deg(r + k)))),
    b: [].concat(...[0, -1, 3, 4].map(r => Array(8).fill(deg(r - 7)))),
  },
  bass: { bpm: 104, div: 2, gate: true, mods: CLASSIC_MODS, a: BASS.map(deg), b: BASS.map(x => deg(x + 4)) },
  drone: { bpm: 20, div: 1, gate: false, mods: CLASSIC_MODS, a: [0, 0, 0, 0], b: [4, 5, 7, 2].map(deg) },
};

const TRIGGER_LENGTH = 0.01;
const MOD_TARGETS = ['pitch', 'pitch_a', 'pitch_b', 'timbre', 'morph', 'harmonics'];

// A value from -1 to 1, the same for the same seed and index.
function hash(seed, i) {
  let x = Math.imul(seed | 0, 374761393) + Math.imul(i | 0, 668265263);
  x = Math.imul(x ^ (x >>> 13), 1274126177);
  x ^= x >>> 16;
  return (x >>> 0) / 4294967295 * 2 - 1;
}

function shape(name, phase) {
  switch (name) {
    case 'sine': return Math.sin(2 * Math.PI * phase);
    case 'tri': return 4 * Math.abs(phase - Math.floor(phase + 0.5)) - 1;
    case 'saw': return 2 * phase - 1;
    case 'ramp': return 1 - 2 * phase;
    case 'square': return phase < 0.5 ? 1 : -1;
    default: return 0;
  }
}

class Loop {
  constructor(l) {
    this.gate = !!l.gate;
    this.beat = 60 / l.bpm;
    this.step = this.beat / (l.div || 2);
    this.swing = l.swing || 0;
    this.a = l.a && l.a.length ? l.a : [0];
    this.b = l.b && l.b.length ? l.b : this.a;
    this.glide = l.glide || 0;
    this.mods = {};
    for (const target of MOD_TARGETS) {
      this.mods[target] = ((l.mods || {})[target] || []).map(m => Object.assign({}, m, { state: null }));
    }
    this.k = -1;
    this.targetA = 0;
    this.targetB = 0;
    this.pitchA = null;
    this.pitchB = null;
    this.noteTime = -1e9;
    this.noteStep = 0;
    this.noteTimeB = -1e9;
  }

  // Steps go in pairs: the first one is longer by the swing.
  stepAt(t) {
    const pair = 2 * this.step, p = Math.floor(t / pair);
    return t - p * pair < this.step * (1 + this.swing) ? 2 * p : 2 * p + 1;
  }

  stepStart(k) {
    return Math.floor(k / 2) * 2 * this.step + (k % 2 ? this.step * (1 + this.swing) : 0);
  }

  // Advances to time t (the block lasting dt): the pitches, and whether
  // TRIGGER is high.
  advance(t, dt) {
    const k = this.stepAt(t);
    if (k !== this.k) {
      this.k = k;
      const a = this.a[k % this.a.length], b = this.b[k % this.b.length];
      if (a !== null && a !== undefined) {
        this.targetA = a;
        this.noteTime = this.stepStart(k);
        this.noteStep = k;
      }
      if (b !== null && b !== undefined) {
        this.targetB = b;
        this.noteTimeB = this.stepStart(k);
      }
    }
    const c = this.glide > 0 ? 1 - Math.exp(-dt / this.glide) : 1;
    this.pitchA = this.pitchA === null ? this.targetA : this.pitchA + (this.targetA - this.pitchA) * c;
    this.pitchB = this.pitchB === null ? this.targetB : this.pitchB + (this.targetB - this.pitchB) * c;
    return this.gate && t - this.noteTime < TRIGGER_LENGTH;
  }

  mod(target, t, dt) {
    let sum = 0;
    for (const m of this.mods[target]) {
      let v = 0;
      if (m.lfo) {
        let phase = t / (m.seconds || m.beats * this.beat) + (m.phase || 0);
        phase -= Math.floor(phase);
        v = (m.offset || 0) + (m.depth || 0) * shape(m.lfo, phase);
      } else if (m.steps) {
        v = m.steps[(m.perNote ? this.noteStep : this.k) % m.steps.length];
      } else if (m.random !== undefined) {
        v = m.random * hash(m.seed || 1, Math.floor((this.k % this.a.length) / (m.every || 1)));
      } else if (m.env !== undefined) {
        const age = t - this.noteTime;
        const accent = m.accents ? m.accents[this.noteStep % m.accents.length] : 1;
        v = age >= 0 ? m.env * accent * Math.exp(-age / (m.decay || 0.2)) : 0;
      } else if (m.vibrato !== undefined) {
        const age = t - (m.voice === 'b' ? this.noteTimeB : this.noteTime);
        const grow = Math.max(0, Math.min(1, (age - (m.delay || 0)) / (m.fade || 1e-3)));
        v = m.vibrato * grow * Math.sin(2 * Math.PI * (m.hz || 5) * age);
      }
      if (m.slew) {
        m.state = m.state === null ? v : m.state + (v - m.state) * (1 - Math.exp(-dt / m.slew));
        v = m.state;
      }
      sum += v;
    }
    return sum;
  }

  modulated(target) { return this.mods[target].length > 0; }
}

const clampVolts = v => Math.max(-5, Math.min(5, v));

class PlaitsProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null;
    this.playing = false;
    this.loop = new Loop(BUILT_IN.melody);
    this.secondVoice = true;
    this.time = 0;          // Sequencer time, in seconds.
    this.step = -1;
    this.gain = 0;          // Output gain, ramped towards the target.
    this.cv = new Float32Array(8);
    this.patched = new Uint8Array(8);
    this.reportTimer = 0;
    // Rendered samples waiting to be played, and the read position (with a
    // fraction when the output runs at another sample rate).
    this.out = new Float32Array(BLOCK * BLOCKS_PER_RENDER + 2);
    this.aux = new Float32Array(BLOCK * BLOCKS_PER_RENDER + 2);
    this.count = 0;
    this.position = 0;
    this.ratio = SAMPLE_RATE / sampleRate;
    this.port.onmessage = e => this.receive(e.data);
  }

  async receive(m) {
    if (m.type === 'init') {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        this.start(instance.exports, 'WebAssembly', m.pots);
      } catch (error) {
        this.port.postMessage({ type: 'wasm-error', message: String(error) });
      }
    } else if (m.type === 'init-fallback') {
      this.start(globalThis.PlaitsFallback, 'JavaScript', m.pots);
    } else if (m.type === 'restore') {
      // The memory of the firmware which ran on the page until audio could
      // run: this one carries on from there.
      if (this.x) this.restore(m.memory);
      else this.pendingRestore = m.memory;
    } else if (m.type === 'snapshot') {
      // A copy of the memory of the firmware, for the page to carry on from
      // if the browser stops this audio context for good.
      if (this.x) {
        const memory = new Uint8Array(this.x.memory.buffer).slice().buffer;
        this.port.postMessage({ type: 'snapshot', memory }, [memory]);
      }
    } else if (m.type === 'transport' || m.type === 'loop') {
      this.sequencer(m);
    } else if (!this.x) {
      return;
    } else if (m.type === 'pot') {
      this.x.plaits_set_pot(m.index, m.value);
    } else if (m.type === 'button') {
      this.x.plaits_set_button(m.index, m.down ? 1 : 0);
    } else if (m.type === 'select') {
      this.x.plaits_select_model(m.model);
    }
  }

  // The settings of the sequencer (they can come before the firmware runs).
  sequencer(m) {
    if (m.type === 'transport') {
      if (m.playing && !this.playing) {
        this.time = 0;
        this.loop.k = -1;
        this.loop.noteTime = -1e9;
        this.loop.noteTimeB = -1e9;
      }
      this.playing = m.playing;
      if (BUILT_IN[m.pattern] && m.pattern !== this.loopName) {
        this.loopName = m.pattern;
        this.loop = new Loop(BUILT_IN[m.pattern]);
        this.time = 0;
      }
      this.secondVoice = m.secondVoice;
    } else if (m.type === 'loop') {
      // A loop sent as data (see web/loops.js), from its start.
      this.loopName = null;
      this.loop = new Loop(m.loop);
      this.time = 0;
    }
  }

  restore(memory) {
    const target = new Uint8Array(this.x.memory.buffer);
    const source = new Uint8Array(memory);
    target.set(source.subarray(0, Math.min(source.length, target.length)));
    this.count = 0;
    this.position = 0;
  }

  // Powers the module on, with the knobs where the page has them.
  start(exports, engine, pots) {
    const x = exports;
    x._initialize();
    x.plaits_reset_hardware();
    pots.forEach((value, index) => x.plaits_set_pot(index, value));
    x.plaits_boot();
    this.x = x;
    if (this.pendingRestore) {
      this.restore(this.pendingRestore);
      this.pendingRestore = null;
    }
    this.port.postMessage({ type: 'ready', engine });
  }

  // The CVs of the sequencer for the next block.
  sequence() {
    const x = this.x;
    const loop = this.loop;
    const duo = x.plaits_duo_mode() === 1;
    const patch = (jack, on) => {
      if (this.patched[jack] !== (on ? 1 : 0)) {
        this.patched[jack] = on ? 1 : 0;
        x.plaits_set_patched(jack, on ? 1 : 0);
      }
    };
    const set = (jack, volts) => {
      this.cv[jack] = volts;
      x.plaits_set_cv(jack, volts);
    };
    patch(V_OCT, this.playing);
    patch(TRIGGER, this.playing && loop.gate);
    patch(TIMBRE, this.playing);
    patch(MORPH, this.playing);
    patch(HARMONICS, this.playing && loop.modulated('harmonics'));
    // In the original firmware, LEVEL is the level of the low-pass gate: the
    // second melody is only patched in duo mode.
    patch(LEVEL, this.playing && duo && this.secondVoice);
    if (!this.playing) {
      return;
    }
    const t = this.time, dt = BLOCK / SAMPLE_RATE;
    const trigger = loop.advance(t, dt);
    const pitch = loop.mod('pitch', t, dt);
    set(V_OCT, (loop.pitchA + pitch + loop.mod('pitch_a', t, dt)) / 12);
    set(LEVEL, (loop.pitchB + pitch + loop.mod('pitch_b', t, dt)) / 12);
    set(TRIGGER, trigger ? 5 : 0);
    set(TIMBRE, clampVolts(loop.mod('timbre', t, dt)));
    set(MORPH, clampVolts(loop.mod('morph', t, dt)));
    if (loop.modulated('harmonics')) set(HARMONICS, clampVolts(loop.mod('harmonics', t, dt)));
    this.step = loop.k;
    this.time += dt;
  }

  render() {
    const x = this.x;
    // Keep the samples which have not been read yet.
    const keep = this.count - Math.floor(this.position);
    this.out.copyWithin(0, this.count - keep, this.count);
    this.aux.copyWithin(0, this.count - keep, this.count);
    this.position -= this.count - keep;
    this.count = keep;
    while (this.count + BLOCK <= this.out.length) {
      this.sequence();
      x.plaits_render(1);
      const o = new Float32Array(x.memory.buffer, x.plaits_out(), BLOCK);
      const a = new Float32Array(x.memory.buffer, x.plaits_aux(), BLOCK);
      this.out.set(o, this.count);
      this.aux.set(a, this.count);
      this.count += BLOCK;
    }
  }

  process(inputs, outputs) {
    const left = outputs[0][0], right = outputs[0][1] || outputs[0][0];
    if (!this.x) {
      left.fill(0);
      right.fill(0);
      return true;
    }
    const duo = this.x.plaits_duo_mode() === 1;
    const target = this.playing ? 0.8 : 0;
    for (let n = 0; n < left.length; ++n) {
      while (this.position + 1 >= this.count) this.render();
      const i = Math.floor(this.position), f = this.position - i;
      const o = this.out[i] + (this.out[i + 1] - this.out[i]) * f;
      const a = this.aux[i] + (this.aux[i + 1] - this.aux[i]) * f;
      this.position += this.ratio;
      this.gain += (target - this.gain) * 0.002;
      // The original firmware plays OUT in both ears (AUX carries a variant
      // of the sound); duo mode plays OUT on the left and AUX on the right.
      left[n] = o * this.gain;
      right[n] = (duo ? a : o) * this.gain;
    }
    this.reportTimer += left.length / sampleRate;
    if (this.reportTimer >= REPORT_INTERVAL) {
      this.reportTimer = 0;
      this.report();
    }
    return true;
  }

  report() {
    const x = this.x;
    const leds = [];
    for (let i = 0; i < 8; ++i) leds.push(x.plaits_led(i));
    this.port.postMessage({
      type: 'state',
      leds,
      duo: x.plaits_duo_mode() === 1,
      model: x.plaits_model(),
      timbre: x.plaits_timbre(),
      morph: x.plaits_morph(),
      cv: Array.from(this.cv),
      patched: Array.from(this.patched),
      step: this.step,
    });
  }
}

registerProcessor('plaits', PlaitsProcessor);
