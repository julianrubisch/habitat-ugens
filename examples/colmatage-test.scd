// Colmatage — clock-driven breakbeat cutter (port of stolmine ER-301 Colmatage,
// BBCut/WarpCut -> Livecut lineage). It continuously records `in` into a buffer
// and replays clock-synced slices of it: stutters, rolls, reversals.
//
// arg order: in, clock, reset, density, blockSize, blockMax, repeatCount,
//   ritardBias, blend, accel, subdiv, phraseMin, phraseMax, dutyCycle,
//   ampMin, ampMax, fade, mix, inLevel, outLevel, tanh, bufnum, maxdur
s.boot;

// ============================================================
// 1) BASIC CUTTING (internal buffer)
// A synthesized drum loop is fed in as the source; Colmatage records it and,
// on each clock beat, decides whether to pass it straight or cut it into
// repeats. Drive `clock` with Impulse.ar — one pulse = one beat. The .set
// lines below show what each parameter does; evaluate them one at a time.
// ============================================================
(
SynthDef(\colmDemo, { |clockHz = 2, density = 0.6, blockMax = 8, repeatCount = 4,
    blend = 0.5, accel = 0.9, ritardBias = 0.5, dutyCycle = 1, sub = 8,
    mix = 1, tanh = 0|
    // rough 4/4 drum loop to be cut
    var beat  = Impulse.ar(clockHz);
    var kick  = SinOsc.ar(55) * EnvGen.ar(Env.perc(0.001, 0.18), beat);
    var hat   = HPF.ar(WhiteNoise.ar, 8000) * EnvGen.ar(Env.perc(0.001, 0.03), Impulse.ar(clockHz * 4)) * 0.4;
    var snare = (PinkNoise.ar + SinOsc.ar(190)) * EnvGen.ar(Env.perc(0.001, 0.12), Impulse.ar(clockHz, 0.5)) * 0.5;
    var src   = (kick + hat + snare) * 0.7;
    var sig   = Colmatage.ar(src, beat, 0,
        density: density, blockMax: blockMax, repeatCount: repeatCount,
        ritardBias: ritardBias, blend: blend, accel: accel, subdiv: sub,
        dutyCycle: dutyCycle, mix: mix, tanh: tanh);
    Out.ar(0, sig ! 2);
}).add;
)
x = Synth(\colmDemo);

x.set(\density, 0.95);                       // almost always cut (vs. pass straight)
x.set(\repeatCount, 8, \blend, 0.1);         // longer rolls, favour accel/ritard shapes
x.set(\accel, 0.7);                          // sharper accelerando within a roll
x.set(\ritardBias, 0.9);                     // often flip the roll into a ritardando
x.set(\dutyCycle, -0.6);                     // negative = reverse slices, 60% duty (gaps)
x.set(\sub, 16);                             // finer subdivision => smaller units
x.set(\tanh, 0.5, \mix, 0.85);               // drive + a little dry blend
x.set(\density, 0.0);                        // back to dry passthrough
x.free;

// ============================================================
// 2) LIVE INPUT via an EXTERNAL buffer
// Pass your own mono Buffer with `bufnum` instead of the internal one. Here we
// cut live audio coming into input channel 0 (mic / interface).
// ============================================================
(
b = Buffer.alloc(s, 8 * s.sampleRate, 1);    // 8 s mono record buffer
SynthDef(\colmLive, { |bufnum|
    var src = SoundIn.ar(0);
    Out.ar(0, Colmatage.ar(src, Impulse.ar(2), 0,
        density: 0.7, repeatCount: 6, bufnum: bufnum) ! 2);
}).add;
)
y = Synth(\colmLive, [\bufnum, b]);
y.free; b.free;

// ============================================================
// 3) RESET
// `reset` (a trigger) restarts the phrase/block scheduler — useful to lock the
// cutting back to the top of a bar. Here a slow Impulse resets every 4 s.
// ============================================================
(
z = {
    var beat = Impulse.ar(4);                // 240 bpm feel
    var src  = Saw.ar(110) * LFPulse.ar(4, 0, 0.3) * 0.3;
    Colmatage.ar(src, beat, Impulse.ar(0.25),   // reset every 4 s
        density: 0.9, blockMax: 4, subdiv: 16, repeatCount: 8) ! 2;
}.play;
)
z.free;
