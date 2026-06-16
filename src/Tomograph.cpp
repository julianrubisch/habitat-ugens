// Native SuperCollider UGen — faithful port of the ER-301 "Tomograph"
// (stolmine Filterbank): 16-band parallel TPT SVF bank with Q-tilt,
// per-band freq/gain, dry/wet, tanh drive.
#include "SC_PlugIn.h"
#include <cmath>

static InterfaceTable* ft;
static const int kNumBands = 16;

enum {
    kIn = 0,
    kFreq0,                            // kFreq0 .. kFreq0+15
    kGain0 = kFreq0 + kNumBands,       // kGain0 .. kGain0+15
    kQ = kGain0 + kNumBands,
    kMix, kTanh, kInLevel, kOutLevel, kSlew, kNbands, kFtype,
    kNumInputs
};

struct Tomograph : public Unit {
    float s1[kNumBands];
    float s2[kNumBands];
    float curFreq[kNumBands];
};

static void Tomograph_next(Tomograph* unit, int inNumSamples);

void Tomograph_Ctor(Tomograph* unit) {
    for (int i = 0; i < kNumBands; ++i) {
        unit->s1[i] = 0.f; unit->s2[i] = 0.f; unit->curFreq[i] = 440.f;
    }
    SETCALC(Tomograph_next);
    Tomograph_next(unit, 1);
}

void Tomograph_next(Tomograph* unit, int inNumSamples) {
    const float* in = IN(kIn);
    float* out = OUT(0);
    const float sr = SAMPLERATE;
    const float pi = (float)M_PI;

    const float inLevel  = IN0(kInLevel);
    const float outLevel = IN0(kOutLevel);
    const float macroQ   = sc_clip(IN0(kQ), 0.f, 1.f);
    const float mix      = sc_clip(IN0(kMix), 0.f, 1.f);
    const float tanhA    = sc_clip(IN0(kTanh), 0.f, 1.f);
    const float slew     = sc_max(IN0(kSlew), 0.f);
    int nbands = sc_clip((int)(IN0(kNbands) + 0.5f), 1, kNumBands);
    int ftype  = sc_clip((int)(IN0(kFtype)  + 0.5f), 0, 2);

    const float baseQ   = 1.f + 99.f * macroQ * macroQ;
    const float qLoss   = macroQ * (2.f - macroQ) * 0.85f + 0.15f;
    const float floorQ  = (ftype == 1) ? 5.f : ((ftype == 2) ? 20.f : 0.5f);
    const float sumNorm = 1.f / std::sqrt((float)nbands);
    const float drv     = 1.f + tanhA * 3.f;
    const float slewCoef = (slew > 1e-6f) ? std::exp(-1.f / (slew * sr)) : 0.f;
    const float nyq = sr * 0.49f;

    float tgt[kNumBands], gn[kNumBands], qpow[kNumBands];
    float p = 1.f;
    for (int i = 0; i < kNumBands; ++i) {
        tgt[i]  = sc_clip(IN0(kFreq0 + i), 20.f, nyq);
        gn[i]   = IN0(kGain0 + i);
        qpow[i] = p; p *= qLoss;
    }

    for (int n = 0; n < inNumSamples; ++n) {
        float x = in[n] * inLevel;
        float wet = 0.f;
        for (int i = 0; i < kNumBands; ++i) {
            unit->curFreq[i] = tgt[i] + slewCoef * (unit->curFreq[i] - tgt[i]);
            float f = unit->curFreq[i];
            float g = std::tan(pi * f / sr);
            float bandQ = baseQ * qpow[i] * (0.5f + 2.f * (f / sr));
            if (bandQ < floorQ) bandQ = floorQ;
            float r = 1.f / bandQ;
            float h = 1.f / (1.f + r * g + g * g);
            float s1 = unit->s1[i], s2 = unit->s2[i];
            float hp = (x - r * s1 - g * s1 - s2) * h;
            float bp = s1 + g * hp;
            float lp = s2 + g * bp;
            unit->s1[i] = zapgremlins(bp + g * hp);
            unit->s2[i] = zapgremlins(lp + g * bp);
            float tap = (ftype == 1) ? lp : bp;
            wet += tap * gn[i];
        }
        wet *= sumNorm;
        float m = x * (1.f - mix) + wet * mix;
        if (tanhA > 0.001f) m = m * (1.f - tanhA) + std::tanh(m * drv) * tanhA;
        out[n] = m * outLevel;
    }
}

PluginLoad(Tomograph) {
    ft = inTable;
    DefineSimpleUnit(Tomograph);
}
