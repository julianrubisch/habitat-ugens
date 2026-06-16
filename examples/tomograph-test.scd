// Tomograph — 16-band TPT SVF filter bank (port of stolmine ER-301 Filterbank)
// splat order: in, f0..f15, g0..g15, q, mix, tanh, inLevel, outLevel, slew, nbands, ftype
s.boot;

// NOTE on "ringing": the bandpass tap peaks at 0 dB for any Q. Higher q does
// not make it LOUDER — it NARROWS the band and LENGTHENS the decay. So you
// only hear "ringing" when the excitation is impulsive. Continuous noise
// re-excites every band each sample and masks the decay tail entirely.

// --- helper: 8 active bands on a minor-ish chord, rest muted --------------
(
~freqs = [110, 165, 220, 277, 330, 440, 554, 660, 0,0,0,0,0,0,0,0];
~gains = ~freqs.collect { |f| (f > 0).asInteger };
)

// 1) PING TEST — sparse impulses; each one rings out. ftype 2 = resonator
//    (Q floor 20), q near 1 = long decay. THIS is where you hear ringing.
(
~t = {
    var ex = Dust.ar(3) * 0.8;                          // impulsive, no steady noise
    Tomograph.ar(ex, *(~freqs ++ ~gains
        ++ [1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 8, 2])) ! 2;  // q=1, mix=1, reson
}.play;
)
// to compare ring length: re-evaluate block 1 with a smaller q (e.g. 0.3).

// 2) Q SWEEP — single impulse train, q glides 0 -> 1; ring time grows audibly
(
~t.free;
~t = {
    var q  = LFTri.kr(0.1).range(0.0, 1.0);             // sweep q
    var ex = Impulse.ar(2) * 0.8;
    Tomograph.ar(ex, *(~freqs ++ ~gains
        ++ [q, 1.0, 0.0, 1.0, 1.0, 0.0, 8, 2])) ! 2;
}.play;
)

// 3) NOISE excitation — for contrast: steady noise = narrowband/vocoder tone,
//    NOT ringing. This is the masking case described above.
(
~t.free;
~t = {
    var ex = PinkNoise.ar(0.3) + Dust.ar(5);
    Tomograph.ar(ex, *(~freqs ++ ~gains
        ++ [0.9, 1.0, 0.0, 1.0, 1.0, 0.0, 8, 2])) ! 2;
}.play;
)

~t.free;
