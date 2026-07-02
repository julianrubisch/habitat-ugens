// Native SuperCollider UGen — faithful port of the ER-301 "Colmatage"
// (stolmine/er-301-habitat, spreadsheet package): a clock-driven breakbeat
// cutter. Algorithm lineage: Nick Collins' BBCut / WarpCut via Livecut
// (Remy Muller), GPLv2.
//
// Differences from the hardware unit:
//  - the record buffer is float, not int16 (no clamp/quantize on store).
//  - the buffer is configurable: internal RTAlloc (default, freed in the
//    Dtor) or an external mono Buffer via `bufnum`.
//  - the ER-301 viz ring and UI accessors are omitted.
//
// The clock period is measured in samples from the `clock` trigger input
// exactly as upstream; a `reset` trigger restarts the phrase/block.
#include "SC_PlugIn.h"
#include <cmath>
#include <cstdint>
#include <cstring>

static InterfaceTable* ft;

static const int kCrossfadeSamples = 64;
static const int kMaxCuts = 128;
static const int kMaxBlockUnits = 128;
static const int kValidSubdivs[] = {6, 8, 12, 16, 24, 32};
static const int kNumSubdivs = 6;

enum {
    kIn = 0, kClock, kReset,
    kDensity, kBlockSize, kBlockMax, kRepeatCount, kRitardBias, kBlend, kAccel,
    kSubdiv, kPhraseMin, kPhraseMax,
    kDutyCycle, kAmpMin, kAmpMax, kFade,
    kMix, kInLevel, kOutLevel, kTanh,
    kBufnum, kMaxdur,
    kNumInputs
};

struct CutInfo { int size; int length; float amp; };

struct Colmatage : public Unit {
    // record buffer (internal RTAlloc when ownBuf, else external SndBuf)
    float* buf;
    int    bufFrames;
    bool   ownBuf;
    float  m_fbufnum;          // cached external bufnum
    SndBuf* m_buf;             // cached external SndBuf
    int    writePos;

    // phrase / block / cut scheduler state
    int phraseUnits, unitsDone, unitsInBlock, unitsInsideBlock;
    CutInfo cuts[kMaxCuts];
    int numCuts, currentCut, readIndex, sliceOrigin;
    int samplesPerUnit, unitSampleCounter;
    float prevOutput;
    int crossfadeCounter;

    // rng + clock/reset edge detection
    uint32_t rngState;
    bool clockWasHigh, resetWasHigh;
    int  clockPeriodSamples, samplesSinceLastClock;

    // per-block param snapshot (block-stable, read by the choose_* helpers)
    float pDensity, pBlockBias, pRitard, pBlend, pAccel, pDutyMag, pAmpMin, pAmpMax;
    int   pBlockMaxBeats, pRepeat, pSubdiv, pPhraseMin, pPhraseMax;
};

// ---- faithful primitives (transcribed from upstream) ---------------------

static inline float irand_float(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return (float)(state >> 1) / (float)0x7FFFFFFF;
}
static inline float irand_range(uint32_t& state, float lo, float hi) {
    return lo + (hi - lo) * irand_float(state);
}
static inline int irand_int(uint32_t& state, int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (int)(irand_float(state) * (float)(hi - lo + 1));
}

static int snap_subdiv(float raw) {
    int v = (int)(raw + 0.5f);
    int best = kValidSubdivs[0];
    int bestDist = std::abs(v - best);
    for (int i = 1; i < kNumSubdivs; ++i) {
        int d = std::abs(v - kValidSubdivs[i]);
        if (d < bestDist) { best = kValidSubdivs[i]; bestDist = d; }
    }
    return best;
}

static inline float expenv(float i, float fade, float size) {
    if (fade < 1.0f) fade = 1.0f;
    return (1.0f - std::exp(-5.0f * i / fade)) * (1.0f - std::exp(5.0f * (i - size) / fade));
}

static inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static int choose_weighted_block_size(uint32_t& rng, float bias, int blockMax) {
    if (blockMax <= 1) return 1;
    float invMax = 1.0f / (float)(blockMax - 1);
    float sum = 0.0f;
    for (int k = 0; k < blockMax; ++k)
        sum += std::exp(bias * 4.0f * (float)k * invMax);
    float r = irand_float(rng) * sum;
    float cumul = 0.0f;
    for (int k = 0; k < blockMax; ++k) {
        cumul += std::exp(bias * 4.0f * (float)k * invMax);
        if (r < cumul) return k + 1;
    }
    return blockMax;
}

// ---- scheduler (operate on the Unit's snapshot state) --------------------

static void choose_phrase_length(Colmatage* u) {
    int pMin = sc_max(1, u->pPhraseMin);
    int pMax = sc_max(pMin, u->pPhraseMax);
    int bars = irand_int(u->rngState, pMin, pMax);
    u->phraseUnits = bars * u->pSubdiv;
    u->unitsDone = 0;
}

static void choose_block(Colmatage* u, int unitsLeft) {
    const int subdiv = u->pSubdiv;
    const double spu = (double)u->samplesPerUnit;
    const float density = u->pDensity;
    const float blockSizeBias = u->pBlockBias;
    int blockMax = (int)sc_clip((float)(u->pBlockMaxBeats * subdiv / 4), 1.f, (float)kMaxBlockUnits);
    if (blockMax < 1) blockMax = 1;
    const int repeatCount = u->pRepeat;
    const float ritardBias = u->pRitard;
    const float blend = u->pBlend;
    const float accel = u->pAccel;
    const float dutyMag = u->pDutyMag;
    const float ampMin = u->pAmpMin;
    const float ampMax = u->pAmpMax;

    u->sliceOrigin = u->writePos;

    u->unitsInBlock = choose_weighted_block_size(u->rngState, blockSizeBias, blockMax);
    if (u->unitsInBlock > unitsLeft) u->unitsInBlock = unitsLeft;

    float straightChance = 1.0f - density;

    if (irand_float(u->rngState) < straightChance) {
        u->numCuts = 1;
        int cutSamples = (int)(spu * (double)u->unitsInBlock);
        u->cuts[0].size = sc_max(1, cutSamples);
        u->cuts[0].length = sc_max(1, (int)((float)cutSamples * dutyMag));
        u->cuts[0].amp = irand_range(u->rngState, ampMin, ampMax);
    } else {
        float startAmp = irand_range(u->rngState, ampMin, ampMax);
        float endAmp = irand_range(u->rngState, ampMin, ampMax);

        if (irand_float(u->rngState) < (1.0f - blend)) {            // even cuts
            float temp = (float)u->unitsInBlock / (float)repeatCount;
            u->numCuts = sc_min(repeatCount, kMaxCuts);
            for (int i = 0; i < u->numCuts; ++i) {
                float phase = (float)i / (float)u->numCuts;
                int l = (int)(spu * temp + 0.5);
                u->cuts[i].size = sc_max(1, l);
                u->cuts[i].length = sc_max(1, (int)((float)l * dutyMag));
                u->cuts[i].amp = startAmp + (endAmp - startAmp) * phase;
            }
        } else {                                                    // accel / ritard
            float temp = (float)u->unitsInBlock * (1.0f - accel) /
                         (1.0f - std::pow(accel, (float)repeatCount));
            u->numCuts = sc_min(repeatCount, kMaxCuts);
            for (int i = 0; i < u->numCuts; ++i) {
                float phase = (float)i / (float)u->numCuts;
                int l = (int)(spu * temp * std::pow(accel, (float)i));
                u->cuts[i].size = sc_max(1, l);
                u->cuts[i].length = sc_max(1, (int)((float)l * dutyMag));
                u->cuts[i].amp = startAmp + (endAmp - startAmp) * phase;
            }
            if (irand_float(u->rngState) < ritardBias) {            // reverse run
                for (int i = 0; i < u->numCuts / 2; ++i) {
                    int j = u->numCuts - 1 - i;
                    CutInfo tmp = u->cuts[i];
                    u->cuts[i] = u->cuts[j];
                    u->cuts[j] = tmp;
                }
            }
        }
    }

    u->unitsInsideBlock = 0;
    u->currentCut = 0;
    u->readIndex = 0;
    u->crossfadeCounter = kCrossfadeSamples;
}

static void advance_unit(Colmatage* u) {
    if (u->phraseUnits <= 0 || u->unitsDone >= u->phraseUnits)
        choose_phrase_length(u);

    if (u->unitsInsideBlock >= u->unitsInBlock) {
        int unitsLeft = u->phraseUnits - u->unitsDone;
        if (unitsLeft <= 0) {
            choose_phrase_length(u);
            unitsLeft = u->phraseUnits;
        }
        choose_block(u, unitsLeft);
    }

    u->unitsInsideBlock++;
    u->unitsDone++;
}

// ---- buffer access (internal float* or external mono SndBuf) -------------

static inline void buf_write(Colmatage* u, int pos, float x) {
    if (u->buf && pos >= 0 && pos < u->bufFrames) u->buf[pos] = x;
}
static inline float buf_read(Colmatage* u, int pos) {
    if (u->buf && pos >= 0 && pos < u->bufFrames) return u->buf[pos];
    return 0.f;
}

// ---- UGen lifecycle ------------------------------------------------------

static void Colmatage_next(Colmatage* unit, int inNumSamples);

void Colmatage_Ctor(Colmatage* unit) {
    const float sr = SAMPLERATE;

    unit->writePos = 0;
    unit->phraseUnits = 0; unit->unitsDone = 0;
    unit->unitsInBlock = 0; unit->unitsInsideBlock = 0;
    unit->numCuts = 0; unit->currentCut = 0; unit->readIndex = 0; unit->sliceOrigin = 0;
    unit->samplesPerUnit = 12000; unit->unitSampleCounter = 0;
    unit->prevOutput = 0.f; unit->crossfadeCounter = 0;
    unit->rngState = 98765u;
    unit->clockWasHigh = false; unit->resetWasHigh = false;
    unit->clockPeriodSamples = 24000; unit->samplesSinceLastClock = 0;
    std::memset(unit->cuts, 0, sizeof(unit->cuts));

    unit->m_fbufnum = -1.f;
    unit->m_buf = nullptr;

    float fbufnum = IN0(kBufnum);
    if (fbufnum < 0.f) {
        float maxdur = sc_max(IN0(kMaxdur), 0.1f);
        int frames = (int)(maxdur * sr);
        if (frames < 1) frames = 1;
        unit->buf = (float*)RTAlloc(unit->mWorld, frames * sizeof(float));
        if (unit->buf) {
            std::memset(unit->buf, 0, frames * sizeof(float));
            unit->bufFrames = frames;
            unit->ownBuf = true;
        } else {
            // allocation failed: degrade to silence, never deref null
            unit->bufFrames = 0;
            unit->ownBuf = false;
            ClearUnitOutputs(unit, 1);
            SETCALC(ClearUnitOutputs);
            return;
        }
    } else {
        unit->buf = nullptr;        // resolved per-block from the SndBuf
        unit->bufFrames = 0;
        unit->ownBuf = false;
    }

    SETCALC(Colmatage_next);
    Colmatage_next(unit, 1);
}

void Colmatage_Dtor(Colmatage* unit) {
    if (unit->ownBuf && unit->buf) RTFree(unit->mWorld, unit->buf);
}

void Colmatage_next(Colmatage* unit, int inNumSamples) {
    const float* in  = IN(kIn);
    const float* clk = IN(kClock);
    const float* rst = IN(kReset);
    float* out = OUT(0);
    const float sr = SAMPLERATE;

    // clock/reset may be audio, control, or scalar. Only audio-rate inputs have
    // a full block buffer; for control/scalar read the single value (index 0)
    // for every sample so we never read past a 1-sample wire buffer.
    const bool clkAudio = (INRATE(kClock) == calc_FullRate);
    const bool rstAudio = (INRATE(kReset) == calc_FullRate);

    // resolve external buffer (per block), or keep the internal one
    if (!unit->ownBuf) {
        float fbufnum = IN0(kBufnum);
        if (fbufnum < 0.f) fbufnum = 0.f;
        if (fbufnum != unit->m_fbufnum) {
            uint32 bufnum = (uint32)fbufnum;
            World* world = unit->mWorld;
            if (bufnum >= world->mNumSndBufs) bufnum = 0;
            unit->m_fbufnum = fbufnum;
            unit->m_buf = world->mSndBufs + bufnum;
        }
        SndBuf* sbuf = unit->m_buf;
        if (sbuf && sbuf->data && sbuf->frames > 0 && sbuf->channels == 1) {
            unit->buf = sbuf->data;
            unit->bufFrames = (int)sbuf->frames;
        } else {
            // external buffer not ready / not mono: pass dry, advance nothing
            unit->buf = nullptr;
            unit->bufFrames = 0;
        }
    }

    const float mix      = sc_clip(IN0(kMix), 0.f, 1.f);
    const float inLevel  = sc_clip(IN0(kInLevel), 0.f, 4.f);
    const float outLevel = sc_clip(IN0(kOutLevel), 0.f, 4.f);
    const float tanhAmt  = sc_clip(IN0(kTanh), 0.f, 1.f);
    const float fadeSamples = sc_clip(IN0(kFade) * sr, 1.f, sr * 0.1f);
    const float dutyVal  = sc_clip(IN0(kDutyCycle), -1.f, 1.f);
    const bool  reverseRead = (dutyVal < 0.f);

    // per-block param snapshot for the choose_* helpers
    unit->pSubdiv     = snap_subdiv(IN0(kSubdiv));
    unit->pDensity    = sc_clip(IN0(kDensity), 0.f, 1.f);
    unit->pBlockBias  = sc_clip(IN0(kBlockSize), 0.f, 1.f);
    unit->pBlockMaxBeats = (int)sc_clip(IN0(kBlockMax) + 0.5f, 1.f, 16.f);
    unit->pRepeat     = (int)sc_clip(IN0(kRepeatCount) + 0.5f, 2.f, 64.f);
    unit->pRitard     = sc_clip(IN0(kRitardBias), 0.f, 1.f);
    unit->pBlend      = sc_clip(IN0(kBlend), 0.f, 1.f);
    unit->pAccel      = sc_clip(IN0(kAccel), 0.5f, 0.999f);
    {
        float dm = std::fabs(dutyVal);
        unit->pDutyMag = sc_max(dm, 0.01f);
    }
    unit->pAmpMin = sc_clip(IN0(kAmpMin), 0.f, 1.f);
    unit->pAmpMax = sc_clip(IN0(kAmpMax), unit->pAmpMin, 1.f);
    unit->pPhraseMin = (int)(IN0(kPhraseMin) + 0.5f);
    unit->pPhraseMax = (int)(IN0(kPhraseMax) + 0.5f);

    int sub = unit->pSubdiv;
    unit->samplesPerUnit = (sub > 0 && unit->clockPeriodSamples > 0)
        ? (unit->clockPeriodSamples * 4) / sub
        : 12000;
    unit->samplesPerUnit = sc_max(64, unit->samplesPerUnit);

    const int bf = unit->bufFrames;
    const bool haveBuf = (unit->buf != nullptr && bf > 0);

    for (int i = 0; i < inNumSamples; ++i) {
        float inSample = in[i] * inLevel;

        if (haveBuf) {
            buf_write(unit, unit->writePos, inSample);
            unit->writePos = (unit->writePos + 1) % bf;
        }

        bool clkHigh = (clkAudio ? clk[i] : clk[0]) > 0.f;
        if (clkHigh && !unit->clockWasHigh) {
            if (unit->samplesSinceLastClock > 100)
                unit->clockPeriodSamples = unit->samplesSinceLastClock;
            unit->samplesSinceLastClock = 0;
        }
        unit->clockWasHigh = clkHigh;
        unit->samplesSinceLastClock++;

        bool rstHigh = (rstAudio ? rst[i] : rst[0]) > 0.f;
        if (rstHigh && !unit->resetWasHigh) {
            unit->unitsDone = 0;
            unit->unitsInsideBlock = unit->unitsInBlock;
            unit->currentCut = unit->numCuts;
            unit->unitSampleCounter = 0;
            unit->crossfadeCounter = kCrossfadeSamples;
        }
        unit->resetWasHigh = rstHigh;

        unit->unitSampleCounter++;
        if (unit->unitSampleCounter >= unit->samplesPerUnit) {
            unit->unitSampleCounter = 0;
            advance_unit(unit);
        }

        float wet = 0.f;
        if (haveBuf && unit->currentCut < unit->numCuts) {
            CutInfo& ci = unit->cuts[unit->currentCut];
            if (unit->readIndex < ci.length) {
                int readPos = reverseRead
                    ? (unit->sliceOrigin + ci.size - 1 - unit->readIndex) % bf
                    : (unit->sliceOrigin + unit->readIndex) % bf;
                if (readPos < 0) readPos += bf;
                float env = expenv((float)unit->readIndex, fadeSamples, (float)ci.length);
                wet = buf_read(unit, readPos) * ci.amp * env;
                unit->readIndex++;
            } else {
                unit->readIndex++;
            }
            if (unit->readIndex >= ci.size) {
                unit->currentCut++;
                unit->readIndex = 0;
                unit->crossfadeCounter = kCrossfadeSamples;
            }
        }

        if (unit->crossfadeCounter > 0) {
            float blend = (float)unit->crossfadeCounter / (float)kCrossfadeSamples;
            wet = unit->prevOutput * blend + wet * (1.0f - blend);
            unit->crossfadeCounter--;
        }
        unit->prevOutput = zapgremlins(wet);

        float mixed = inSample * (1.0f - mix) + wet * mix;
        if (tanhAmt > 0.001f) mixed = fast_tanh(mixed * (1.0f + tanhAmt * 4.0f));
        out[i] = mixed * outLevel;
    }
}

PluginLoad(Colmatage) {
    ft = inTable;
    DefineDtorUnit(Colmatage);
}
