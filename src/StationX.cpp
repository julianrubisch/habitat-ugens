// StationX (CodescanFilter) — FIR filter whose 4..64-tap kernel is read from
// binary file data (signed 8-bit) at a scan position, L1-normalized, convolved
// with the input. Native SC port of stolmine biome/CodescanFilter.cpp.
#include "SC_PlugIn.h"
#include <cmath>
#include <cstring>

static InterfaceTable* ft;

static const int kMaxFIRTaps = 64;

enum { kIn = 0, kBuf, kScan, kTaps, kMix, kNumInputs };

struct StationX : public Unit {
    float mDelayLine[kMaxFIRTaps];
    int mWriteIdx;
    float mDCState;
};

static void StationX_next(StationX* unit, int inNumSamples);

void StationX_Ctor(StationX* unit) {
    memset(unit->mDelayLine, 0, sizeof(unit->mDelayLine));
    unit->mWriteIdx = 0;
    unit->mDCState = 0.f;
    SETCALC(StationX_next);
    StationX_next(unit, 1);
}

void StationX_next(StationX* unit, int inNumSamples) {
    const float* in = IN(kIn);
    float* out = OUT(0);
    const float sr = SAMPLERATE;

    uint32 bufnum = (uint32)IN0(kBuf);
    World* world = unit->mWorld;
    if (bufnum >= world->mNumSndBufs) bufnum = 0;
    const SndBuf* sbuf = world->mSndBufs + bufnum;
    ACQUIRE_SNDBUF_SHARED(sbuf);
    const float* data = sbuf->data;
    int dataSize = sbuf->frames;

    if (!data || dataSize < kMaxFIRTaps) {
        RELEASE_SNDBUF_SHARED(sbuf);
        for (int i = 0; i < inNumSamples; ++i) out[i] = in[i];
        return;
    }

    const float scan = sc_clip(IN0(kScan), 0.f, 1.f);
    const int numTaps = sc_clip((int)(IN0(kTaps) + 0.5f), 4, kMaxFIRTaps);
    const float mix = sc_clip(IN0(kMix), 0.f, 1.f);
    const float dcCoeff = 6.2832f * 20.f / sr;

    int maxOffset = dataSize - numTaps;
    int scanOffset = sc_clip((int)(scan * (float)maxOffset), 0, maxOffset);

    float kernel[kMaxFIRTaps];
    float normSum = 0.f;
    for (int t = 0; t < numTaps; ++t) { kernel[t] = data[scanOffset + t]; normSum += std::fabs(kernel[t]); }
    if (normSum > 0.001f) { float inv = 1.f / normSum; for (int t = 0; t < numTaps; ++t) kernel[t] *= inv; }

    for (int i = 0; i < inNumSamples; ++i) {
        unit->mDelayLine[unit->mWriteIdx] = in[i];
        unit->mWriteIdx = (unit->mWriteIdx + 1) % numTaps;

        float wet = 0.f;
        int readIdx = unit->mWriteIdx;
        for (int t = 0; t < numTaps; ++t) {
            readIdx--; if (readIdx < 0) readIdx = numTaps - 1;
            wet += unit->mDelayLine[readIdx] * kernel[t];
        }
        float mixed = in[i] * (1.f - mix) + wet * mix;
        unit->mDCState += (mixed - unit->mDCState) * dcCoeff;
        out[i] = mixed - unit->mDCState;
    }
    RELEASE_SNDBUF_SHARED(sbuf);
}

PluginLoad(StationX) {
    ft = inTable;
    DefineSimpleUnit(StationX);
}
