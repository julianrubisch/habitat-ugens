Petrichor : MultiOutUGen {
    *ar { arg in, masterTime = 0.5, feedback = 0.3, feedbackTone = 0, mix = 0.5,
        tapCount = 4, voctPitch = 0, skew = 0, grainSize = 0.5, drift = 0,
        reverse = 0, stack = 0, grid = 0, inLevel = 1, outLevel = 1, tanh = 0,
        mono = 0, maxDelayTime = 8,
        tapLevel = #[1,1,1,1,1,1,1,1], tapPan = #[0,0,0,0,0,0,0,0],
        tapPitch = #[0,0,0,0,0,0,0,0],
        filterCutoff = #[10000,10000,10000,10000,10000,10000,10000,10000],
        filterQ = #[0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5],
        filterType = #[0,0,0,0,0,0,0,0];
        ^this.multiNew('audio', in, masterTime, feedback, feedbackTone, mix,
            tapCount, voctPitch, skew, grainSize, drift, reverse, stack, grid,
            inLevel, outLevel, tanh, mono, maxDelayTime,
            *(tapLevel ++ tapPan ++ tapPitch ++ filterCutoff ++ filterQ ++ filterType))
    }
    init { arg ...theInputs; inputs = theInputs; ^this.initOutputs(2, rate) }
    checkInputs { ^this.checkValidInputs }
}
