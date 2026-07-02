// BletchleyPark (CodescanOsc) — wavetable osc scanning a 256-sample window
// through a 4096-sample region of binary file data (signed 8-bit). Native
// SC port of stolmine er-301-habitat biome/CodescanOsc.cpp.
#include "SC_PlugIn.h"
#include <cmath>

static InterfaceTable* ft;

enum { kVOct = 0, kSync, kBuf, kScan, kFund, kRegionStart, kNumInputs };

struct BletchleyPark : public Unit {
    float mPhase;
    bool mSyncWasHigh;
    float mDCState;
};

static void BletchleyPark_next(BletchleyPark* unit, int inNumSamples);

void BletchleyPark_Ctor(BletchleyPark* unit) {
    unit->mPhase = 0.f;
    unit->mSyncWasHigh = false;
    unit->mDCState = 0.f;
    SETCALC(BletchleyPark_next);
    BletchleyPark_next(unit, 1);
}

void BletchleyPark_next(BletchleyPark* unit, int inNumSamples) {
    const float* voct = IN(kVOct);
    const float* sync = IN(kSync);
    // voct and sync may be audio-rate (per-sample) OR scalar/control (a single
    // value, e.g. a constant 0). A scalar input is a size-1 wire, so reading it
    // as an audio buffer (voct[i] for i>0) walks off the end -> garbage/NaN ->
    // silence. Honour the actual input rate instead.
    const bool voctAudio = INRATE(kVOct) == calc_FullRate;
    const bool syncAudio = INRATE(kSync) == calc_FullRate;
    float* out = OUT(0);
    const float sr = SAMPLERATE;

    uint32 bufnum = (uint32)IN0(kBuf);
    World* world = unit->mWorld;
    if (bufnum >= world->mNumSndBufs) bufnum = 0;
    const SndBuf* sbuf = world->mSndBufs + bufnum;
    ACQUIRE_SNDBUF_SHARED(sbuf);
    const float* data = sbuf->data;
    int dataSize = sbuf->frames;

    if (!data || dataSize < 256) {
        RELEASE_SNDBUF_SHARED(sbuf);
        for (int i = 0; i < inNumSamples; ++i) out[i] = 0.f;
        return;
    }

    const float f0 = sc_clip(IN0(kFund), 0.1f, sr * 0.49f);
    const float scan = sc_clip(IN0(kScan), 0.f, 1.f);
    const float dcCoeff = 6.2832f * 20.f / sr;
    const int windowSize = 256;
    int maxOffset = dataSize - windowSize;
    int scanRange = 4096; if (scanRange > maxOffset) scanRange = maxOffset;
    int regionStart = sc_clip((int)IN0(kRegionStart), 0, maxOffset);
    int scanOffset = regionStart + (int)(scan * (float)scanRange);
    scanOffset = sc_clip(scanOffset, 0, maxOffset);

    for (int i = 0; i < inNumSamples; ++i) {
        float syncV = syncAudio ? sync[i] : sync[0];
        bool syncHigh = syncV > 0.5f;
        if (syncHigh && !unit->mSyncWasHigh) unit->mPhase = 0.f;
        unit->mSyncWasHigh = syncHigh;

        float voctV = voctAudio ? voct[i] : voct[0];
        float freq = f0 * std::pow(2.f, voctV * 10.f);
        unit->mPhase += freq / sr;
        unit->mPhase -= std::floor(unit->mPhase);

        float pos = unit->mPhase * (float)(windowSize - 1);
        int idx0 = (int)pos;
        float frac = pos - (float)idx0;
        int idx1 = idx0 + 1; if (idx1 >= windowSize) idx1 = 0;
        float s0 = data[scanOffset + idx0];
        float s1 = data[scanOffset + idx1];
        float raw = s0 + (s1 - s0) * frac;

        unit->mDCState += (raw - unit->mDCState) * dcCoeff;
        out[i] = raw - unit->mDCState;
    }
    RELEASE_SNDBUF_SHARED(sbuf);
}

PluginLoad(BletchleyPark) {
    ft = inTable;
    DefineSimpleUnit(BletchleyPark);
}
