#include "Cube4x4/State4x4.hpp"
#include <algorithm>
#include <numeric>

State4x4::State4x4() {
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 16; ++i) {
            facelets[face * 16 + i] = static_cast<uint8_t>(face);
        }
    }
}

bool State4x4::isSolved() const {
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 16; ++i) {
            if (facelets[face * 16 + i] != face) return false;
        }
    }
    return true;
}

bool State4x4::isValid() const {
    return validateFacelets(facelets.data());
}

bool State4x4::validateFacelets(const uint8_t colors[96]) {
    int count[6] = {0};
    for (int i = 0; i < 96; ++i) {
        if (colors[i] > 5) return false;
        count[colors[i]]++;
    }
    for (int i = 0; i < 6; ++i) {
        if (count[i] != 16) return false;
    }
    return true;
}

static void rotateFaceClockwise(uint8_t* face) {
    uint8_t temp[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            temp[r * 4 + c] = face[(3 - c) * 4 + r];
        }
    }
    for (int i = 0; i < 16; ++i) {
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

// Applies a single 90-degree turn of slice layer (0 = outer, 1 = inner)
void State4x4::applyMove(uint8_t axis, uint8_t power, uint8_t sliceMask) {
    if (power == 0 || power > 3 || axis > 5) return;

    for (uint8_t p = 0; p < power; ++p) {
        // If outer layer (layer 0) is turned, rotate face stickers
        if (sliceMask & 0x01) {
            rotateFaceClockwise(&facelets[axis * 16]);
        }

        for (int layer = 0; layer < 2; ++layer) {
            if (!(sliceMask & (1 << layer))) continue;

            if (axis == 0) { // U axis: F -> L -> B -> R -> F
                int row = layer;
                for (int col = 0; col < 4; ++col) {
                    cycle4(facelets[4 * 16 + row * 4 + col],  // F
                           facelets[2 * 16 + row * 4 + col],  // L
                           facelets[5 * 16 + row * 4 + col],  // B
                           facelets[3 * 16 + row * 4 + col]); // R
                }
            } else if (axis == 1) { // D axis: F -> R -> B -> L -> F
                int row = 3 - layer;
                for (int col = 0; col < 4; ++col) {
                    cycle4(facelets[4 * 16 + row * 4 + col],  // F
                           facelets[3 * 16 + row * 4 + col],  // R
                           facelets[5 * 16 + row * 4 + col],  // B
                           facelets[2 * 16 + row * 4 + col]); // L
                }
            } else if (axis == 2) { // L axis: U -> F -> D -> B -> U
                int col = layer;
                for (int row = 0; row < 4; ++row) {
                    cycle4(facelets[0 * 16 + row * 4 + col],         // U
                           facelets[4 * 16 + row * 4 + col],         // F
                           facelets[1 * 16 + row * 4 + col],         // D
                           facelets[5 * 16 + (3 - row) * 4 + (3 - col)]); // B reversed
                }
            } else if (axis == 3) { // R axis: U -> B -> D -> F -> U
                int col = 3 - layer;
                for (int row = 0; row < 4; ++row) {
                    cycle4(facelets[0 * 16 + row * 4 + col],         // U
                           facelets[5 * 16 + (3 - row) * 4 + (3 - col)], // B reversed
                           facelets[1 * 16 + row * 4 + col],         // D
                           facelets[4 * 16 + row * 4 + col]);        // F
                }
            } else if (axis == 4) { // F axis: U -> R -> D -> L -> U
                int offset = layer;
                for (int i = 0; i < 4; ++i) {
                    cycle4(facelets[0 * 16 + (3 - offset) * 4 + i], // U bottom row
                           facelets[3 * 16 + i * 4 + offset],       // R left col
                           facelets[1 * 16 + offset * 4 + (3 - i)], // D top row
                           facelets[2 * 16 + (3 - i) * 4 + (3 - offset)]); // L right col
                }
            } else if (axis == 5) { // B axis: U -> L -> D -> R -> U
                int offset = layer;
                for (int i = 0; i < 4; ++i) {
                    cycle4(facelets[0 * 16 + offset * 4 + (3 - i)], // U top row
                           facelets[2 * 16 + i * 4 + offset],       // L left col
                           facelets[1 * 16 + (3 - offset) * 4 + i], // D bottom row
                           facelets[3 * 16 + (3 - i) * 4 + (3 - offset)]); // R right col
                }
            }
        }
    }
}

void State4x4::applyMove(const Move& m) {
    applyMove(m.axis, m.power, m.sliceMask);
}

bool State4x4::areCentersSolved() const {
    static const int centerIdx[4] = {5, 6, 9, 10};
    for (int face = 0; face < 6; ++face) {
        uint8_t c = facelets[face * 16 + centerIdx[0]];
        for (int i = 1; i < 4; ++i) {
            if (facelets[face * 16 + centerIdx[i]] != c) return false;
        }
    }
    return true;
}

struct WingPairInfo {
    int w1_facelet1;
    int w1_facelet2;
    int w2_facelet1;
    int w2_facelet2;
};

bool State4x4::areWingsPaired() const {
    static const WingPairInfo wingPairs[12] = {
        // UR: U(7, 11), R(2, 1)
        {0*16 + 7,  3*16 + 2,  0*16 + 11, 3*16 + 1},
        // UF: U(13, 14), F(1, 2)
        {0*16 + 13, 4*16 + 1,  0*16 + 14, 4*16 + 2},
        // UL: U(4, 8), L(1, 2)
        {0*16 + 4,  2*16 + 1,  0*16 + 8,  2*16 + 2},
        // UB: U(1, 2), B(2, 1)
        {0*16 + 1,  5*16 + 2,  0*16 + 2,  5*16 + 1},
        // DR: D(7, 11), R(13, 14)
        {1*16 + 7,  3*16 + 13, 1*16 + 11, 3*16 + 14},
        // DF: D(1, 2), F(13, 14)
        {1*16 + 1,  4*16 + 13, 1*16 + 2,  4*16 + 14},
        // DL: D(4, 8), L(14, 13)
        {1*16 + 4,  2*16 + 14, 1*16 + 8,  2*16 + 13},
        // DB: D(13, 14), B(13, 14)
        {1*16 + 13, 5*16 + 14, 1*16 + 14, 5*16 + 13},
        // FR: F(7, 11), R(4, 8)
        {4*16 + 7,  3*16 + 4,  4*16 + 11, 3*16 + 8},
        // FL: F(4, 8), L(7, 11)
        {4*16 + 4,  2*16 + 7,  4*16 + 8,  2*16 + 11},
        // BL: B(7, 11), L(4, 8)
        {5*16 + 7,  2*16 + 4,  5*16 + 11, 2*16 + 8},
        // BR: B(4, 8), R(7, 11)
        {5*16 + 4,  3*16 + 7,  5*16 + 8,  3*16 + 11}
    };

    for (int e = 0; e < 12; ++e) {
        uint8_t c1a = facelets[wingPairs[e].w1_facelet1];
        uint8_t c1b = facelets[wingPairs[e].w1_facelet2];
        uint8_t c2a = facelets[wingPairs[e].w2_facelet1];
        uint8_t c2b = facelets[wingPairs[e].w2_facelet2];
        if (c1a != c2a || c1b != c2b) return false;
    }
    return true;
}

bool State4x4::hasOLLParity() const {
    State3x3 s3 = toReduced3x3();
    uint32_t eoSum = 0;
    for (int i = 0; i < 12; ++i) eoSum += s3.eo[i];
    return (eoSum % 2) != 0;
}

bool State4x4::hasPLLParity() const {
    State3x3 s3 = toReduced3x3();
    int cParity = 0, eParity = 0;
    for (int i = 0; i < 7; ++i) {
        for (int j = i + 1; j < 8; ++j) {
            if (s3.cp[j] < s3.cp[i]) cParity++;
        }
    }
    for (int i = 0; i < 11; ++i) {
        for (int j = i + 1; j < 12; ++j) {
            if (s3.ep[j] < s3.ep[i]) eParity++;
        }
    }
    return (cParity % 2) != (eParity % 2);
}

State3x3 State4x4::toReduced3x3() const {
    uint8_t c54[54];
    // Map 4x4 outer corners and paired edges to 3x3 54 facelets
    for (int face = 0; face < 6; ++face) {
        const uint8_t* f4 = &facelets[face * 16];
        uint8_t* f3 = &c54[face * 9];
        // Corners: 0->0, 3->2, 12->6, 15->8
        f3[0] = f4[0];
        f3[2] = f4[3];
        f3[6] = f4[12];
        f3[8] = f4[15];
        // Edges: average/pick wing
        f3[1] = f4[1]; // top edge
        f3[3] = f4[4]; // left edge
        f3[5] = f4[7]; // right edge
        f3[7] = f4[13]; // bottom edge
        // Center: center 5
        f3[4] = f4[5];
    }
    return State3x3::fromFacelets(c54);
}

State4x4 State4x4::fromFacelets(const uint8_t colors[96]) {
    State4x4 state;
    for (int i = 0; i < 96; ++i) {
        state.facelets[i] = colors[i];
    }
    return state;
}
