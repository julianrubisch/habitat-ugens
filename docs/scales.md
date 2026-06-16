# Scales (Scala `.scl`)

Tomograph distributes its 16 band frequencies from a musical scale. Scales are
read from **Scala `.scl`** files, the de-facto standard tuning format.

## Where to get `.scl` files

- **Scala scale archive** (Huygens-Fokker Foundation) — ~5000 scales, the
  canonical source: <https://www.huygens-fokker.org/scala/downloads.html>
  (direct archive: <https://www.huygens-fokker.org/docs/scales.zip>).
- The `.scl` format spec: <https://www.huygens-fokker.org/scala/scl_format.html>

> **macOS/Linux unzip caveat.** The archive ships CR-LF line endings. Extract
> with `unzip -aa scales.zip` (autoconvert) or the files may fail to parse.

A handful of classic scales is bundled in [`../examples/scales/`](../examples/scales/)
so the examples run out of the box (already converted to LF):

| File | Scale |
|------|-------|
| `meanquar.scl` | 1/4-comma meantone (P. Aaron, 1523) |
| `pyth_12.scl` | 12-tone Pythagorean |
| `young-lm_piano.scl` | Thomas Young well-temperament (1799) |
| `partch_43.scl` | Harry Partch 43-tone just intonation |
| `alves_slendro.scl` | Javanese slendro (B. Alves) |
| `alves_pelog.scl` | Javanese pelog (B. Alves) |

These remain under the terms of the Scala archive; credit the Huygens-Fokker
Foundation and the individual scale authors named in each file's header.

## Loading scales: the TuningLib quark

Parsing is delegated to the **TuningLib** quark's `Scala` class (maintained
upstream) rather than a hand-rolled parser. Install it once, then recompile:

```supercollider
Quarks.install("TuningLib");
thisProcess.recompile;   // or Cmd+Shift+L
```

`Scala` extends `Tuning`; `Scala.open(path).cents` returns the scale's degree
cents with the unison (`0`) first and the octave excluded. Verified against
SuperCollider 3.14.1 (`Tuning.cents` -> `tuning * 100`).

## Copy-paste: drive Tomograph from a `.scl`

Edit `~scaleDir` to point at this repo's `examples/scales/` (absolute path),
then evaluate each block.

```supercollider
// 0) one-time install (skip if already installed)
Quarks.install("TuningLib"); thisProcess.recompile;

// 1) helpers — load scale cents, map to 16 band frequencies
(
~scaleDir = "/path/to/habitat-ugens/examples/scales";   // <- edit to your checkout

// primary loader: TuningLib Scala; falls back to a tiny inline parser if absent.
// returns in-period degree cents, 0 < c <= 1200, unison excluded.
~scalaCents = { |path|
    if(\Scala.asClass.notNil) {
        Scala.open(path).cents.select { |c| (c > 0) and: { c <= 1200 } };
    } {
        var lines = File.readAllString(path).split($\n)
            .collect(_.stripWhiteSpace).reject { |l| l.isEmpty or: { l[0] == $! } };
        lines = lines.drop(2);   // drop name line + pitch-count line
        lines.collect { |l|
            var tok = l.split($ ).first;
            if(tok.contains($.)) { tok.asFloat }                              // cents
            { var pq = tok.split($/); 1200 * (pq[0].asFloat / (pq[1] ? "1").asFloat).log2 }  // ratio
        }.select { |c| (c > 0) and: { c <= 1200 } };
    };
};

// fill 16 bands from a root, repeating the scale up successive octaves.
~scaleToFreqs = { |cents, root = 110, nbands = 16|
    var deg = [0] ++ cents;   // unison is band 0
    Array.fill(nbands, { |i|
        var oct = i div: deg.size;
        root * ((deg.wrapAt(i) + (1200 * oct)) / 100).midiratio;
    });
};
)

// 2) play: noise/impulses through the scale-tuned filter bank
(
var cents = ~scalaCents.(~scaleDir +/+ "meanquar.scl");
var freqs = ~scaleToFreqs.(cents, root: 110, nbands: 16);
var gains = Array.fill(16, 1);
// splat order: in, f0..f15, g0..g15, q, mix, tanh, inLevel, outLevel, slew, nbands, ftype
{
    var ex = PinkNoise.ar(0.3) + Dust.ar(5);
    Tomograph.ar(ex, *(freqs ++ gains ++ [0.9, 1.0, 0.0, 1.0, 1.0, 0.05, 16, 2])) ! 2;
}.play;
)
```

Swap the filename to hear other tunings; raise `q` toward `1` and use
`ftype: 2` (resonator) to make the scale ring.

## In a SynthDef

For reuse and live control, wrap Tomograph in a SynthDef. The 16 band
frequencies and 16 gains are exposed as **arrayed controls** (`\freqs`,
`\gains`) so a whole scale can be set at instantiation and updated live with
`.setn`.

```supercollider
(
SynthDef(\tomograph, { |out = 0, in = 0, gate = 1, amp = 0.5,
    q = 0.5, mix = 1, drive = 0, inLevel = 1, outLevel = 1,
    slew = 0.05, nbands = 16, ftype = 2|

    var freqs = \freqs.kr(Array.fill(16, 440));   // 16 band frequencies (Hz)
    var gains = \gains.kr(Array.fill(16, 1));      // 16 per-band gains

    // excitation: internal noise + impulses. Swap for `In.ar(in, 1)` to
    // process an audio bus instead.
    var ex  = PinkNoise.ar(0.3) + Dust.ar(5);
    var sig = Tomograph.ar(ex,
        *(freqs ++ gains ++ [q, mix, drive, inLevel, outLevel, slew, nbands, ftype]));

    var env = EnvGen.kr(Env.asr(0.01, 1, 0.2), gate, doneAction: 2);
    Out.ar(out, (sig * env * amp) ! 2);
}).add;
)

// instantiate from a scale (reuses ~scalaCents / ~scaleToFreqs above)
(
var freqs = ~scaleToFreqs.(~scalaCents.(~scaleDir +/+ "meanquar.scl"), root: 110);
~syn = Synth(\tomograph, [\freqs, freqs, \gains, Array.fill(16, 1), \q, 0.9, \ftype, 2]);
)

// retune live without rebuilding the synth
(
var freqs = ~scaleToFreqs.(~scalaCents.(~scaleDir +/+ "partch_43.scl"), root: 110);
~syn.setn(\freqs, freqs);
)

~syn.set(\gate, 0);   // release
```

> `.setn` (not `.set`) is required for arrayed controls — it writes the whole
> 16-element block. `\freqs.kr([...])` and `\gains.kr([...])` define those
> blocks; their default-array length fixes the control size at `add` time.

