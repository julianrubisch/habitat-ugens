s.boot;
(
~t = {
    var freqs = [110, 165, 220, 277, 330, 440, 554, 660, 0,0,0,0,0,0,0,0];
    var gains = freqs.collect { |f| (f > 0).asInteger };
    var ex = PinkNoise.ar(0.3) + Dust.ar(5);
    Tomograph.ar(ex, *(freqs ++ gains ++ [0.9, 1.0, 0.0, 1.0, 1.0, 0.0, 8, 2])) ! 2;
}.play;
)
// arg order: in, f0..f15, g0..g15, q, mix, tanh, inLevel, outLevel, slew, nbands, ftype
