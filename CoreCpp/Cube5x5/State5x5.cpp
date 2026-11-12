#include "Cube5x5/State5x5.hpp"
#include <algorithm>
#include <numeric>

State5x5::State5x5() {
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 25; ++i) {
            facelets[face * 25 + i] = static_cast<uint8_t>(face);
        }
    }
}

bool State5x5::isSolved() const {
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 25; ++i) {
            if (facelets[face * 25 + i] != face) return false;
        }
    }
    return true;
}

bool State5x5::isValid() const {
    return validateFacelets(facelets.data());
}

bool State5x5::validateFacelets(const uint8_t colors[150]) {
    int count[6] = {0};
    for (int i = 0; i < 150; ++i) {
        if (colors[i] > 5) return false;
        count[colors[i]]++;
    }
    for (int i = 0; i < 6; ++i) {
        if (count[i] != 25) return false;
    }
    return true;
}

static void rotateFaceClockwise5x5(uint8_t* face) {
    uint8_t temp[25];
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 5; ++c) {
            temp[r * 5 + c] = face[(4 - c) * 5 + r];
        }
    }
    for (int i = 0; i < 25; ++i) {
        face[i] = temp[i];
    }
}

static void cycle4(uint8_t& a, uint8_t& b, uint8_t& c, uint8_t& d) {
    uint8_t t = a;
    a = d;
    d = c;
    c = b;
    b = t;
}

void State5x5::applyMove(uint8_t axis, uint8_t power, uint8_t sliceMask) {
    if (power == 0 || power > 3 || axis > 5) return;

    for (uint8_t p = 0; p < power; ++p) {
        if (sliceMask & 0x01) {
            rotateFaceClockwise5x5(&facelets[axis * 25]);
        }

        for (int layer = 0; layer < 3; ++layer) {
            if (!(sliceMask & (1 << layer))) continue;

            if (axis == 0) { // U axis: F -> L -> B -> R -> F
                int row = layer;
                for (int col = 0; col < 5; ++col) {
                    cycle4(facelets[4 * 25 + row * 5 + col],
                           facelets[2 * 25 + row * 5 + col],
                           facelets[5 * 25 + row * 5 + col],
                           facelets[3 * 25 + row * 5 + col]);
                }
            } else if (axis == 1) { // D axis: F -> R -> B -> L -> F
                int row = 4 - layer;
                for (int col = 0; col < 5; ++col) {
                    cycle4(facelets[4 * 25 + row * 5 + col],
                           facelets[3 * 25 + row * 5 + col],
                           facelets[5 * 25 + row * 5 + col],
                           facelets[2 * 25 + row * 5 + col]);
                }
            } else if (axis == 2) { // L axis: U -> F -> D -> B -> U
                int col = layer;
                for (int row = 0; row < 5; ++row) {
                    cycle4(facelets[0 * 25 + row * 5 + col],
                           facelets[4 * 25 + row * 5 + col],
                           facelets[1 * 25 + row * 5 + col],
                           facelets[5 * 25 + (4 - row) * 5 + (4 - col)]);
                }
            } else if (axis == 3) { // R axis: U -> B -> D -> F -> U
                int col = 4 - layer;
                for (int row = 0; row < 5; ++row) {
                    cycle4(facelets[0 * 25 + row * 5 + col],
                           facelets[5 * 25 + (4 - row) * 5 + (4 - col)],
                           facelets[1 * 25 + row * 5 + col],
                           facelets[4 * 25 + row * 5 + col]);
                }
            } else if (axis == 4) { // F axis: U -> R -> D -> L -> U
                int offset = layer;
                for (int i = 0; i < 5; ++i) {
                    cycle4(facelets[0 * 25 + (4 - offset) * 5 + i],
                           facelets[3 * 25 + i * 5 + offset],
                           facelets[1 * 25 + offset * 5 + (4 - i)],
                           facelets[2 * 25 + (4 - i) * 5 + (4 - offset)]);
                }
            } else if (axis == 5) { // B axis: U -> L -> D -> R -> U
                int offset = layer;
                for (int i = 0; i < 5; ++i) {
                    cycle4(facelets[0 * 25 + offset * 5 + (4 - i)],
                           facelets[2 * 25 + i * 5 + offset],
                           facelets[1 * 25 + (4 - offset) * 5 + i],
                           facelets[3 * 25 + (4 - i) * 5 + (4 - offset)]);
                }
            }
        }
    }
}

void State5x5::applyMove(const Move& m) {
    applyMove(m.axis, m.power, m.sliceMask);
}

bool State5x5::areCentersSolved() const {
    for (int face = 0; face < 6; ++face) {
        uint8_t c = facelets[face * 25 + 12]; // Fixed center at (2,2)
        for (int r = 1; r <= 3; ++r) {
            for (int col = 1; col <= 3; ++col) {
                if (facelets[face * 25 + r * 5 + col] != c) return false;
            }
        }
    }
    return true;
}

struct TredgeInfo {
    int midge_f1;
    int midge_f2;
    int w1_f1;
    int w1_f2;
    int w2_f1;
    int w2_f2;
};

bool State5x5::areTredgesPaired() const {
    static const TredgeInfo tredges[12] = {
        // UR: midge (2, 2*5+4), w1 (1, 1*5+4), w2 (3, 3*5+4)
        {0*25 + 2, 3*25 + 2,  0*25 + 1, 3*25 + 1,  0*25 + 3, 3*25 + 3},
        // UF: midge (4*5+2, 2), w1 (4*5+1, 1), w2 (4*5+3, 3)
        {0*25 + 22, 4*25 + 2, 0*25 + 21, 4*25 + 1, 0*25 + 23, 4*25 + 3},
        // UL
        {0*25 + 10, 2*25 + 2, 0*25 + 5,  2*25 + 1, 0*25 + 15, 2*25 + 3},
        // UB
        {0*25 + 2,  5*25 + 2, 0*25 + 1,  5*25 + 3, 0*25 + 3,  5*25 + 1},
        // DR
        {1*25 + 14, 3*25 + 22, 1*25 + 9,  3*25 + 21, 1*25 + 19, 3*25 + 23},
        // DF
        {1*25 + 2,  4*25 + 22, 1*25 + 1,  4*25 + 23, 1*25 + 3,  4*25 + 21},
        // DL
        {1*25 + 10, 2*25 + 22, 1*25 + 5,  2*25 + 23, 1*25 + 15, 2*25 + 21},
        // DB
        {1*25 + 22, 5*25 + 22, 1*25 + 21, 5*25 + 23, 1*25 + 23, 5*25 + 21},
        // FR
        {4*25 + 14, 3*25 + 10, 4*25 + 9,  3*25 + 5,  4*25 + 19, 3*25 + 15},
        // FL
        {4*25 + 10, 2*25 + 14, 4*25 + 5,  2*25 + 9,  4*25 + 15, 2*25 + 19},
        // BL
        {5*25 + 14, 2*25 + 10, 5*25 + 9,  2*25 + 5,  5*25 + 19, 2*25 + 15},
        // BR
        {5*25 + 10, 3*25 + 14, 5*25 + 5,  3*25 + 9,  5*25 + 15, 3*25 + 19}
    };

    for (int e = 0; e < 12; ++e) {
        uint8_t mf1 = facelets[tredges[e].midge_f1];
        uint8_t mf2 = facelets[tredges[e].midge_f2];
        uint8_t w1f1 = facelets[tredges[e].w1_f1];
        uint8_t w1f2 = facelets[tredges[e].w1_f2];
        uint8_t w2f1 = facelets[tredges[e].w2_f1];
        uint8_t w2f2 = facelets[tredges[e].w2_f2];

        if (w1f1 != mf1 || w1f2 != mf2) return false;
        if (w2f1 != mf1 || w2f2 != mf2) return false;
    }
    return true;
}

bool State5x5::hasEdgeFlipParity() const {
    State3x3 s3 = toReduced3x3();
    uint32_t eoSum = 0;
    for (int i = 0; i < 12; ++i) eoSum += s3.eo[i];
    return (eoSum % 2) != 0;
}

State3x3 State5x5::toReduced3x3() const {
    uint8_t c54[54];
    for (int face = 0; face < 6; ++face) {
        const uint8_t* f5 = &facelets[face * 25];
        uint8_t* f3 = &c54[face * 9];
        // Corners: (0,0)->0, (0,4)->2, (4,0)->6, (4,4)->8
        f3[0] = f5[0];
        f3[2] = f5[4];
        f3[6] = f5[20];
        f3[8] = f5[24];
        // Midges: (0,2)->1, (2,0)->3, (2,4)->5, (4,2)->7
        f3[1] = f5[2];
        f3[3] = f5[10];
        f3[5] = f5[14];
        f3[7] = f5[22];
        // Fixed Center: (2,2)->4
        f3[4] = f5[12];
    }
    return State3x3::fromFacelets(c54);
}

State5x5 State5x5::fromFacelets(const uint8_t colors[150]) {
    State5x5 state;
    for (int i = 0; i < 150; ++i) {
        state.facelets[i] = colors[i];
    }
    return state;
}
