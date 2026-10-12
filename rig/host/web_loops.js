// The CVs of the web demo's loops, as its sequencer plays them (web/loops.js,
// and the Loop class of web/worklet.js, evaluated here): for each loop of the
// page, block by block, the pitches of the two voices (semitones above the
// pitch set by FREQUENCY), TRIGGER, and the TIMBRE, MORPH and HARMONICS CVs
// (volts). rig/host/loops.py plays them on the module with the oC.
//
//   node web_loops.js SECONDS > loops.json

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const web = path.join(__dirname, '..', '..', 'web');
const SAMPLE_RATE = 47872, BLOCK = 12;

globalThis.AudioWorkletProcessor = class {};
globalThis.registerProcessor = () => {};
vm.runInThisContext(fs.readFileSync(path.join(web, 'loops.js'), 'utf8'));
vm.runInThisContext(fs.readFileSync(path.join(web, 'worklet.js'), 'utf8') +
                    '\nglobalThis.WebLoop = Loop; globalThis.clampWebVolts = clampVolts;\n');

// The loops of the page, in its order (PAGE_LOOPS in web/index.html).
const page = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
const ids = JSON.parse(page.match(/const PAGE_LOOPS = (\[[^\]]*\])/)[1].replace(/'/g, '"'));
const all = [...PLAITS_LOOPS.current, ...PLAITS_LOOPS.candidates];

const seconds = parseFloat(process.argv[2] || '12');
const dt = BLOCK / SAMPLE_RATE;
const out = ids.map((id, i) => {
  const loop = all.find(l => l.id === id);
  // As the page plays it in duo mode: the countermelody on the second voice.
  const resolved = PLAITS_LOOPS.resolve(loop, 'counter');
  const l = new WebLoop(resolved);
  const n = Math.round(seconds / dt);
  const r = { a: [], b: [], trigger: [], timbre: [], morph: [], harmonics: [] };
  for (let k = 0, t = 0; k < n; ++k, t += dt) {
    // The same calls, in the same order, as PlaitsProcessor.sequence().
    const trigger = l.advance(t, dt);
    const pitch = l.mod('pitch', t, dt);
    r.a.push(l.pitchA + pitch + l.mod('pitch_a', t, dt));
    r.b.push(l.pitchB + pitch + l.mod('pitch_b', t, dt));
    r.trigger.push(trigger ? 1 : 0);
    r.timbre.push(clampWebVolts(l.mod('timbre', t, dt)));
    r.morph.push(clampWebVolts(l.mod('morph', t, dt)));
    r.harmonics.push(l.modulated('harmonics') ? clampWebVolts(l.mod('harmonics', t, dt)) : 0);
  }
  const cycle = Math.max(l.a.length, l.b.length) * l.step;
  return { number: i + 1, id, name: loop.name, desc: loop.desc, model: loop.model,
           gate: l.gate, cycle, dt, harmonics_modulated: l.modulated('harmonics'), cv: r };
});
process.stdout.write(JSON.stringify(out));
