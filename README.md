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
| Petrichor | MultitapDelay | Rainmaker-inspired stereo multitap granular delay |

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
  -DSC_PATH="/path/to/supercollider" \
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

Tomograph distributes its band frequencies from a musical scale loaded from a
Scala `.scl` file. See **[docs/scales.md](docs/scales.md)** for where to source
scales, the TuningLib quark setup, a ready-to-use SynthDef, and copy-paste
SuperCollider snippets. A few example scales are bundled in
[`examples/scales/`](examples/scales/).

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
