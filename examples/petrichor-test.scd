// Petrichor — Rainmaker-inspired stereo multitap granular delay (port of
// stolmine ER-301 MultitapDelay). 8 taps, each with a TPT SVF, level, pan, and
// a 3-grain granular pitch shifter (also does granular reverse). Tap times are
// derived from masterTime x tapCount x skew x stack x grid, plus per-tap drift.
//
// arg order: in, masterTime, feedback, feedbackTone, mix, tapCount, voctPitch,
//   skew, grainSize, drift, reverse, stack, grid, inLevel, outLevel, tanh, mono,
//   maxDelayTime, then the six per-tap arrays (8 each): tapLevel, tapPan,
//   tapPitch, filterCutoff, filterQ, filterType.  -> 2 outputs [L, R].
//
// Large float buffers need server memory (~4 MB per second). Set memSize before
// booting if you use a big maxDelayTime:
// s.options.memSize = 8192 * 64;   // ~32 MB of wire/real-time pool
s.boot;

// 1) LIVE INPUT — granular multitap with pitched / reversed taps and per-tap
//    filtering. This is the brief's reference patch.
(
~p = {
    var src = SoundIn.ar(0);
    Petrichor.ar(src,
        masterTime: 0.4, feedback: 0.55, feedbackTone: -0.3, mix: 0.6,
        tapCount: 6, grainSize: 0.5, drift: 0.3, reverse: 0.2,
        stack: 1, grid: 2, maxDelayTime: 8,
        tapPitch: #[0, 7, 0, 12, -5, 0, 0, 0],
        filterType: #[1, 0, 2, 0, 1, 0, 0, 0],
        filterCutoff: #[2000, 8000, 1200, 8000, 600, 8000, 8000, 8000]);
}.play;
)
~p.free;

// 2) RHYTHMIC TAPS from an impulse — grid / stack / skew change the spacing,
//    feedback grows the tail, pan spreads the taps across the field.
(
~p = {
    var src = Decay2.ar(Impulse.ar(1), 0.001, 0.1) * PinkNoise.ar(0.6);
    Petrichor.ar(src,
        masterTime: 0.5, feedback: 0.7, mix: 0.7,
        tapCount: 8, skew: 0.5, stack: 0, grid: 3, maxDelayTime: 8,
        tapPan: #[-1, -0.6, -0.2, 0.2, 0.6, 1, 0, 0]);
}.play;
)
~p.free;


// ---------------------------------------------------------------------------
// 3) HOST-SIDE "XFORM" RANDOMIZER
//
// The hardware unit had an "Xform" feature that randomized its parameters on a
// gate. That is a control-domain feature, not audio DSP, so it is not part of
// the UGen — it lives here in sclang. This routine randomizes Petrichor's macro
// arguments on a trigger, mirroring the original depth/spread/center math:
//
//   center = spread*mid + (1-spread)*cur      // blend toward range centre
//   v      = clip(center + rand2(depth*range/2), lo, hi)
//
// We track the current macro values in a dict (since `.set` is one-way) and
// drive a Synth whose macros are named controls.
// ---------------------------------------------------------------------------
(
SynthDef(\petriXform, { |masterTime = 0.5, feedback = 0.3, feedbackTone = 0,
    mix = 0.6, skew = 0, grainSize = 0.5, drift = 0, reverse = 0,
    stack = 0, grid = 0, tapCount = 4|
    var src = SoundIn.ar(0);
    Out.ar(0, Petrichor.ar(src,
        masterTime: masterTime, feedback: feedback, feedbackTone: feedbackTone,
        mix: mix, tapCount: tapCount, skew: skew, grainSize: grainSize,
        drift: drift, reverse: reverse, stack: stack, grid: grid,
        maxDelayTime: 8,
        tapPitch: #[0, 7, 0, 12, -5, 0, 0, 0],
        filterType: #[1, 0, 2, 0, 1, 0, 0, 0],
        filterCutoff: #[2000, 8000, 1200, 8000, 600, 8000, 8000, 8000]));
}).add;
)

(
~x = Synth(\petriXform);

// macro ranges (name -> [lo, hi, isInteger])
~ranges = (
    masterTime:   [0.01, 4.0, false],
    feedback:     [0.0, 0.95, false],
    feedbackTone: [-1.0, 1.0, false],
    skew:         [-2.0, 2.0, false],
    grainSize:    [0.0, 1.0, false],
    drift:        [0.0, 1.0, false],
    reverse:      [0.0, 1.0, false],
    stack:        [0.0, 3.0, true],
    grid:         [0.0, 4.0, true],
    tapCount:     [1.0, 8.0, true]
);

// current values (seeded from the SynthDef defaults)
~cur = (
    masterTime: 0.5, feedback: 0.3, feedbackTone: 0, skew: 0,
    grainSize: 0.5, drift: 0, reverse: 0, stack: 0, grid: 0, tapCount: 4
);

// one randomization pass. depth 0..1 = how far to jump; spread 0..1 = pull
// toward the range centre (0) vs. stay near the current value (1).
~xform = { |depth = 0.5, spread = 0.5|
    var args = [];
    ~ranges.keysValuesDo { |key, r|
        var lo = r[0], hi = r[1], isInt = r[2];
        var range = hi - lo;
        var mid = (lo + hi) * 0.5;
        var cur = ~cur[key];
        var center = (spread * mid) + ((1 - spread) * cur);
        var v = (center + (depth * range * 0.5).rand2).clip(lo, hi);
        if(isInt) { v = v.round.asInteger };
        ~cur[key] = v;
        args = args ++ [key, v];
    };
    ~x.set(*args);
    ("xform ->" + args).postln;
};
)

// fire it manually:
~xform.(depth: 0.6, spread: 0.4);
~xform.(depth: 1.0, spread: 0.0);   // big jump toward range centres
~xform.(depth: 0.2, spread: 0.8);   // gentle nudge around current values

// or run it on a clock (a "gate" every 2 seconds):
(
~xformRoutine = Routine({
    loop {
        ~xform.(depth: 0.5, spread: 0.4);
        2.0.wait;
    }
}).play(AppClock);
)
~xformRoutine.stop;

~x.free;
