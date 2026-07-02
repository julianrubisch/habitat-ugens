// Codescan pair — BletchleyPark (CodescanOsc) + StationX (CodescanFilter).
// Both read an arbitrary binary file as signed 8-bit samples ((signed char)/127)
// and turn it into sound material. Port of stolmine er-301-habitat biome/
// CodescanOsc + biome/CodescanFilter.
//
// splat order:
//   BletchleyPark.ar(voct, sync, buf, scan, fundamental, regionStart)   -> 1 out
//   StationX.ar(in, buf, scan, taps, mix)                               -> 1 out
s.boot;

// ---- shared loader: file bytes -> Buffer of signed-8-bit floats ~[-1,1] ----
// The UGens read the buffer floats directly (matching mData[i]=(signed char)b/127),
// so the /127 interpretation happens once, here. Int8Array is signed 8-bit;
// File:read(rawArray) fills a pre-sized raw array (same idiom SC uses for
// readAllString/readAllSignal). For very large files, read a slice instead.
(
~loadCodescan = { |path|
    var f = File(path.standardizePath, "rb"), bytes, data;
    if (f.isOpen.not) { ("Codescan: cannot open " ++ path).warn; nil } {
        bytes = Int8Array.newClear(f.length);
        f.read(bytes);
        f.close;
        // NB: Int8Array.collect returns an Int8Array and truncates the /127.0
        // floats back to 0 -> a silent buffer. Convert to Array first.
        data = bytes.as(Array).collect { |b| b / 127.0 };   // -128..127 -> ~ -1..1
        Buffer.loadCollection(s, data);
    }
};
)

~code = ~loadCodescan.("/bin/ls");          // any binary file works as material

// 1) BletchleyPark — file-bytes wavetable osc. MouseX sweeps the 256-sample
//    window through the 4096-sample region; Impulse into `sync` resets phase.
//    regionStart is randomized per voice (replaces the source's per-instance
//    random region). Move the mouse left/right to hear the scan.
(
~b = {
    var rs = rrand(0, (~code.numFrames - 4096 - 256).max(0));
    BletchleyPark.ar(0, Impulse.ar(2), ~code, MouseX.kr(0, 1), 110, rs) * 0.3 ! 2;
}.play;
)
~b.free;

// 1b) V/Oct tracking — 0.1 V/oct in a 10 Vpp range, so pitch = fundamental * 2^(voct*10).
//     A slow ramp over one "volt" (0..0.1) sweeps one octave.
(
~b = {
    var voct = LFSaw.kr(0.2).range(0, 0.3);   // 3 octaves
    var rs = rrand(0, (~code.numFrames - 4096 - 256).max(0));
    BletchleyPark.ar(voct, Impulse.ar(0), ~code, 0.5, 110, rs) * 0.3 ! 2;
}.play;
)
~b.free;

// 2) StationX — file-bytes FIR filter on noise. MouseX picks the kernel
//    position; taps sets its length (4..64); mix the dry/wet.
(
~s = { StationX.ar(PinkNoise.ar(0.4), ~code, MouseX.kr(0, 1), 32, 0.8) ! 2 }.play;
)
~s.free;

// 2b) taps sweep — the kernel lengthens/shortens, changing the filter character.
(
~s = {
    var taps = MouseY.kr(4, 64).round;
    StationX.ar(Saw.ar(80, 0.4), ~code, MouseX.kr(0, 1), taps, 1.0) ! 2;
}.play;
)
~s.free;

~code.free;
