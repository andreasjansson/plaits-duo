// Copyright 2026 Andreas Jansson. MIT license (see LICENSE).
//
// The loops of the web demo's sequencer, as data. Each loop is one melody,
// for V/OCT: degrees of a scale (null: a rest), transposed by `transpose`
// semitones above the pitch set by FREQUENCY (D3 with the knob where the page
// starts), with CV modulations for the jacks (see web/worklet.js for the
// fields). In duo mode, the second voice (LEVEL) plays a melody made from it:
// one of the second voices below. `model` is the duo model each loop is
// first heard on (0 to 7: classic, waveshaper, FM, formant, harmonic,
// wavetable, string, vowel).
//
// Defines globalThis.PLAITS_LOOPS: { current, candidates, voices, resolve }.
// resolve(loop, voice) is the loop as the sequencer plays it: the notes of
// the two voices in semitones. Some loops have a countermelody (`counter`,
// in degrees like the melody) for their second voice.

(function () {
  const SCALES = {
    major: [0, 2, 4, 5, 7, 9, 11],
    minor: [0, 2, 3, 5, 7, 8, 10],
    dorian: [0, 2, 3, 5, 7, 9, 10],
    phrygian: [0, 1, 3, 5, 7, 8, 10],
    lydian: [0, 2, 4, 6, 7, 9, 11],
    mixolydian: [0, 2, 4, 5, 7, 9, 10],
    harmonicMinor: [0, 2, 3, 5, 7, 8, 11],
    pentaMinor: [0, 3, 5, 7, 10],
    pentaMajor: [0, 2, 4, 7, 9],
    whole: [0, 2, 4, 6, 8, 10],
    blues: [0, 3, 5, 6, 7, 10],
    pelog: [0, 1, 3, 7, 8],
    chromatic: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
  };

  // Degree d of a scale, in semitones (any integer: other octaves).
  const deg = (scale, d) => {
    const s = SCALES[scale], n = s.length, o = Math.floor(d / n);
    return s[d - o * n] + 12 * o;
  };
  const repeat = (list, n) => [].concat(...Array(n).fill(list));
  // Euclidean rhythm: k hits spread over n steps, rotated by r.
  const euclid = (k, n, r = 0) => Array.from({ length: n }, (_, i) => ((i + r) * k) % n < k);
  // Values on the hits of a rhythm, rests elsewhere.
  const place = (hits, values) => {
    let j = 0;
    return hits.map(h => h ? values[j++ % values.length] : null);
  };
  // Chord tones (degrees) over a list of roots (null in the shape: a rest).
  const arp = (roots, shape) => [].concat(...roots.map(r => shape.map(k => k === null ? null : r + k)));

  const lfo = (shape, beats, depth, offset = 0, phase = 0) => ({ lfo: shape, beats, depth, offset, phase });
  const lfoSec = (shape, seconds, depth) => ({ lfo: shape, seconds, depth });
  const steps = (values, slew = 0) => ({ steps: values, slew });
  const random = (volts, every = 1, seed = 1, slew = 0) => ({ random: volts, every, seed, slew });
  const env = (volts, decay, accents) => ({ env: volts, decay, accents });
  const vibrato = (depth, hz, delay, fade, voice) => ({ vibrato: depth, hz, delay, fade, voice });

  // The loops of the page so far, with their own second melodies (step
  // lengths and LFO periods as they were, in seconds, at 104 BPM).
  const classicMods = { timbre: [lfoSec('tri', 9.3, 2.5)], morph: [lfoSec('sine', 13.7, 2.5)] };
  const bassLine = [-7, -7, -5, -4, -7, -3, -4, -5, -7, -7, -5, -5, -4, -4, -6, -6];
  const current = [
    { id: 'melody', name: 'Melody', desc: 'Triggered notes in D dorian.', model: 0,
      bpm: 104, div: 2, gate: true, scale: 'dorian', notes: [0, 2, 4, 3, 2, 4, 6, 7, 6, 4, 3, 2, 1, 2, 3, 1],
      own: [4, 4, 6, 7, 6, 6, 8, 9, 8, 6, 5, 4, 3, 4, 5, 4], mods: classicMods },
    { id: 'arpeggio', name: 'Arpeggio', desc: 'Sixteenths over Dm, C, G and Am.', model: 2,
      bpm: 104, div: 4, gate: true, scale: 'dorian', notes: arp([0, -1, 3, 4], [0, 2, 4, 7, 9, 7, 4, 2]),
      own: [].concat(...[0, -1, 3, 4].map(r => Array(8).fill(r - 7))), mods: classicMods },
    { id: 'bass', name: 'Bass line', desc: 'A low line in eighths.', model: 1,
      bpm: 104, div: 2, gate: true, scale: 'dorian', notes: bassLine, own: bassLine.map(x => x + 4), mods: classicMods },
    { id: 'drone', name: 'Drone', desc: 'The gate open: a held note.', model: 4,
      bpm: 20, div: 1, gate: false, scale: 'dorian', notes: [0, 0, 0, 0], own: [4, 5, 7, 2], mods: classicMods },
  ];

  const tune = [0, 1, 2, 4, 3, 2, 1, 2, 4, 5, 4, 2, 1, 0, 1, -1];

  const candidates = [
    // Melodies.
    { id: 'dorian-tune', name: 'Dorian tune', desc: 'A singing tune in D dorian, TIMBRE and MORPH slowly turning.', model: 0,
      bpm: 100, div: 2, gate: true, scale: 'dorian', notes: tune,
      mods: { timbre: [lfo('tri', 16, 2)], morph: [lfo('sine', 24, 1.5)] } },
    { id: 'swung-major', name: 'Swung major tune', desc: 'A major tune, swung, an envelope on TIMBRE.', model: 3,
      bpm: 96, div: 2, swing: 0.2, gate: true, scale: 'major', notes: [4, 5, 4, 2, 0, 2, 4, 7, 6, 4, 5, 2, 4, 2, 1, 0],
      mods: { timbre: [env(3, 0.25)], morph: [lfo('sine', 32, 2)] } },
    { id: 'rise-fall', name: 'Rise and fall', desc: 'A line climbing a full octave and coming back down.', model: 4,
      bpm: 108, div: 2, gate: true, scale: 'dorian', notes: [0, 1, 2, 3, 4, 5, 4, 3, 2, 3, 4, 5, 6, 7, 6, 4],
      mods: { timbre: [lfo('tri', 8, 2.5)], morph: [lfo('tri', 12, 1.5)] } },
    { id: 'minor-song', name: 'Minor song', desc: 'A minor melody under a rising TIMBRE ramp.', model: 5,
      bpm: 112, div: 2, gate: true, scale: 'minor', notes: [0, 2, 4, 2, 3, 1, 4, 7, 6, 4, 5, 3, 1, 2, 0, -1],
      mods: { timbre: [lfo('saw', 16, 2)], morph: [env(2, 0.4)] } },
    { id: 'five-eight', name: 'Five against eight', desc: 'A five-note figure against an eight-step accent pattern: they keep shifting.', model: 2,
      bpm: 120, div: 4, gate: true, scale: 'pentaMinor', notes: [0, 2, 4, 3, 1],
      // A slow line an octave up, in half notes: F, D, G, F.
      counter: [6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6],
      mods: { timbre: [steps([3, 0, 1, 0, 2, 0, 1, 0])], morph: [lfo('sine', 16, 1)] } },
    { id: 'mixolydian', name: 'Mixolydian tune', desc: 'A bright tune with a flattened seventh.', model: 1,
      bpm: 100, div: 2, gate: true, scale: 'mixolydian', notes: [0, 2, 4, 5, 4, 2, 3, 1, 0, 2, 4, 6, 7, 6, 4, 3],
      mods: { timbre: [lfo('tri', 12, 2)], morph: [lfo('sine', 20, 2)] } },
    { id: 'lament', name: 'Lament', desc: 'A slow phrygian descent, with vibrato.', model: 3,
      bpm: 76, div: 1, gate: true, scale: 'phrygian', notes: [4, 3, 2, 1, 2, 1, 0, null],
      mods: { pitch: [lfo('sine', 0.3, 0.15)], morph: [lfo('tri', 16, 1.5)] } },
    { id: 'hook', name: 'Hook', desc: 'A short pop hook with rests, in D major.', model: 5,
      bpm: 112, div: 2, gate: true, scale: 'major', notes: [0, 2, 4, 4, null, 4, 5, 4, 2, null, 0, 1, 2, null, 1, 0],
      mods: { timbre: [env(2, 0.3)], morph: [lfo('sine', 16, 1.5)] } },
    { id: 'riff', name: 'Low riff', desc: 'A low minor riff with rests, slightly swung.', model: 1,
      bpm: 116, div: 2, swing: 0.1, gate: true, scale: 'minor', transpose: -12,
      notes: [0, 0, null, 0, 2, null, 3, 2, 0, 0, null, 0, -2, null, -1, -2],
      mods: { timbre: [env(3, 0.2)], morph: [lfo('tri', 8, 1.5)] } },
    { id: 'figure', name: 'Twelve-note figure', desc: 'A twelve-note figure in fast sixteenths, after Steve Reich.', model: 6,
      bpm: 132, div: 4, gate: true, scale: 'mixolydian', notes: [0, 1, 4, 5, 6, 1, 0, 5, 4, 1, 6, 5],
      // Long tones an octave up, one per pass of the figure: A, B, G, E.
      counter: [...Array(12).fill(11), ...Array(12).fill(12), ...Array(12).fill(10), ...Array(12).fill(8)],
      mods: { timbre: [lfo('sine', 24, 1.2)], morph: [lfo('sine', 31, 1.2)] } },

    // Arpeggios and sequences.
    { id: 'minor-arp', name: 'Minor arpeggio', desc: 'Sixteenths over Am, F, C and G, random MORPH.', model: 0,
      bpm: 118, div: 4, gate: true, scale: 'minor', transpose: -5, notes: arp([0, -2, 2, -3], [0, 2, 4, 7, 9, 7, 4, 2]),
      mods: { timbre: [lfo('tri', 16, 2)], morph: [random(1.5, 4, 3, 0.05)] } },
    { id: 'triplets', name: 'Sevenths in triplets', desc: 'Major seventh chords broken in triplets.', model: 4,
      bpm: 96, div: 3, gate: true, scale: 'major', notes: arp([0, 3, 5, 4], [0, 2, 4, 6, 4, 2, 0, 2, 4, 6, 4, 2]),
      mods: { timbre: [lfo('sine', 12, 2)], morph: [lfo('tri', 24, 1.5)] } },
    { id: 'berlin', name: 'Berlin school', desc: 'A phrygian ostinato in sixteenths, a slow TIMBRE sweep and random MORPH.', model: 1,
      bpm: 128, div: 4, gate: true, scale: 'phrygian', notes: arp([0, 0, -2, -1], [0, 7, 1, 7, 3, 7, 1, 7]),
      mods: { timbre: [lfo('saw', 16, 2.5)], morph: [random(1.5, 4, 7)] } },
    { id: 'cascade', name: 'Pentatonic cascade', desc: 'Fast falling runs in a pentatonic scale.', model: 2,
      bpm: 140, div: 4, gate: true, scale: 'pentaMajor', notes: [9, 8, 7, 6, 5, 4, 3, 2, 8, 7, 6, 5, 4, 3, 2, 1],
      mods: { timbre: [env(2, 0.15)], morph: [lfo('sine', 8, 1)] } },
    { id: 'whole-tone', name: 'Whole-tone dream', desc: 'A whole-tone scale with a little glide between notes.', model: 5,
      bpm: 84, div: 2, gate: true, glide: 0.04, scale: 'whole', notes: [0, 2, 1, 3, 2, 4, 3, 5, 4, 2, 3, 1, 2, 0, 1, -1],
      mods: { timbre: [lfo('sine', 16, 2)], morph: [lfo('sine', 22, 2)] } },
    { id: 'lydian', name: 'Lydian sparkle', desc: 'High lydian sixteenths with an envelope on MORPH.', model: 2,
      bpm: 132, div: 4, gate: true, scale: 'lydian', notes: [7, 9, 11, 10, 9, 7, 8, 6, 7, 9, 10, 12, 11, 9, 8, 7],
      mods: { timbre: [lfo('tri', 8, 1.5)], morph: [env(2, 0.1)] } },
    { id: 'harmonic-minor', name: 'Harmonic minor runs', desc: 'Scale runs up and down in harmonic minor.', model: 0,
      bpm: 126, div: 4, gate: true, scale: 'harmonicMinor', notes: [0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, -1],
      mods: { timbre: [lfo('saw', 8, 2)], morph: [lfo('tri', 16, 1)] } },
    { id: 'euclid-5-8', name: 'Euclid 5 of 8', desc: 'Five notes spread over eight steps, an accent pattern on TIMBRE.', model: 6,
      bpm: 112, div: 4, gate: true, scale: 'pentaMinor', notes: place(repeat(euclid(5, 8), 2), [0, 2, 4, 3, 1]),
      // An upper line changing every half bar: A, G, F, G, A, C, A, F.
      counter: [].concat(...[8, 7, 6, 7, 8, 9, 8, 6].map(d => Array(8).fill(d))),
      mods: { timbre: [steps([3, 0, 1.5, 0, 2, 0, 1, 0])], morph: [lfo('tri', 16, 1.5)] } },
    { id: 'euclid-7-16', name: 'Euclid 7 of 16', desc: 'Seven notes spread over sixteen steps, random TIMBRE on every step.', model: 2,
      bpm: 120, div: 4, gate: true, scale: 'dorian', notes: place(euclid(7, 16), [0, 4, 2, 6, 3, 5, 1]),
      mods: { timbre: [random(2, 1, 11, 0.02)], morph: [lfo('sine', 16, 1.5)] } },
    { id: 'blues', name: 'Blues riff', desc: 'A swung riff in the blues scale.', model: 1,
      bpm: 100, div: 2, swing: 0.3, gate: true, scale: 'blues', notes: [0, 0, 2, 3, 4, 3, 2, 0, 1, 1, 2, 3, 2, 1, 0, null],
      mods: { timbre: [env(2.5, 0.3)], morph: [lfo('tri', 16, 1)] } },

    // Bass lines.
    { id: 'acid', name: 'Acid line', desc: 'Sixteenths with accents on TIMBRE and slides between notes.', model: 1,
      bpm: 126, div: 4, gate: true, glide: 0.03, scale: 'pentaMinor', transpose: -12,
      notes: [0, 0, 5, 0, 3, null, 0, 4, 0, 6, 0, 3, null, 2, 0, 1],
      mods: { timbre: [steps([3, 0, 0, 2, 0, 0, 3, 0, 1, 0, 3, 0, 0, 2, 0, 1], 0.01)], morph: [lfo('saw', 16, 2)] } },
    { id: 'walking', name: 'Walking bass', desc: 'A walking line in quarter notes.', model: 1,
      bpm: 112, div: 1, gate: true, scale: 'dorian', transpose: -12, notes: [0, 2, 4, 5, 3, 5, 6, 8, 4, 3, 2, 1, 0, 1, 2, 4],
      mods: { timbre: [env(2, 0.35)], morph: [lfo('tri', 32, 1)] } },
    { id: 'disco', name: 'Octave disco', desc: 'Bass jumping between octaves, TIMBRE pulsing.', model: 0,
      bpm: 118, div: 2, gate: true, scale: 'dorian', notes: [].concat(...[0, -2, 3, -4].map(r => repeat([r - 7, r], 4))),
      mods: { timbre: [lfo('sine', 2, 1.5)], morph: [lfo('tri', 16, 1)] } },
    { id: 'dub', name: 'Dub bass', desc: 'A slow, sparse low line, an envelope on MORPH.', model: 1,
      bpm: 74, div: 2, gate: true, scale: 'minor', transpose: -12,
      notes: [0, null, null, 0, 2, null, 3, null, 0, null, null, -2, -1, null, 0, null],
      mods: { morph: [env(3, 0.6)], timbre: [lfo('tri', 32, 1)] } },
    { id: 'long-bass', name: 'Long bass', desc: 'Long low notes, TIMBRE slowly opening and closing.', model: 0,
      bpm: 86, div: 1, gate: true, scale: 'minor', transpose: -12, notes: [0, 0, -2, -2, 3, 3, 2, 1],
      mods: { timbre: [lfo('tri', 16, 2)], morph: [lfo('sine', 24, 1)] } },

    // Slow loops and drones (TRIGGER unpatched: the low-pass gate open).
    { id: 'root-fifth', name: 'Root and fifth', desc: 'A drone gliding between the key note and the fifth.', model: 4,
      bpm: 60, div: 0.25, gate: false, glide: 0.3, scale: 'dorian', notes: [0, 4],
      mods: { timbre: [lfo('sine', 32, 2.5)], morph: [lfo('tri', 48, 2)] } },
    { id: 'sigh', name: 'Sigh', desc: 'A drone sliding down by semitones and back.', model: 5,
      bpm: 60, div: 0.5, gate: false, glide: 0.4, scale: 'chromatic', notes: [0, -1, -2, -1],
      mods: { morph: [lfo('sine', 40, 2)], timbre: [lfo('tri', 28, 1.5)] } },
    { id: 'fourths', name: 'Circle of fourths', desc: 'A drone gliding through the circle of fourths.', model: 0,
      bpm: 60, div: 0.5, gate: false, glide: 0.25, scale: 'chromatic', notes: [0, 5, -2, 3, -4, 1, -6, -1],
      mods: { timbre: [lfo('sine', 16, 2)], morph: [lfo('tri', 24, 1)] } },
    { id: 'breathing', name: 'Breathing', desc: 'One held note, TIMBRE rising and falling like a breath.', model: 3,
      bpm: 60, div: 1, gate: false, scale: 'dorian', notes: [0],
      mods: { timbre: [lfo('sine', 2.5, 2)], morph: [lfo('sine', 20, 1.5)] } },
    { id: 'wobble', name: 'Wobble', desc: 'A low drone with TIMBRE wobbling in eighths, HARMONICS drifting.', model: 1,
      bpm: 140, div: 0.25, gate: false, scale: 'dorian', notes: [-7, -8, -5, -4],
      mods: { timbre: [lfo('sine', 0.5, 3)], harmonics: [lfo('sine', 8, 0.5)] } },
    { id: 'random-walk', name: 'Random walk', desc: 'Slow notes, TIMBRE and MORPH wandering at random.', model: 2,
      bpm: 60, div: 0.5, gate: false, glide: 0.2, scale: 'dorian', notes: [0, -1, 2, -2],
      // A slow line above, a third, fifth or sixth away: F, E, D, D, then A,
      // G, F, E.
      counter: [9, 8, 7, 7, 11, 10, 9, 8],
      mods: { timbre: [random(2.5, 1, 5, 0.4)], morph: [random(2, 1, 9, 0.6)] } },
    { id: 'slow-glides', name: 'Slow glides', desc: 'A slow melody gliding from note to note.', model: 4,
      bpm: 70, div: 1, gate: false, glide: 0.5, scale: 'dorian', notes: [0, 2, 3, 4, 3, 2],
      mods: { timbre: [lfo('tri', 12, 1.5)], morph: [lfo('sine', 18, 1)] } },
    { id: 'chant', name: 'Chant', desc: 'A slow sung tune, held notes gliding into each other, with vibrato.', model: 7,
      bpm: 90, div: 0.5, gate: false, glide: 0.08, scale: 'dorian', notes: [0, 1, 2, 1, 3, 2, 1, 0],
      // Counterpoint above the chant, in contrary motion, octaves, thirds and
      // sixths, ending on the cadence C to D over E to D.
      counter: [7, 6, 4, 6, 5, 4, 6, 7],
      mods: { pitch: [lfo('sine', 0.3, 0.18)], morph: [lfo('tri', 16, 1.5)] } },
    { id: 'harmonics-sweep', name: 'HARMONICS sweep', desc: 'One held note, HARMONICS slowly swept by its CV.', model: 2,
      bpm: 60, div: 1, gate: false, scale: 'dorian', notes: [0],
      mods: { harmonics: [lfo('tri', 16, 1.2)], timbre: [lfo('sine', 24, 1)] } },
    { id: 'chromatic', name: 'Chromatic line', desc: 'A line falling and climbing by semitones.', model: 3,
      bpm: 100, div: 2, gate: true, scale: 'chromatic', notes: [12, 11, 10, 9, 8, 7, 6, 5, 6, 7, 8, 9, 10, 11, 12, 7],
      mods: { timbre: [lfo('tri', 8, 2)], morph: [lfo('sine', 12, 1.5)] } },

    // Rhythms.
    { id: 'plucked', name: 'Plucked ostinato', desc: 'A repeated plucked figure, accented by an envelope on TIMBRE.', model: 6,
      bpm: 104, div: 2, gate: true, scale: 'dorian', notes: [0, 0, 4, 0, 3, 0, 2, 1],
      mods: { timbre: [env(2.5, 0.2)], morph: [lfo('tri', 16, 1)] } },
    { id: 'marimba', name: 'Marimba', desc: 'Sparse high sixteenths in a pentatonic scale.', model: 2,
      bpm: 120, div: 4, gate: true, scale: 'pentaMajor', notes: place(euclid(9, 16), [5, 7, 6, 8, 5, 9, 7, 6, 4]),
      mods: { morph: [env(2, 0.12)], timbre: [lfo('sine', 16, 1)] } },
    { id: 'bells', name: 'Bells', desc: 'Slow ringing notes.', model: 2,
      bpm: 72, div: 1, gate: true, scale: 'dorian', notes: [4, 2, 3, 0, 4, 5, 3, 1],
      mods: { timbre: [env(3, 0.8)], morph: [lfo('tri', 24, 2)] } },
    { id: 'stutter', name: 'Stutter', desc: 'Bursts of thirty-second notes, every other beat.', model: 0,
      bpm: 90, div: 8, gate: true, scale: 'dorian',
      notes: [].concat(...[0, 2, 3, 1].map(x => [x, x, x, x, ...Array(12).fill(null)])),
      mods: { timbre: [lfo('saw', 1, 2)], morph: [lfo('sine', 8, 1)] } },
    { id: 'triplet-groove', name: 'Triplet groove', desc: 'A shuffling triplet rhythm, accents on TIMBRE.', model: 1,
      bpm: 100, div: 3, gate: true, scale: 'mixolydian', notes: [0, null, 2, 4, null, 2, 0, null, 2, 5, null, 4],
      mods: { timbre: [steps([2, 0, 0.5, 2, 0, 0.5])], morph: [lfo('tri', 12, 1.5)] } },
    { id: 'gallop', name: 'Gallop', desc: 'Long, short, short: a galloping minor line.', model: 5,
      bpm: 132, div: 4, gate: true, scale: 'minor', notes: [0, null, 0, 0, 2, null, 2, 2, 3, null, 3, 3, 1, null, 1, 1],
      // A line over the chords of the gallop, a note per beat: A, C, B flat, G,
      // then F, A, D, C (fifths and thirds), leading back to the start.
      counter: [].concat(...[11, 13, 12, 10, 9, 11, 14, 13].map(d => Array(4).fill(d))),
      mods: { morph: [lfo('sine', 8, 1.5)], timbre: [lfo('tri', 16, 1)] } },
    { id: 'gamelan', name: 'Gamelan', desc: 'A figure in a pelog scale, an envelope on TIMBRE.', model: 2,
      bpm: 116, div: 4, gate: true, scale: 'pelog', notes: [0, 1, 2, 4, 3, 2, 1, 2, 5, 4, 3, 1, 2, 0, 1, 2],
      mods: { timbre: [env(2, 0.3)], morph: [lfo('sine', 12, 1)] } },

    // Modulation.
    { id: 'accents', name: 'Accent sequencer', desc: 'A one-note groove: an 8-step accent pattern on TIMBRE, a 5-step one on MORPH.', model: 1,
      bpm: 120, div: 4, gate: true, scale: 'dorian', notes: [0, 0, 0, 7, 0, 0, 6, 0, 0, 0, 4, 0, 0, 7, 0, 2],
      // A riff over the one-note groove, a note per beat: A, G, F, D, then F,
      // G, A, C.
      counter: [].concat(...[11, 10, 9, 7, 9, 10, 11, 13].map(d => Array(4).fill(d))),
      mods: { timbre: [steps([4, 0, 1, 0, 3, 0, 1, 2])], morph: [steps([-2, 0, 2, -1, 1])] } },
    { id: 'sample-hold', name: 'Sample and hold', desc: 'A new random TIMBRE and MORPH on every note.', model: 0,
      bpm: 128, div: 4, gate: true, scale: 'pentaMinor', notes: [0, 3, 1, 4, 2, 5, 3, 1, 0, 2, 4, 6, 5, 3, 2, 1],
      mods: { timbre: [random(3, 1, 21)], morph: [random(2, 1, 22)] } },
    { id: 'swells', name: 'Swells', desc: 'Slow notes, each one swelling through MORPH and TIMBRE.', model: 5,
      bpm: 80, div: 1, gate: true, scale: 'minor', notes: [0, 2, 4, 3, 2, 1, 0, -1],
      mods: { morph: [env(3, 1)], timbre: [env(2, 0.6)] } },
    { id: 'saw-sweeps', name: 'Saw sweeps', desc: 'An arpeggio under repeating TIMBRE ramps, MORPH flipping every two bars.', model: 1,
      bpm: 112, div: 2, gate: true, scale: 'dorian', notes: arp([0, 3], [0, 2, 4, 7, 4, 2, 0, -1]),
      mods: { timbre: [lfo('saw', 4, 2.5)], morph: [lfo('square', 8, 1.5)] } },
    { id: 'eighth-wobble', name: 'Eighth-note wobble', desc: 'A tune with TIMBRE wobbling in time, on every eighth.', model: 3,
      bpm: 100, div: 2, gate: true, scale: 'dorian', notes: [4, 2, 0, 1, 2, 4, 5, 4, 2, 1, 0, -1, 0, 1, 2, null],
      mods: { timbre: [lfo('sine', 0.5, 2)], morph: [lfo('tri', 16, 1)] } },
    { id: 'harmonics-steps', name: 'HARMONICS per note', desc: 'Each note with its own HARMONICS, from a CV sequence.', model: 2,
      bpm: 108, div: 2, gate: true, scale: 'dorian', notes: [0, 2, 4, 2, 5, 4, 3, 1],
      mods: { harmonics: [steps([0, 0.8, -0.6, 0.4, -0.3, 0.9, -0.8, 0.2])], timbre: [lfo('tri', 16, 1.5)] } },
    { id: 'glissando', name: 'Glissando arpeggio', desc: 'Wide arpeggios with every note sliding into the next.', model: 5,
      bpm: 96, div: 2, gate: true, glide: 0.06, scale: 'minor', transpose: -5, notes: arp([0, 5], [0, 4, 7, 9, 7, 4, 2, -3]),
      mods: { timbre: [lfo('sine', 12, 1.5)], morph: [lfo('tri', 20, 1)] } },
    { id: 'vowel-song', name: 'Vowel song', desc: 'A sung tune, MORPH moving through the vowels on every note.', model: 7,
      bpm: 92, div: 2, gate: true, scale: 'major', notes: [0, 2, 4, 4, 5, 4, 2, 0, 1, 2, 0, -1, 0, 2, 1, 0],
      mods: { morph: [steps([-2, 0, 2, 1, -1, 2, 0, -2])], timbre: [lfo('sine', 16, 1)] } },

    // Techno: minor keys, sixteenths.
    { id: 'rolling-bass', name: 'Rolling bass', desc: 'A low note on the last three sixteenths of every beat, accented on the second.', model: 1,
      bpm: 128, div: 4, gate: true, scale: 'minor', transpose: -12,
      notes: [null, 0, 0, 0, null, 0, 0, 0, null, 0, 0, 0, null, 0, 3, 1],
      mods: { timbre: [steps([0, 2.5, 1, 1.5])], morph: [lfo('tri', 32, 1.5)] } },
    { id: 'offbeat-bass', name: 'Off-beat bass', desc: 'Bass on every off-beat, two bars, an octave jump to close.', model: 0,
      bpm: 126, div: 4, gate: true, scale: 'minor', transpose: -12,
      notes: [null, null, 0, null, null, null, 0, null, null, null, 0, null, null, null, 0, 7,
              null, null, -2, null, null, null, -2, null, null, null, -1, null, null, null, -1, 6],
      mods: { timbre: [env(3, 0.15)], morph: [lfo('sine', 16, 1)] } },
    { id: 'hypnotic', name: 'Hypnotic', desc: 'Two notes and a flat second, a sixteen-step accent pattern, MORPH ramping over eight bars.', model: 0,
      bpm: 130, div: 4, gate: true, scale: 'phrygian', notes: [0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 4, 0, 1, 0, 4, 0],
      mods: { timbre: [steps([3, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 2, 0, 1, 0, 1], 0.005)],
              morph: [lfo('saw', 32, 1.5)], harmonics: [lfo('sine', 32, 0.3)] } },
    { id: 'minimal-stab', name: 'Minimal stab', desc: 'Three stabs per bar on a tresillo, random MORPH.', model: 5,
      bpm: 124, div: 4, gate: true, scale: 'minor', notes: place(repeat(euclid(3, 8), 2), [4, 2, 6]),
      mods: { timbre: [env(3, 0.4)], morph: [random(1.5, 3, 31)] } },
    { id: 'dub-techno', name: 'Dub techno', desc: 'Off-beat stabs fading slowly, MORPH drifting over twelve bars.', model: 4,
      bpm: 118, div: 4, gate: true, scale: 'minor',
      notes: [null, null, 0, null, null, null, 0, null, null, null, 0, null, null, null, 0, null,
              null, null, 0, null, null, null, 0, null, null, null, -2, null, null, null, -2, null],
      mods: { timbre: [env(2.5, 0.5), lfo('sine', 32, 1)], morph: [lfo('sine', 48, 2)] } },
    { id: 'acid-squelch', name: 'Acid squelch', desc: 'A phrygian acid line: slides, octave jumps and a sixteen-step accent pattern.', model: 1,
      bpm: 132, div: 4, gate: true, glide: 0.025, scale: 'phrygian', transpose: -12,
      notes: [0, 0, 7, 0, 1, 0, null, 0, 3, 0, 7, 1, 0, null, 6, 0],
      mods: { timbre: [steps([3, 0, 0, 1, 3, 0, 0, 2, 0, 3, 0, 1, 3, 0, 2, 0], 0.008)], morph: [lfo('saw', 8, 2)] } },
    { id: 'acid-octaves', name: 'Acid octaves', desc: 'An acid line leaping between octaves in harmonic minor.', model: 0,
      bpm: 128, div: 4, gate: true, glide: 0.03, scale: 'harmonicMinor', transpose: -12,
      notes: [0, 7, 0, 0, 7, 0, 6, 7, 0, 7, 0, 4, 7, 5, 7, 6],
      // A lead above the bass: A, F, B flat and A, D and F, then B flat, A,
      // G, B flat and C sharp (with the C sharp of the bass).
      counter: [11, 11, 11, 11, 9, 9, 9, 9, 12, 12, 12, 11, 14, 14, 14, 9,
                12, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10, 12, 12, 12, 13],
      mods: { timbre: [steps([2, 0, 3, 0, 1, 0, 3, 1], 0.008)], morph: [lfo('tri', 16, 2)] } },
    { id: 'detroit', name: 'Detroit lead', desc: 'A syncopated pentatonic lead, seven notes over sixteen steps.', model: 5,
      bpm: 126, div: 4, gate: true, scale: 'pentaMinor', notes: place(euclid(7, 16), [5, 4, 3, 4, 2, 3, 1]),
      mods: { morph: [env(2, 0.25)], timbre: [lfo('tri', 16, 1.5)] } },
    { id: 'warehouse', name: 'Warehouse pulse', desc: 'A dark low pulse on the key note and the flat second.', model: 1,
      bpm: 134, div: 4, gate: true, scale: 'phrygian', transpose: -12,
      notes: [0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 3, 1, 0],
      mods: { timbre: [steps([3, 0, 1, 0]), lfo('saw', 16, 1.5)], harmonics: [lfo('sine', 16, 0.4)] } },
    { id: 'three-four', name: 'Three over four', desc: 'A three-note figure against a four-step accent: it lands differently every beat.', model: 2,
      bpm: 128, div: 4, gate: true, scale: 'minor', notes: [0, 4, 2],
      mods: { timbre: [steps([3, 0, 1, 0])], morph: [lfo('sine', 16, 1)] } },
    { id: 'seven-sixteen', name: 'Seven over sixteen', desc: 'A seven-note figure against a sixteen-step accent pattern.', model: 0,
      bpm: 126, div: 4, gate: true, scale: 'minor', notes: [0, 2, 3, 4, 3, 2, 0],
      mods: { timbre: [steps([3, 0, 0, 1, 0, 0, 2, 0, 3, 0, 0, 1, 0, 2, 0, 0])], morph: [lfo('tri', 24, 1.5)] } },
    { id: 'tresillo-bass', name: 'Tresillo bass', desc: 'A low line on three of every eight sixteenths.', model: 1,
      bpm: 124, div: 4, gate: true, scale: 'minor', transpose: -12, notes: place(repeat(euclid(3, 8), 2), [0, 0, 3, 0, 0, -2]),
      mods: { timbre: [env(2.5, 0.25)], morph: [lfo('tri', 16, 1)] } },
    { id: 'euclid-5-16', name: 'Euclid 5 of 16', desc: 'Five high notes spread over sixteen steps, a random MORPH on each.', model: 2,
      bpm: 128, div: 4, gate: true, scale: 'pentaMinor', notes: place(euclid(5, 16, 2), [5, 7, 6, 4, 5]),
      mods: { morph: [random(2, 1, 41)], timbre: [env(2, 0.2)] } },
    { id: 'triplet-techno', name: 'Triplet techno', desc: 'A minor figure in triplets over a four-to-the-floor tempo.', model: 5,
      bpm: 128, div: 3, gate: true, scale: 'minor', notes: [0, null, 4, 0, 2, null, 0, null, 4, 3, null, 2],
      // Quarter notes against the triplets: an arch, A, F, A, B flat, then a
      // falling arpeggio, C, A, F, D.
      counter: [].concat(...[11, 9, 11, 12, 13, 11, 9, 7].map(d => Array(3).fill(d))),
      mods: { timbre: [steps([3, 0, 1])], morph: [lfo('sine', 16, 1)] } },
    { id: 'tom-groove', name: 'Tom groove', desc: 'Falling notes like a tom fill, short and plucked.', model: 6,
      bpm: 126, div: 4, gate: true, scale: 'minor', notes: [7, null, null, 4, null, 2, null, null, 7, null, 4, null, 2, null, 0, null],
      mods: { timbre: [env(2.5, 0.15)], morph: [lfo('tri', 32, 1)] } },
    { id: 'shifting', name: 'Shifting sequence', desc: 'A gapped figure moving over Am, Am, F and G, random MORPH.', model: 0,
      bpm: 128, div: 4, gate: true, scale: 'minor', transpose: -5, notes: arp([0, 0, -2, -1], [0, null, 4, 0, 7, null, 4, 2]),
      mods: { morph: [random(1.5, 2, 51)], timbre: [lfo('saw', 16, 2)] } },
    { id: 'minor-ninth', name: 'Minor ninth arpeggio', desc: 'Ninth chords broken in sixteenths, TIMBRE pumping on every beat.', model: 2,
      bpm: 124, div: 4, gate: true, scale: 'minor', notes: arp([0, 0, -2, 3], [0, 2, 4, 6, 8, 6, 4, 2]),
      mods: { timbre: [lfo('sine', 1, 1.5)], morph: [random(1, 8, 61)] } },
    { id: 'industrial', name: 'Industrial', desc: 'A low semitone grind with hard accents, HARMONICS jumping.', model: 1,
      bpm: 136, div: 4, gate: true, scale: 'phrygian', transpose: -12,
      notes: [0, 1, 0, 0, null, 0, 1, 0, 0, null, 0, 1, 0, 0, null, 1],
      // A dark line above the grind, with only notes which sound well over
      // both its D and E flat: G, B flat, C, B flat, then G, F, G, low B flat.
      counter: [].concat(...[10, 12, 13, 12, 10, 9, 10, 5].map(d => Array(4).fill(d))),
      mods: { timbre: [steps([4, 0, 2, 0, 4, 1, 0, 2])], harmonics: [steps([0, 0.5, 0, -0.3])] } },
    { id: 'peak-time', name: 'Peak-time riff', desc: 'A syncopated pentatonic riff under an eight-bar TIMBRE build.', model: 5,
      bpm: 132, div: 4, gate: true, scale: 'pentaMinor', notes: [0, null, 0, 3, null, 0, 4, null, 0, null, 5, 4, 3, null, 0, null],
      mods: { timbre: [lfo('saw', 32, 2.5)], morph: [env(2, 0.2)] } },
    { id: 'gallop-bass', name: 'Gallop bass', desc: 'A low galloping line, long, short, short.', model: 1,
      bpm: 130, div: 4, gate: true, scale: 'minor', transpose: -12,
      notes: [0, null, 0, 0, 0, null, 0, 0, -2, null, -2, -2, -1, null, -1, -1],
      mods: { timbre: [steps([3, 0, 1, 1.5])], morph: [lfo('sine', 16, 1)] } },
  ];

  // The loops of ~/projects/fxaid (tools/plaits_loop and tools/rings_loop),
  // the input loops of its effect pages, at 120 BPM, in MIDI notes (D3, 50,
  // is the pitch set by FREQUENCY). Some of them were written for two
  // modules, or for Rings' four voices: they keep their own second voice. The
  // others have a second voice written for them.
  const midi = notes => notes.map(m => m === null ? null : m - 50);
  // A list of `length` steps, with values on some of them (null elsewhere).
  const at = (length, events) => {
    const out = Array(length).fill(null);
    for (const [step, value] of events) out[step] = value;
    return out;
  };
  // Notes held for a number of steps: [[note, steps], ...].
  const held = notes => [].concat(...notes.map(([m, n]) => [m, ...Array(n - 1).fill(null)]));

  // Rings live patch: a cluster of four sixteenths, a quarter rest, three,
  // an eighth rest, two, over C, G, Am and F.
  const ringsChords = [[60, 64, 67, 72, 76], [55, 59, 62, 67, 71], [57, 60, 64, 69, 72], [53, 57, 60, 65, 69]];
  const ringsLive = [].concat(...ringsChords.map(c =>
      [0, 1, 2, 3, null, null, null, null, 4, 3, 2, null, null, 1, 0, null].map(i => i === null ? null : c[i])));

  // EBM bass: relentless sixteenths with octave jumps over E, E, C and D.
  const ebmA = [1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 3, 1, 2, 1];
  const ebmB = [1, 1, 2, 1, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 3, 2];
  const ebm = [].concat(...[40, 40, 36, 38].map((root, bar) =>
      (bar % 2 ? ebmB : ebmA).map(kind => root + [0, 0, 12, 7][kind])));
  const ebmAccents = [1, 0.3, 0.6, 0.3, 0.5, 0.3, 0.9, 0.3, 0.5, 0.3, 0.9, 0.3, 0.6, 0.3, 0.7, 0.4];

  // Rings syncopated: Dm9, B flat maj7, Gm9 and A7sus4 to A7b9, in ticks of
  // a twelfth of a beat (sixteenths are 3, the eighth-note triplet 4). The
  // chords were a bass note and a note above it, on the same tick. Here only
  // V/OCT plays the line (TRIGGER is unpatched, the low-pass gate open):
  // each note is held until the next.
  const syncoTop = [[0, 57], [9, 64], [12, 65], [21, 69], [30, 72], [39, 76],
                    [45, 53], [54, 69], [60, 74], [63, 72], [66, 69], [78, 65], [81, 67],
                    [90, 62], [102, 70], [108, 69], [120, 74], [124, 72], [128, 70], [132, 69], [141, 64],
                    [144, 62], [150, 67], [159, 73], [171, 70], [174, 69], [183, 64], [189, 61]];
  // (The first one an octave up, D3: LEVEL reads no lower than 11 semitones
  // below the pitch set by FREQUENCY, see LEVEL_LOWEST.)
  const syncoBass = [[0, 50], [45, 46], [90, 43], [144, 45]];

  // String machine: Fm9, D flat maj9, B flat m11, Csus4 and C, the chords
  // pushed an eighth ahead of the bar line, re-struck off the beat. Here only
  // V/OCT plays them (TRIGGER is unpatched, the low-pass gate open): the root
  // of each chord, held until the next one.
  // The sixteenths where the chords were struck: the top line moves on them.
  const strokes = [0, 6, 10, 14, 22, 26, 30, 38, 42, 46, 54];

  // Vowel duet, in E minor, in eighths: the lead and the low voice.
  const lead = [[0, 71], [3, 69], [4, 67], [6, 64], [8, 67], [9, 69], [12, 71], [14, 72], [15, 71],
                [16, 74], [22, 71], [23, 69], [24, 69], [25, 67], [26, 66]];
  const low = [[0, 55], [4, 52], [8, 52], [12, 55], [16, 59], [20, 57], [24, 54], [26, 57]];

  // Wavetable sequence: A minor sixteenths (with accents), alternating
  // between two modules.
  const waves = [[0, 45, 1], [2, 57, 0], [3, 52, 0], [5, 45, 0], [6, 60, 1], [8, 45, 0], [10, 55, 0], [11, 57, 1], [13, 64, 0], [15, 62, 0],
                 [16, 41, 1], [18, 53, 0], [19, 60, 0], [21, 41, 0], [22, 57, 1], [24, 41, 0], [26, 52, 0], [27, 53, 1], [29, 60, 0], [30, 57, 0],
                 [32, 50, 1], [34, 62, 0], [35, 57, 0], [37, 50, 0], [38, 65, 1], [40, 50, 0], [42, 60, 0], [43, 62, 1], [45, 69, 0], [47, 65, 0],
                 [48, 40, 1], [51, 59, 0], [52, 52, 0], [54, 56, 1], [56, 52, 0], [57, 59, 0], [58, 62, 0], [60, 64, 1]];
  // In duo mode, the notes alternate between the voices as they did between
  // the modules, each voice holding its note through the other's. TRIGGER
  // pings both voices together, so the held note sounds again with the new
  // one: where the two would make a second, a tritone or a seventh, the new
  // note goes to the same voice as the one before (7 of the 38 notes).
  const clash = (x, y) => { const d = Math.abs(x - y), i = d % 12; return i === 1 || i === 6 || i === 11 || (i === 2 && d < 12); };
  const wavesVoice = (() => {
    let held = [waves[waves.length - 2][1], waves[waves.length - 1][1]], last = 1, voices = [];
    for (let pass = 0; pass < 2; ++pass) {  // the second pass starts where the loop ends
      voices = waves.map(([, m]) => {
        let v = 1 - last;
        if (clash(m, held[1 - v]) && !clash(m, held[v])) v = last;
        held[v] = m;
        last = v;
        return v;
      });
    }
    return voices;
  })();
  // The pitch of each voice at each note (the first voice is pinged on
  // every note, with its held pitch when the note is the second voice's).
  const wavesHeld = v => waves.map((_, i) => {
    for (let j = i; j > i - waves.length; --j) {
      const k = (j + waves.length) % waves.length;
      if (wavesVoice[k] === v) return waves[k][1];
    }
    return waves[i][1];
  });
  const wavesA = at(64, wavesHeld(0).map((m, i) => [waves[i][0], m]));
  const wavesB = at(64, wavesHeld(1).map((m, i) => [waves[i][0], m]));

  const fxaid = [
    { id: 'rings-live', name: 'Rings live', desc: 'Rings, four-voice clusters of sixteenths with rests over C, G, Am and F.', model: 6,
      bpm: 120, div: 4, gate: true, scale: 'chromatic', notes: midi(ringsLive),
      // A bass line moving under the clusters, one note for each: C, E, D,
      // then B, D, G, then A, C, G, then F, A, G (the passing notes lead to
      // the next chord).
      counter: midi(at(64, [48, 52, 50, 47, 50, 43, 45, 48, 43, 41, 45, 43].map((m, i) =>
          [16 * Math.floor(i / 3) + [0, 8, 13][i % 3], m]))),
      mods: { timbre: [env(1.2, 0.25)], morph: [lfo('sine', 16, 0.8)] } },
    { id: 'ebm-bass', name: 'EBM bass', desc: 'An EBM bass line in E minor, sixteenths with octave jumps, the accents opening TIMBRE.', model: 0,
      bpm: 120, div: 4, gate: true, scale: 'chromatic', notes: midi(ebm),
      // A falling hook above the bass, E, D, B and then G, A or C, the last
      // bar turning around over D: D, A, F sharp, A, B, back to E.
      counter: midi(held([[76, 3], [74, 3], [71, 4], [67, 2], [71, 4],
                          [76, 3], [74, 3], [71, 4], [69, 2], [67, 4],
                          [76, 3], [74, 3], [72, 4], [67, 2], [64, 4],
                          [74, 3], [69, 3], [66, 4], [69, 2], [71, 4]])),
      mods: { timbre: [lfo('sine', 16, 0.6), env(2.6, 0.12, ebmAccents)] } },
    { id: 'rings-synco', name: 'Rings syncopated', desc: 'A held line over Dm9, B flat maj7, Gm9 and A7: pushed chords, sixteenth pickups and a triplet.', model: 6,
      bpm: 120, div: 12, gate: false, scale: 'chromatic', notes: midi(at(192, syncoTop)),
      // Its own bass notes, held under the line.
      counter: midi(at(192, syncoBass)),
      mods: { timbre: [lfo('tri', 8, 1.2)], morph: [lfo('sine', 16, 1)] } },
    { id: 'string-machine', name: 'String machine', desc: 'Fm9, D flat maj9, B flat m11 and Csus4 to C, held notes, the chords pushed an eighth.', model: 5,
      bpm: 120, div: 4, gate: false, scale: 'chromatic', notes: midi(at(64, [[0, 53], [14, 49], [30, 46], [46, 48]])),
      // The top of the chords, over their roots: A flat, G, E flat, then F,
      // A flat, F, then D flat, E flat, F, the F held into the sus4, which
      // resolves to E.
      counter: midi(at(64, [68, 67, 63, 65, 68, 65, 61, 63, 65, 65, 64].map((m, i) => [strokes[i], m]))),
      mods: { timbre: [lfo('sine', 8, 1.25)], morph: [lfo('tri', 16, 2.6)] } },
    { id: 'vowel-duet', name: 'Vowel duet', desc: 'Two voices singing in E minor, gliding, with vibrato, MORPH moving through the vowels.', model: 7,
      bpm: 120, div: 2, gate: false, glide: 0.035, scale: 'chromatic', notes: midi(at(32, lead)),
      // Its own low voice.
      counter: midi(at(32, low)),
      mods: { pitch_a: [vibrato(0.25, 5.2, 0.25, 0.4, 'a')], pitch_b: [vibrato(0.25, 4.7, 0.25, 0.4, 'b')],
              morph: [lfo('sine', 8, 3)], timbre: [lfo('sine', 16, 0.8)] } },
    { id: 'wavetable-seq', name: 'Wavetable sequence', desc: 'A minor sixteenths, HARMONICS sweeping the wavetables, an envelope on MORPH.', model: 5,
      bpm: 120, div: 4, gate: true, scale: 'chromatic', notes: midi(at(64, waves.map(([s, m]) => [s, m]))),
      // Its own two modules: the notes alternate between the voices.
      duoNotes: midi(wavesA), counter: midi(wavesB),
      mods: { harmonics: [lfo('tri', 16, 1)],
              timbre: [lfo('sine', 4, 2.1), { steps: at(64, waves.map(([s, , a]) => [s, a * 1.05])).map(x => x || 0), perNote: true }],
              morph: [env(3.5, 0.2)] } },
  ];

  const groups = [['Melodies', 10], ['Arpeggios and sequences', 10], ['Bass lines', 5],
                  ['Slow and drones', 10], ['Rhythms', 7], ['Modulation', 8], ['Techno', 20]];
  let i = 0;
  for (const [group, n] of groups) {
    for (let j = 0; j < n; ++j) candidates[i++].group = group;
  }
  current.forEach(l => { l.group = 'On the page now'; });
  fxaid.forEach(l => { l.group = 'fxaid'; candidates.push(l); });

  // The second voices, made from the melody. A loop whose melody is low (a
  // bass line) gets its pedal above it, and a second voice which would go
  // too high or too low is moved by an octave.
  const VOICES = [
    { id: 'off', name: 'One voice', desc: 'LEVEL unpatched: the melody alone.' },
    { id: 'own', name: 'Its own second melody', desc: 'The second melody this loop had on the page.' },
    { id: 'counter', name: 'Countermelody', desc: 'A second melody written for the loop.' },
    { id: 'octave-up', name: 'Octave above', desc: 'The melody an octave higher.', semis: 12 },
    { id: 'octave-down', name: 'Octave below', desc: 'The melody an octave lower.', semis: -12 },
    { id: 'fifth', name: 'Fifth above', desc: 'Parallel fifths.', semis: 7 },
    { id: 'third', name: 'Third above', desc: 'A third above, in the scale.', degrees: 2, chromatic: 4 },
    { id: 'sixth', name: 'Sixth below', desc: 'A sixth below, in the scale.', degrees: -5, chromatic: -9 },
    { id: 'canon', name: 'Canon', desc: 'The melody two beats later, an octave up.' },
    { id: 'contrary', name: 'Contrary motion', desc: 'The melody upside down: when it climbs, the second voice falls.' },
    { id: 'pedal', name: 'Pedal', desc: 'The key note held, an octave below (above a bass line).' },
    { id: 'unison', name: 'Detuned unison', desc: 'The same notes, a few cents apart, drifting: slow beating.' },
    { id: 'phasing', name: 'Phasing', desc: 'The melody one step shorter, an octave up: it drifts against itself.' },
  ];
  const BY_VOICE = Object.fromEntries(VOICES.map(v => [v.id, v]));

  const toSemis = (loop, notes) =>
      notes.map(d => d === null ? null : deg(loop.scale, d) + (loop.transpose || 0));
  const plus = (notes, n) => notes.map(x => x === null ? null : x + n);
  // The lowest note of the second voice: LEVEL was made for 0 to 8V, and its
  // input stage clips at about -0.92V, 11 semitones below the pitch set by
  // FREQUENCY (on the module too).
  const LEVEL_LOWEST = -11;
  // A second voice moved by octaves to stay in a useful range (B2 to E5, with
  // FREQUENCY where the page starts).
  const fit = notes => {
    const heard = notes.filter(x => x !== null);
    if (!heard.length) return notes;
    let octaves = 0;
    while (Math.max(...heard) + octaves > 26) octaves -= 12;
    while (Math.min(...heard) + octaves < LEVEL_LOWEST) octaves += 12;
    return plus(notes, octaves);
  };

  // The loop as the sequencer plays it (see web/worklet.js), with the second
  // voice `voiceId` (its notes are only heard when LEVEL is patched).
  function resolve(loop, voiceId) {
    const voice = BY_VOICE[voiceId] || VOICES[0];
    // A loop which splits its melody between the two voices in duo mode.
    const a = toSemis(loop, voice.id === 'counter' && loop.duoNotes ? loop.duoNotes : loop.notes);
    const heard = a.filter(x => x !== null);
    const mean = heard.reduce((s, x) => s + x, 0) / Math.max(heard.length, 1);
    const mods = Object.assign({}, loop.mods || {});
    const chromatic = loop.scale === 'chromatic';
    let b = a;
    switch (voice.id) {
      case 'own':
        if (loop.own) b = toSemis(loop, loop.own);
        break;
      case 'counter':
        if (loop.counter) b = toSemis(loop, loop.counter);
        break;
      case 'octave-up': case 'octave-down': case 'fifth':
        b = plus(a, voice.semis);
        break;
      case 'third': case 'sixth':
        b = chromatic ? plus(a, voice.chromatic) : toSemis(loop, plus(loop.notes, voice.degrees));
        break;
      case 'canon': {
        const n = Math.min(Math.round(2 * (loop.div || 2)), a.length - 1);
        b = plus(a.map((_, i) => a[(i - n + a.length) % a.length]), 12);
        break;
      }
      case 'contrary': {
        // The degrees mirrored around the first note, an octave up.
        const first = loop.notes.find(d => d !== null) || 0;
        const octave = SCALES[loop.scale].length;
        b = toSemis(loop, loop.notes.map(d => d === null ? null : 2 * first - d + octave));
        break;
      }
      case 'pedal': {
        const key = loop.transpose || 0;
        b = a.map(() => mean < -6 ? key + 12 : key - 12);
        break;
      }
      case 'unison':
        b = plus(a, 0.12);
        mods.pitch_b = [...(mods.pitch_b || []), lfo('sine', 6, 0.08)];
        break;
      case 'phasing':
        b = plus(a.slice(0, Math.max(a.length - 1, 1)), 12);
        break;
    }
    return { bpm: loop.bpm, div: loop.div, swing: loop.swing || 0, gate: loop.gate, glide: loop.glide || 0, a, b: fit(b), mods };
  }

  globalThis.PLAITS_LOOPS = { current, candidates, voices: VOICES, resolve };
})();
