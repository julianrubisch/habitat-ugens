# habitat-ugens

Native SuperCollider server plugins (UGens) ported from
[`stolmine/er-301-habitat`](https://github.com/stolmine/er-301-habitat), a
collection of units for the ER-301 Sound Computer.

This repository is a **port**, not the original work. Every unit here derives
from the corresponding unit in `stolmine/er-301-habitat`; that project is the
source of all DSP designs and the primary credit for the sound of these UGens.
Please refer upstream for the canonical implementations and documentation.

## Units

| UGen | Source class | Lineage / notes |
|------|--------------|-----------------|
| Tomograph | Filterbank | 16-band TPT SVF bank, scale distribution |
| Colmatage | Colmatage | BBCut/WarpCut, N. Collins → Livecut (R. Muller), GPLv2 |

> Each unit is built only when its name is uncommented in the `HABITAT_UGENS`
> list in `CMakeLists.txt` and `src/<Name>.cpp` + `sc/<Name>.sc` both exist.
> Until a unit's own brief lands, its row above is reserved.

## Layout

```
habitat-ugens/
├── CMakeLists.txt        # build harness; add a unit to the HABITAT_UGENS list
├── src/                  # one <Name>.cpp per UGen (the server plugin)
├── sc/                   # one <Name>.sc per UGen (the language class)
├── examples/             # <name>-test.scd per UGen
└── HelpSource/Classes/   # optional <Name>.schelp per UGen
```

## Requirements

- A SuperCollider **source tree** matching the SuperCollider you actually run.
  The plugin stamps an `api_version` taken from these headers; if it does not
  match the running `scsynth`, the server silently refuses to load the plugin.
- CMake ≥ 3.12 and a C++17 compiler.
- **TuningLib** quark (runtime, sclang only) for loading Scala `.scl` scales —
  see [Scales](#scales-scala-scl). Not needed to build the UGens.

Confirm `SC_PATH` matches your running SC:

```sh
git -C "$SC_PATH" describe --tags     # compare to Main.version in sclang
```

## Build / install / test

```sh
# configure (SC_PATH must match the SuperCollider you actually run)
cmake -B build \
  -DSC_PATH="/Users/jrubisch/Documents/_CODE/oss/supercollider" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_INSTALL_PREFIX="$HOME/Library/Application Support/SuperCollider/Extensions/habitat-ugens"

cmake --build build --config Release
cmake --install build          # copies <Name>.scx + <Name>.sc into the Extensions subfolder
```

Then in SuperCollider:

```supercollider
thisProcess.recompile;   // load the .sc classes (or Cmd+Shift+L)
s.reboot;                // plugins load only at server boot
```

Verify a unit loaded, e.g.:

```supercollider
Tomograph.asClass.notNil;   // -> true, and a clean boot with no "UGen not installed"
```

## Adding a unit

1. Create `src/<Name>.cpp` (the UGen) and `sc/<Name>.sc` (the class).
2. Uncomment / add `<Name>` in `HABITAT_UGENS` in `CMakeLists.txt`.
3. Add `examples/<name>-test.scd`.
4. Rebuild, reinstall, then `s.reboot`.

The `.cpp` input enum order and the `.sc` `multiNew` argument order must be
identical.

## Scales (Scala `.scl`)

Tomograph distributes its 16 band frequencies from a musical scale. Scales are
read from **Scala `.scl`** files, the de-facto standard tuning format.

### Where to get `.scl` files

- **Scala scale archive** (Huygens-Fokker Foundation) — ~5000 scales, the
  canonical source: <https://www.huygens-fokker.org/scala/downloads.html>
  (direct archive: <https://www.huygens-fokker.org/docs/scales.zip>).
- The `.scl` format spec: <https://www.huygens-fokker.org/scala/scl_format.html>

> **macOS/Linux unzip caveat.** The archive ships CR-LF line endings. Extract
> with `unzip -aa scales.zip` (autoconvert) or the files may fail to parse.

A handful of classic scales is bundled in [`examples/scales/`](examples/scales/)
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

### Loading scales: the TuningLib quark

Parsing is delegated to the **TuningLib** quark's `Scala` class (maintained
upstream) rather than a hand-rolled parser. Install it once, then recompile:

```supercollider
Quarks.install("TuningLib");
thisProcess.recompile;   // or Cmd+Shift+L
```

`Scala` extends `Tuning`; `Scala.open(path).cents` returns the scale's degree
cents with the unison (`0`) first and the octave excluded. Verified against
SuperCollider 3.14.1 (`Tuning.cents` -> `tuning * 100`).

### Copy-paste: drive Tomograph from a `.scl`

Edit `~scaleDir` to point at this repo's `examples/scales/` (absolute path),
then evaluate each block.

```supercollider
// 0) one-time install (skip if already installed)
Quarks.install("TuningLib"); thisProcess.recompile;

// 1) helpers — load scale cents, map to 16 band frequencies
(
~scaleDir = "/Users/jrubisch/Documents/_CODE/oss/habitat-ugens/examples/scales";

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

## License

GNU General Public License v3.0 (or later). See [`LICENSE`](LICENSE).

The Colmatage unit carries BBCut / WarpCut / Livecut lineage, originally
authored by **Nick Collins** (BBCut) and **Remy Muller** (Livecut), released
under the GNU GPL v2. The repository is kept GPL-compatible for this reason. If
upstream Livecut is "v2 only" rather than "v2 or later", the Colmatage sources
retain their GPLv2 notice. Confirm exact upstream terms before publishing.

## Credits

- **stolmine** — `er-301-habitat`, source of every unit ported here.
- **Nick Collins** — BBCut / WarpCut (Colmatage lineage).
- **Remy Muller** — Livecut (Colmatage lineage).
- Modeled on [`v7b1/mi-UGens`](https://github.com/v7b1/mi-UGens) and the
  sc3-plugins repository layout.
