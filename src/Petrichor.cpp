// Native SuperCollider UGen — faithful port of the ER-301 "MultitapDelay"
// (stolmine/er-301-habitat, spreadsheet package): a Rainmaker-inspired stereo
// multitap delay. 8 taps, each with a TPT state-variable filter
// (Off/LP/BP/HP/Notch), level, pan, and a 3-grain granular pitch shifter
// (which also does granular reverse). Tap delay times are derived from a
// distribution (MasterTime x TapCount x Skew x Stack x Grid) plus per-tap
// slow sinusoidal Drift. Feedback is a multi-tap weighted-sum network
// (feedback x 1/sqrt(activeTaps) x per-tap Q-compensation) through a tone
// filter and a soft limiter, computed per-sample inside the loop (exact, no
// block delay).
//
// This ports the scalar reference math (the `#else` branches of the upstream
// NEON code). Differences from the hardware unit:
//  - the delay buffer is float, not int16 (no clamp/quantize on store); sized
//    at Ctor from `maxDelayTime` and freed in the Dtor.
//  - the parameter-randomizing "Xform" feature and the edit-buffer UI are
//    dropped; Xform lives host-side in sclang (see examples/petrichor-test.scd).
//  - per-tap values are flat control inputs (arrays splatted from sclang);
//    tap times are derived, not exposed.
//  - fast_log2/exp2/powf/tanh and the fast drift-sine are replaced by std math.
#include "SC_PlugIn.h"
#include <cmath>
#include <cstdint>
#include <cstring>

static InterfaceTable* ft;

static const int kMaxTaps = 8;
static const int kGrainsPerTap = 3;

enum TapFilterType {
    TAP_FILTER_OFF = 0,
    TAP_FILTER_LP,
    TAP_FILTER_BP,
    TAP_FILTER_HP,
    TAP_FILTER_NOTCH,
    TAP_FILTER_COUNT
};

enum {
    kIn = 0,
    kMasterTime, kFeedback, kFeedbackTone, kMix, kTapCount, kVOctPitch,
    kSkew, kGrainSize, kDrift, kReverse, kStack, kGrid, kInLevel, kOutLevel,
    kTanh, kMono, kMaxDelayTime,
    kTapLevel0,                              // kTapLevel0    .. +8
    kTapPan0      = kTapLevel0    + kMaxTaps, // kTapPan0      .. +8
    kTapPitch0    = kTapPan0      + kMaxTaps, // kTapPitch0    .. +8
    kFilterCutoff0 = kTapPitch0   + kMaxTaps, // kFilterCutoff0 .. +8
    kFilterQ0     = kFilterCutoff0 + kMaxTaps, // kFilterQ0     .. +8
    kFilterType0  = kFilterQ0     + kMaxTaps, // kFilterType0  .. +8
    kNumInputs    = kFilterType0  + kMaxTaps
};

struct Grain {
    float phase;       // 0-1 through the Hann envelope
    float readPos;     // fractional sample position in the buffer
    float phaseDelta;
    float speed;
    bool  active;
    bool  reverse;
};

struct Petrichor : public Unit {
    // delay buffer (internal RTAlloc, freed in Dtor)
    float* buf;
    int    maxDelay;     // length in samples
    int    writeIndex;

    // per-tap user params (refreshed each block from control inputs)
    float tapLevel[kMaxTaps];
    float tapPan[kMaxTaps];
    float tapPitch[kMaxTaps];      // semitones
    float filterCutoff[kMaxTaps];  // Hz
    float filterQ[kMaxTaps];       // 0-1
    int   filterType[kMaxTaps];
    float cachedBandQ[kMaxTaps];   // for feedback Q-compensation

    // per-tap TPT SVF coefficients + state
    float svfG[kMaxTaps];
    float svfR[kMaxTaps];
    float svfH[kMaxTaps];
    float svfS1[kMaxTaps];
    float svfS2[kMaxTaps];

    // block-rate-baked per-tap mode gains + masks
    float lpGain[kMaxTaps];
    float bpGain[kMaxTaps];
    float hpGain[kMaxTaps];
    float useFilterMask[kMaxTaps];
    float effectiveTapLevel[kMaxTaps];

    // equal-power pan coefficients
    float panL[kMaxTaps];
    float panR[kMaxTaps];

    // baked per-tap feedback weight (feedback x 1/sqrt(N) x Q-comp x active mask)
    float fbWeightSum[kMaxTaps];

    // tap distribution
    float delaySamples[kMaxTaps];
    float smoothedDelaySamples[kMaxTaps];
    float driftPhase[kMaxTaps];
    float tapEnergy[kMaxTaps];     // viz energy follower (parity; feeds nothing)

    // granular pitch shift state
    Grain grains[kMaxTaps][kGrainsPerTap];
    int   grainSpawnCounter[kMaxTaps];

    // feedback path one-pole states
    float fbFilterState;
    float fbHpState;

    uint32_t rngSeed;
};


static inline float lcgFloat(uint32_t& seed) {
    seed = seed * 1103515245u + 12345u;
    return (float)((seed >> 8) & 0x7FFF) / 32767.0f;
}


static void Petrichor_next(Petrichor* unit, int inNumSamples);

void Petrichor_Ctor(Petrichor* unit) {
    const float sr = SAMPLERATE;

    float maxDelayTime = sc_max(IN0(kMaxDelayTime), 0.001f);
    int maxDelay = (int)(maxDelayTime * sr);
    if (maxDelay < 1) maxDelay = 1;

    unit->buf = (float*)RTAlloc(unit->mWorld, maxDelay * sizeof(float));
    if (!unit->buf) {
        // allocation failed: degrade to silence, never deref null
        unit->maxDelay = 0;
        ClearUnitOutputs(unit, 1);
        SETCALC(ClearUnitOutputs);
        return;
    }
    std::memset(unit->buf, 0, maxDelay * sizeof(float));
    unit->maxDelay = maxDelay;
    unit->writeIndex = 0;

    for (int i = 0; i < kMaxTaps; ++i) {
        unit->tapLevel[i] = 1.0f;
        unit->tapPan[i] = 0.0f;
        unit->tapPitch[i] = 0.0f;
        unit->filterCutoff[i] = 10000.0f;
        unit->filterQ[i] = 0.0f;
        unit->filterType[i] = TAP_FILTER_OFF;
        unit->cachedBandQ[i] = 0.5f;
        unit->svfG[i] = 0.0f;
        unit->svfR[i] = 1.0f;
        unit->svfH[i] = 1.0f;
        unit->svfS1[i] = 0.0f;
        unit->svfS2[i] = 0.0f;
        unit->lpGain[i] = 0.0f;
        unit->bpGain[i] = 0.0f;
        unit->hpGain[i] = 0.0f;
        unit->useFilterMask[i] = 0.0f;
        unit->effectiveTapLevel[i] = 0.0f;
        unit->panL[i] = 0.707f;
        unit->panR[i] = 0.707f;
        unit->fbWeightSum[i] = 0.0f;
        unit->delaySamples[i] = 0.0f;
        unit->smoothedDelaySamples[i] = 0.0f;
        unit->driftPhase[i] = (float)i * 1.618f;  // golden-ratio spread
        unit->tapEnergy[i] = 0.0f;
        unit->grainSpawnCounter[i] = 0;
        for (int g = 0; g < kGrainsPerTap; ++g) {
            unit->grains[i][g].active = false;
            unit->grains[i][g].reverse = false;
            unit->grains[i][g].phase = 0.0f;
            unit->grains[i][g].readPos = 0.0f;
            unit->grains[i][g].phaseDelta = 0.0f;
            unit->grains[i][g].speed = 1.0f;
        }
    }

    unit->fbFilterState = 0.0f;
    unit->fbHpState = 0.0f;
    unit->rngSeed = 42u;

    SETCALC(Petrichor_next);
    Petrichor_next(unit, 1);
}

void Petrichor_Dtor(Petrichor* unit) {
    if (unit->buf) RTFree(unit->mWorld, unit->buf);
}

void Petrichor_next(Petrichor* unit, int inNumSamples) {
    const float* in = IN(kIn);
    float* out  = OUT(0);
    float* outR = OUT(1);

    const int maxDelay = unit->maxDelay;
    float* buf = unit->buf;
    if (!buf || maxDelay == 0) {
        // no buffer — passthrough
        for (int i = 0; i < inNumSamples; ++i) { out[i] = in[i]; outR[i] = in[i]; }
        return;
    }

    const float sr = SAMPLERATE;
    const float maxBufferTime = (float)maxDelay / sr;

    int tapCount = (int)sc_clip(IN0(kTapCount) + 0.5f, 1.f, (float)kMaxTaps);

    float masterTimeRaw = sc_clip(IN0(kMasterTime), 0.001f, maxBufferTime);
    float feedback   = sc_clip(IN0(kFeedback), 0.f, 0.95f);
    float mix        = sc_clip(IN0(kMix), 0.f, 1.f);
    float inputLevel = sc_clip(IN0(kInLevel), 0.f, 4.f);
    float outputLevel = sc_clip(IN0(kOutLevel), 0.f, 4.f);
    float tanhAmt    = sc_clip(IN0(kTanh), 0.f, 1.f);
    float skew       = IN0(kSkew);
    float drift      = sc_clip(IN0(kDrift), 0.f, 1.f);
    float reverse    = sc_clip(IN0(kReverse), 0.f, 1.f);
    float grainSizeParam = sc_clip(IN0(kGrainSize), 0.f, 1.f);
    bool  mono       = (IN0(kMono) > 0.5f);

    int stackExp = (int)sc_clip(IN0(kStack) + 0.5f, 0.f, 3.f);
    int stack = 1 << stackExp;          // 1, 2, 4, 8
    if (stack > tapCount) stack = tapCount;
    int gridExp = (int)sc_clip(IN0(kGrid) + 0.5f, 0.f, 4.f);
    int grid = 1 << gridExp;            // 1, 2, 4, 8, 16

    // V/Oct master pitch (0.1 per octave in the original 10Vpp range)
    float voctPitch = IN0(kVOctPitch) * 10.0f;

    // refresh per-tap user params from control inputs
    for (int t = 0; t < kMaxTaps; ++t) {
        unit->tapLevel[t]     = sc_clip(IN0(kTapLevel0 + t), 0.f, 1.f);
        unit->tapPan[t]       = sc_clip(IN0(kTapPan0 + t), -1.f, 1.f);
        unit->tapPitch[t]     = sc_clip(IN0(kTapPitch0 + t), -24.f, 24.f);
        unit->filterCutoff[t] = sc_clip(IN0(kFilterCutoff0 + t), 20.f, 10000.f);
        unit->filterQ[t]      = sc_clip(IN0(kFilterQ0 + t), 0.f, 1.f);
        unit->filterType[t]   = (int)sc_clip(IN0(kFilterType0 + t) + 0.5f,
                                             0.f, (float)(TAP_FILTER_COUNT - 1));
    }

    // --- grain timing setup ---
    // 0 -> 5ms, 1 -> 300ms grain duration; 50% overlap (Hann COLA sweet spot).
    int grainDuration = (int)(sr * (0.005f + grainSizeParam * 0.295f));
    if (grainDuration < 64) grainDuration = 64;
    int grainPeriod = (int)(grainDuration * 0.5f);
    if (grainPeriod < 32) grainPeriod = 32;
    float grainPhaseDelta = 1.0f / (float)grainDuration;

    // --- tap distribution (recomputed every block; per-sample drift below) ---
    int numGroups = (tapCount + stack - 1) / stack;
    if (numGroups < 1) numGroups = 1;
    float totalSpan = masterTimeRaw * (float)numGroups / (float)grid;
    float maxSpanSec = (float)maxDelay / sr;
    if (totalSpan > maxSpanSec) totalSpan = maxSpanSec;
    float masterTime = totalSpan * (float)grid / (float)numGroups;

    float skewExp = std::exp2(skew);
    for (int t = 0; t < tapCount; ++t) {
        int groupIndex = t / stack;
        float pos = std::pow((float)(groupIndex + 1) / (float)grid, skewExp);
        if (drift > 0.001f) {
            unit->driftPhase[t] += 0.0003f + 0.0001f * (float)t;
            pos += std::sin(unit->driftPhase[t]) * drift * 0.1f;
        }
        if (pos < 0.001f) pos = 0.001f;
        float ds = pos * masterTime * sr;
        if (ds > (float)(maxDelay - 1)) ds = (float)(maxDelay - 1);
        unit->delaySamples[t] = ds;

        float p = (unit->tapPan[t] + 1.0f) * 0.5f;  // 0=left, 1=right
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        unit->panL[t] = std::sqrt(1.0f - p);
        unit->panR[t] = std::sqrt(p);
    }

    // --- per-tap SVF coefficients + mode bake ---
    for (int t = 0; t < tapCount; ++t) {
        float freq = unit->filterCutoff[t] / sr;
        freq = sc_clip(freq, 0.0001f, 0.49f);
        float q = 1.0f + 29.0f * unit->filterQ[t] * unit->filterQ[t];  // 1..30
        float bandQ = q * (0.5f + freq * 2.0f);
        if (bandQ < 0.5f) bandQ = 0.5f;
        unit->cachedBandQ[t] = bandQ;

        float g = freq * (1.0f + freq * freq * 0.333333333f);  // fast tan
        float r = 1.0f / bandQ;
        unit->svfG[t] = g;
        unit->svfR[t] = r;
        unit->svfH[t] = 1.0f / (1.0f + r * g + g * g);

        int ftype = unit->filterType[t];
        unit->lpGain[t] = (ftype == TAP_FILTER_LP || ftype == TAP_FILTER_NOTCH) ? 1.0f : 0.0f;
        unit->bpGain[t] = (ftype == TAP_FILTER_BP) ? 1.0f : 0.0f;
        unit->hpGain[t] = (ftype == TAP_FILTER_HP || ftype == TAP_FILTER_NOTCH) ? 1.0f : 0.0f;
        unit->useFilterMask[t] = (ftype != TAP_FILTER_OFF) ? 1.0f : 0.0f;
        unit->effectiveTapLevel[t] = unit->tapLevel[t];
    }
    // pad unused lanes to neutral (g=0 freezes state, zero contribution)
    for (int t = tapCount; t < kMaxTaps; ++t) {
        unit->svfG[t] = 0.0f;
        unit->svfR[t] = 1.0f;
        unit->svfH[t] = 1.0f;
        unit->lpGain[t] = 0.0f;
        unit->bpGain[t] = 0.0f;
        unit->hpGain[t] = 0.0f;
        unit->useFilterMask[t] = 0.0f;
        unit->effectiveTapLevel[t] = 0.0f;
        unit->panL[t] = 0.0f;
        unit->panR[t] = 0.0f;
    }

    // --- feedback weights (multi-tap weighted-sum recirculation) ---
    int activeTapCount = 0;
    for (int t = 0; t < tapCount; ++t)
        if (unit->tapLevel[t] >= 0.001f) activeTapCount++;
    float fbNormFactor = (activeTapCount > 0)
        ? (feedback / std::sqrt((float)activeTapCount)) : 0.0f;
    for (int t = 0; t < tapCount; ++t) {
        if (unit->tapLevel[t] < 0.001f) {
            unit->fbWeightSum[t] = 0.0f;
        } else {
            float qComp = (unit->filterType[t] != TAP_FILTER_OFF)
                ? (1.0f / (1.0f + unit->cachedBandQ[t] * 0.1f)) : 1.0f;
            unit->fbWeightSum[t] = fbNormFactor * qComp;
        }
    }
    for (int t = tapCount; t < kMaxTaps; ++t)
        unit->fbWeightSum[t] = 0.0f;

    // --- per-tap grain speeds / need-grains flags ---
    float tapSpeeds[kMaxTaps];
    bool  tapNeedGrains[kMaxTaps];
    for (int t = 0; t < tapCount; ++t) {
        float pitch = voctPitch + unit->tapPitch[t] / 12.0f;
        tapNeedGrains[t] = (std::fabs(pitch) > 0.001f) || (reverse > 0.001f);
        tapSpeeds[t] = tapNeedGrains[t] ? std::exp2(pitch) : 1.0f;
    }

    // --- feedback tone: -1 dark (LP) .. 0 flat .. +1 bright (HP) ---
    float tone = sc_clip(IN0(kFeedbackTone), -1.f, 1.f);
    float fbFilterCoeff = (tone <= 0.0f) ? (0.05f + 0.95f * (1.0f + tone)) : 1.0f;

    const float dsSmoothCoeff = 0.001f;  // ~20ms @ 48kHz

    for (int i = 0; i < inNumSamples; ++i) {
        float x = in[i] * inputLevel;

        if (unit->writeIndex >= maxDelay) unit->writeIndex = 0;
        const int writeIndex = unit->writeIndex;
        buf[writeIndex] = x;

        float wetL = 0.0f;
        float wetR = 0.0f;

        // smooth the delaySamples used for grain spawn start (tames CV/drift
        // jitter so successive grain starts stay phase-coherent)
        for (int t = 0; t < tapCount; ++t)
            unit->smoothedDelaySamples[t] +=
                (unit->delaySamples[t] - unit->smoothedDelaySamples[t]) * dsSmoothCoeff;

        // ---- Pass B: per-tap grain machinery + buffer reads ----
        float tapOutScratch[kMaxTaps];
        for (int t = 0; t < tapCount; ++t) {
            if (unit->tapLevel[t] < 0.001f) { tapOutScratch[t] = 0.0f; continue; }

            float delaySamples = unit->delaySamples[t];
            float tapOut = 0.0f;

            if (tapNeedGrains[t]) {
                float tapSpeed = tapSpeeds[t];
                unit->grainSpawnCounter[t]--;
                if (unit->grainSpawnCounter[t] <= 0) {
                    for (int g = 0; g < kGrainsPerTap; ++g) {
                        if (!unit->grains[t][g].active) {
                            Grain& gr = unit->grains[t][g];
                            gr.active = true;
                            gr.phase = 0.0f;
                            gr.phaseDelta = grainPhaseDelta;
                            gr.speed = tapSpeed;
                            gr.reverse = (reverse > 0.001f) && (lcgFloat(unit->rngSeed) < reverse);
                            float startPos = (float)writeIndex - unit->smoothedDelaySamples[t];
                            while (startPos < 0.0f) startPos += (float)maxDelay;
                            gr.readPos = startPos;
                            break;
                        }
                    }
                    unit->grainSpawnCounter[t] = grainPeriod;
                }
                for (int g = 0; g < kGrainsPerTap; ++g) {
                    Grain& gr = unit->grains[t][g];
                    if (!gr.active) continue;
                    float env = 0.5f * (1.0f - std::cos(6.28318530718f * gr.phase));
                    int idx = (int)gr.readPos;
                    if (idx >= maxDelay) idx -= maxDelay;
                    float frac = gr.readPos - (float)idx;
                    int idx2 = idx + 1;
                    if (idx2 >= maxDelay) idx2 = 0;
                    float s0 = buf[idx];
                    float s1 = buf[idx2];
                    tapOut += (s0 + (s1 - s0) * frac) * env;
                    gr.readPos += gr.reverse ? -gr.speed : gr.speed;
                    if (gr.readPos < 0.0f) gr.readPos += (float)maxDelay;
                    if (gr.readPos >= (float)maxDelay) gr.readPos -= (float)maxDelay;
                    gr.phase += gr.phaseDelta;
                    if (gr.phase >= 1.0f) gr.active = false;
                }
            } else {
                // direct linear-interp read (no pitch shift)
                float readPos = (float)writeIndex - delaySamples;
                if (readPos < 0.0f) readPos += (float)maxDelay;
                int idx = (int)readPos;
                if (idx >= maxDelay) idx -= maxDelay;
                float frac = readPos - (float)idx;
                int idx2 = idx + 1;
                if (idx2 >= maxDelay) idx2 = 0;
                tapOut = buf[idx] + (buf[idx2] - buf[idx]) * frac;
            }

            tapOutScratch[t] = tapOut;
        }
        for (int t = tapCount; t < kMaxTaps; ++t) tapOutScratch[t] = 0.0f;

        // ---- Pass C: per-tap TPT SVF + mode dispatch + level x pan ----
        for (int t = 0; t < kMaxTaps; ++t) {
            float tapIn = tapOutScratch[t];
            float g = unit->svfG[t], r = unit->svfR[t], h = unit->svfH[t];
            float hp = (tapIn - r * unit->svfS1[t] - g * unit->svfS1[t] - unit->svfS2[t]) * h;
            float bp = unit->svfS1[t] + g * hp;
            unit->svfS1[t] = bp + g * hp;
            float lp = unit->svfS2[t] + g * bp;
            unit->svfS2[t] = lp + g * bp;
            float filtered = lp * unit->lpGain[t] + bp * unit->bpGain[t] + hp * unit->hpGain[t];
            float fin = tapIn + unit->useFilterMask[t] * (filtered - tapIn);
            float filteredOut = fin * unit->effectiveTapLevel[t];
            tapOutScratch[t] = filteredOut;
            wetL += filteredOut * unit->panL[t];
            wetR += filteredOut * unit->panR[t];
        }

        // energy follower (viz parity; feeds nothing here)
        for (int t = 0; t < tapCount; ++t) {
            float e = tapOutScratch[t] * tapOutScratch[t];
            unit->tapEnergy[t] += (e - unit->tapEnergy[t]) * 0.001f;
        }

        // ---- multi-tap weighted-sum feedback ----
        float fb = 0.0f;
        for (int t = 0; t < kMaxTaps; ++t)
            fb += tapOutScratch[t] * unit->fbWeightSum[t];

        // tone-controlled damping + soft limiter
        unit->fbFilterState += (fb - unit->fbFilterState) * fbFilterCoeff;
        float fbOut = unit->fbFilterState;
        if (tone > 0.0f) {
            float lpCoeff = 1.0f - tone * 0.95f;  // 1.0 (flat) .. 0.05 (heavy HP)
            unit->fbHpState += (fb - unit->fbHpState) * lpCoeff;
            fbOut = fb - unit->fbHpState * tone;
        }
        float fbInjection = (std::fabs(fbOut) > 1.5f) ? std::tanh(fbOut) : fbOut;
        buf[writeIndex] += zapgremlins(fbInjection);

        unit->writeIndex = (writeIndex + 1) % maxDelay;

        // mix
        float mixedL = x * (1.0f - mix) + wetL * mix;
        float mixedR = x * (1.0f - mix) + wetR * mix;

        // user saturation
        if (tanhAmt > 0.001f) {
            float drive = 1.0f + tanhAmt * 3.0f;
            mixedL = mixedL * (1.0f - tanhAmt) + std::tanh(mixedL * drive) * tanhAmt;
            mixedR = mixedR * (1.0f - tanhAmt) + std::tanh(mixedR * drive) * tanhAmt;
        }

        // output limiter (always on)
        if (mono) {
            float m = std::tanh((mixedL + mixedR) * 0.5f * outputLevel);
            out[i] = m;
            outR[i] = m;
        } else {
            out[i] = std::tanh(mixedL * outputLevel);
            outR[i] = std::tanh(mixedR * outputLevel);
        }
    }

    // keep recirculating feedback states denormal-free
    unit->fbFilterState = zapgremlins(unit->fbFilterState);
    unit->fbHpState = zapgremlins(unit->fbHpState);
}

PluginLoad(Petrichor) {
    ft = inTable;
    DefineDtorUnit(Petrichor);
}
