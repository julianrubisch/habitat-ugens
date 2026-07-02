BletchleyPark : UGen {
    *ar { arg voct = 0, sync = 0, buf, scan = 0, fundamental = 110, regionStart = 0;
        ^this.multiNew('audio', voct, sync, buf, scan, fundamental, regionStart)
    }
    checkInputs { ^this.checkValidInputs }
}
