StationX : UGen {
    *ar { arg in, buf, scan = 0, taps = 32, mix = 0.5;
        ^this.multiNew('audio', in, buf, scan, taps, mix)
    }
    checkInputs { ^this.checkValidInputs }
}
