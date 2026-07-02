Colmatage : UGen {
    *ar { arg in, clock = 0, reset = 0,
        density = 0.5, blockSize = 0.5, blockMax = 8, repeatCount = 4,
        ritardBias = 0.5, blend = 0.5, accel = 0.9,
        subdiv = 8, phraseMin = 2, phraseMax = 4,
        dutyCycle = 1, ampMin = 0.8, ampMax = 1, fade = 0.005,
        mix = 1, inLevel = 1, outLevel = 1, tanh = 0,
        bufnum = -1, maxdur = 8;
        ^this.multiNew('audio', in, clock, reset,
            density, blockSize, blockMax, repeatCount, ritardBias, blend, accel,
            subdiv, phraseMin, phraseMax,
            dutyCycle, ampMin, ampMax, fade,
            mix, inLevel, outLevel, tanh,
            bufnum, maxdur)
    }
    checkInputs { ^this.checkValidInputs }
}
