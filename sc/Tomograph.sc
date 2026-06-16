Tomograph : UGen {
    *ar { arg in,
        f0 = 440, f1 = 440, f2 = 440, f3 = 440, f4 = 440, f5 = 440, f6 = 440, f7 = 440,
        f8 = 440, f9 = 440, f10 = 440, f11 = 440, f12 = 440, f13 = 440, f14 = 440, f15 = 440,
        g0 = 1, g1 = 1, g2 = 1, g3 = 1, g4 = 1, g5 = 1, g6 = 1, g7 = 1,
        g8 = 1, g9 = 1, g10 = 1, g11 = 1, g12 = 1, g13 = 1, g14 = 1, g15 = 1,
        q = 0.5, mix = 1, tanh = 0, inLevel = 1, outLevel = 1, slew = 0, nbands = 8, ftype = 0;
        ^this.multiNew('audio', in,
            f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15,
            g0, g1, g2, g3, g4, g5, g6, g7, g8, g9, g10, g11, g12, g13, g14, g15,
            q, mix, tanh, inLevel, outLevel, slew, nbands, ftype)
    }
    checkInputs { ^this.checkValidInputs }
}
