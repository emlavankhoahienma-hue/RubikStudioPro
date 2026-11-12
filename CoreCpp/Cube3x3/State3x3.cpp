#include "Cube3x3/State3x3.hpp"
#include <algorithm>
#include <numeric>

State3x3::State3x3() {
    for (uint8_t i = 0; i < 8; ++i) {
        cp[i] = i;
        co[i] = 0;
    }
    for (uint8_t i = 0; i < 12; ++i) {
        ep[i] = i;
        eo[i] = 0;
    }
}

bool State3x3::isSolved() const {
    for (uint8_t i = 0; i < 8; ++i) {
        if (cp[i] != i || co[i] != 0) return false;
    }
    for (uint8_t i = 0; i < 12; ++i) {
        if (ep[i] != i || eo[i] != 0) return false;
    }
    return true;
}

static int countInversions(const uint8_t* arr, int n) {
    int inv = 0;
    for (int i = 0; i < n - 1; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (arr[i] > arr[j]) inv++;
        }
    }
    return inv;
}

bool State3x3::isValid() const {
    // 1. Check permutation validity
    uint8_t cCount[8] = {0};
    uint32_t coSum = 0;
    for (int i = 0; i < 8; ++i) {
        if (cp[i] > 7 || co[i] > 2) return false;
        cCount[cp[i]]++;
        coSum += co[i];
    }
    for (int i = 0; i < 8; ++i) {
        if (cCount[i] != 1) return false;
    }

    uint8_t eCount[12] = {0};
    uint32_t eoSum = 0;
    for (int i = 0; i < 12; ++i) {
        if (ep[i] > 11 || eo[i] > 1) return false;
        eCount[ep[i]]++;
        eoSum += eo[i];
    }
    for (int i = 0; i < 12; ++i) {
        if (eCount[i] != 1) return false;
    }

    // 2. Corner orientation sum mod 3 == 0
    if ((coSum % 3) != 0) return false;

    // 3. Edge orientation sum mod 2 == 0
    if ((eoSum % 2) != 0) return false;

    // 4. Parity of corners == Parity of edges
    int cParity = countInversions(cp.data(), 8) % 2;
    int eParity = countInversions(ep.data(), 12) % 2;
    if (cParity != eParity) return false;

    return true;
}

void State3x3::applyMove(const Move& m) {
    applyMove(m.axis, m.power, m.sliceMask);
}

void State3x3::applyMove(uint8_t axis, uint8_t power, uint8_t sliceMask) {
    if (power == 0 || power > 3 || axis > 5) return;

    for (uint8_t p = 0; p < power; ++p) {
        if (axis == 0) { // U move
            if (sliceMask & 0x01) {
                // Corners: 0 <- 3 <- 2 <- 1 <- 0
                uint8_t tcp = cp[0], tco = co[0];
                cp[0] = cp[3]; co[0] = co[3];
                cp[3] = cp[2]; co[3] = co[2];
                cp[2] = cp[1]; co[2] = co[1];
                cp[1] = tcp;   co[1] = tco;

                // Edges: 0 <- 3 <- 2 <- 1 <- 0
                uint8_t tep = ep[0], teo = eo[0];
                ep[0] = ep[3]; eo[0] = eo[3];
                ep[3] = ep[2]; eo[3] = eo[2];
                ep[2] = ep[1]; eo[2] = eo[1];
                ep[1] = tep;   eo[1] = teo;
            }
            if (sliceMask & 0x02) { // E' slice (follows U)
                // Slice edges: 8 <- 11 <- 10 <- 9 <- 8 (FR, BR, BL, FL)
                uint8_t tep = ep[8], teo = eo[8];
                ep[8] = ep[11]; eo[8] = eo[11];
                ep[11] = ep[10]; eo[11] = eo[10];
                ep[10] = ep[9];  eo[10] = eo[9];
                ep[9] = tep;    eo[9] = teo;
            }
        } else if (axis == 1) { // D move
            if (sliceMask & 0x01) {
                // Corners: 4 <- 5 <- 6 <- 7 <- 4 (DFR, DLF, DBL, DRB)
                uint8_t tcp = cp[4], tco = co[4];
                cp[4] = cp[5]; co[4] = co[5];
                cp[5] = cp[6]; co[5] = co[6];
                cp[6] = cp[7]; co[6] = co[7];
                cp[7] = tcp;   co[7] = tco;

                // Edges: 4 <- 5 <- 6 <- 7 <- 4 (DR, DF, DL, DB)
                uint8_t tep = ep[4], teo = eo[4];
                ep[4] = ep[5]; eo[4] = eo[5];
                ep[5] = ep[6]; eo[5] = eo[6];
                ep[6] = ep[7]; eo[6] = eo[7];
                ep[7] = tep;   eo[7] = teo;
            }
            if (sliceMask & 0x02) { // E slice (follows D)
                uint8_t tep = ep[8], teo = eo[8];
                ep[8] = ep[9];  eo[8] = eo[9];
                ep[9] = ep[10]; eo[9] = eo[10];
                ep[10] = ep[11]; eo[10] = eo[11];
                ep[11] = tep;   eo[11] = teo;
            }
        } else if (axis == 2) { // L move
            if (sliceMask & 0x01) {
                // Corners: 1 <- 2 <- 6 <- 5 <- 1 (UFL, ULB, DBL, DLF)
                uint8_t p1 = cp[1], o1 = co[1];
                uint8_t p2 = cp[2], o2 = co[2];
                uint8_t p6 = cp[6], o6 = co[6];
                uint8_t p5 = cp[5], o5 = co[5];
                cp[1] = p2; co[1] = static_cast<uint8_t>((o2 + 2) % 3);
                cp[2] = p6; co[2] = static_cast<uint8_t>((o6 + 1) % 3);
                cp[6] = p5; co[6] = static_cast<uint8_t>((o5 + 2) % 3);
                cp[5] = p1; co[5] = static_cast<uint8_t>((o1 + 1) % 3);

                // Edges: 2 <- 10 <- 6 <- 9 <- 2 (UL, BL, DL, FL)
                uint8_t tep = ep[2], teo = eo[2];
                ep[2] = ep[10]; eo[2] = eo[10];
                ep[10] = ep[6]; eo[10] = eo[6];
                ep[6] = ep[9];  eo[6] = eo[9];
                ep[9] = tep;   eo[9] = teo;
            }
            if (sliceMask & 0x02) { // M slice (follows L)
                // Edges: 1 <- 3 <- 7 <- 5 <- 1 (UF, UB, DB, DF)
                uint8_t tep = ep[1], teo = eo[1];
                ep[1] = ep[3]; eo[1] = static_cast<uint8_t>(eo[3] ^ 1);
                ep[3] = ep[7]; eo[3] = static_cast<uint8_t>(eo[7] ^ 1);
                ep[7] = ep[5]; eo[7] = static_cast<uint8_t>(eo[5] ^ 1);
                ep[5] = tep;   eo[5] = static_cast<uint8_t>(teo ^ 1);
            }
        } else if (axis == 3) { // R move
            if (sliceMask & 0x01) {
                // Corners: 0 <- 4 <- 7 <- 3 <- 0 (URF, DFR, DRB, UBR)
                uint8_t p0 = cp[0], o0 = co[0];
                uint8_t p4 = cp[4], o4 = co[4];
                uint8_t p7 = cp[7], o7 = co[7];
                uint8_t p3 = cp[3], o3 = co[3];
                cp[0] = p4; co[0] = static_cast<uint8_t>((o4 + 1) % 3);
                cp[4] = p7; co[4] = static_cast<uint8_t>((o7 + 2) % 3);
                cp[7] = p3; co[7] = static_cast<uint8_t>((o3 + 1) % 3);
                cp[3] = p0; co[3] = static_cast<uint8_t>((o0 + 2) % 3);

                // Edges: 0 <- 8 <- 4 <- 11 <- 0 (UR, FR, DR, BR)
                uint8_t tep = ep[0], teo = eo[0];
                ep[0] = ep[8];  eo[0] = eo[8];
                ep[8] = ep[4];  eo[8] = eo[4];
                ep[4] = ep[11]; eo[4] = eo[11];
                ep[11] = tep;   eo[11] = teo;
            }
            if (sliceMask & 0x02) { // M' slice (follows R)
                uint8_t tep = ep[1], teo = eo[1];
                ep[1] = ep[5]; eo[1] = static_cast<uint8_t>(eo[5] ^ 1);
                ep[5] = ep[7]; eo[5] = static_cast<uint8_t>(eo[7] ^ 1);
                ep[7] = ep[3]; eo[7] = static_cast<uint8_t>(eo[3] ^ 1);
                ep[3] = tep;   eo[3] = static_cast<uint8_t>(teo ^ 1);
            }
        } else if (axis == 4) { // F move
            if (sliceMask & 0x01) {
                // Corners: 0 <- 1 <- 5 <- 4 <- 0 (URF, UFL, DLF, DFR)
                uint8_t p0 = cp[0], o0 = co[0];
                uint8_t p1 = cp[1], o1 = co[1];
                uint8_t p5 = cp[5], o5 = co[5];
                uint8_t p4 = cp[4], o4 = co[4];
                cp[0] = p1; co[0] = static_cast<uint8_t>((o1 + 2) % 3);
                cp[1] = p5; co[1] = static_cast<uint8_t>((o5 + 1) % 3);
                cp[5] = p4; co[5] = static_cast<uint8_t>((o4 + 2) % 3);
                cp[4] = p0; co[4] = static_cast<uint8_t>((o0 + 1) % 3);

                // Edges: 1 <- 9 <- 5 <- 8 <- 1 (UF, FL, DF, FR)
                uint8_t p_ep1 = ep[1], p_eo1 = eo[1];
                uint8_t p_ep9 = ep[9], p_eo9 = eo[9];
                uint8_t p_ep5 = ep[5], p_eo5 = eo[5];
                uint8_t p_ep8 = ep[8], p_eo8 = eo[8];
                ep[1] = p_ep9; eo[1] = static_cast<uint8_t>(p_eo9 ^ 1);
                ep[9] = p_ep5; eo[9] = static_cast<uint8_t>(p_eo5 ^ 1);
                ep[5] = p_ep8; eo[5] = static_cast<uint8_t>(p_eo8 ^ 1);
                ep[8] = p_ep1; eo[8] = static_cast<uint8_t>(p_eo1 ^ 1);
            }
            if (sliceMask & 0x02) { // S slice (follows F)
                // Edges: 0 <- 2 <- 6 <- 4 <- 0 (UR, UL, DL, DR)
                uint8_t tep = ep[0], teo = eo[0];
                ep[0] = ep[2]; eo[0] = static_cast<uint8_t>(eo[2] ^ 1);
                ep[2] = ep[6]; eo[2] = static_cast<uint8_t>(eo[6] ^ 1);
                ep[6] = ep[4]; eo[6] = static_cast<uint8_t>(eo[4] ^ 1);
                ep[4] = tep;   eo[4] = static_cast<uint8_t>(teo ^ 1);
            }
        } else if (axis == 5) { // B move
            if (sliceMask & 0x01) {
                // Corners: 2 <- 3 <- 7 <- 6 <- 2 (ULB, UBR, DRB, DBL)
                uint8_t p2 = cp[2], o2 = co[2];
                uint8_t p3 = cp[3], o3 = co[3];
                uint8_t p7 = cp[7], o7 = co[7];
                uint8_t p6 = cp[6], o6 = co[6];
                cp[2] = p3; co[2] = static_cast<uint8_t>((o3 + 2) % 3);
                cp[3] = p7; co[3] = static_cast<uint8_t>((o7 + 1) % 3);
                cp[7] = p6; co[7] = static_cast<uint8_t>((o6 + 2) % 3);
                cp[6] = p2; co[6] = static_cast<uint8_t>((o2 + 1) % 3);

                // Edges: 3 <- 11 <- 7 <- 10 <- 3 (UB, BR, DB, BL)
                uint8_t p_ep3  = ep[3],  p_eo3  = eo[3];
                uint8_t p_ep11 = ep[11], p_eo11 = eo[11];
                uint8_t p_ep7  = ep[7],  p_eo7  = eo[7];
                uint8_t p_ep10 = ep[10], p_eo10 = eo[10];
                ep[3]  = p_ep11; eo[3]  = static_cast<uint8_t>(p_eo11 ^ 1);
                ep[11] = p_ep7;  eo[11] = static_cast<uint8_t>(p_eo7 ^ 1);
                ep[7]  = p_ep10; eo[7]  = static_cast<uint8_t>(p_eo10 ^ 1);
                ep[10] = p_ep3;  eo[10] = static_cast<uint8_t>(p_eo3 ^ 1);
            }
            if (sliceMask & 0x02) { // S' slice (follows B)
                uint8_t tep = ep[0], teo = eo[0];
                ep[0] = ep[4]; eo[0] = static_cast<uint8_t>(eo[4] ^ 1);
                ep[4] = ep[6]; eo[4] = static_cast<uint8_t>(eo[6] ^ 1);
                ep[6] = ep[2]; eo[6] = static_cast<uint8_t>(eo[2] ^ 1);
                ep[2] = tep;   eo[2] = static_cast<uint8_t>(teo ^ 1);
            }
        }
    }
}

// Coordinate conversions
uint16_t State3x3::getTwist() const {
    uint16_t ret = 0;
    for (int i = 0; i < 7; ++i) {
        ret = ret * 3 + co[i];
    }
    return ret;
}

void State3x3::setTwist(uint16_t twist) {
    uint32_t sum = 0;
    for (int i = 6; i >= 0; --i) {
        co[i] = twist % 3;
        sum += co[i];
        twist /= 3;
    }
    co[7] = static_cast<uint8_t>((3 - (sum % 3)) % 3);
}

uint16_t State3x3::getFlip() const {
    uint16_t ret = 0;
    for (int i = 0; i < 11; ++i) {
        ret = (ret << 1) | eo[i];
    }
    return ret;
}

void State3x3::setFlip(uint16_t flip) {
    uint32_t sum = 0;
    for (int i = 10; i >= 0; --i) {
        eo[i] = flip & 1;
        sum += eo[i];
        flip >>= 1;
    }
    eo[11] = static_cast<uint8_t>((2 - (sum % 2)) % 2);
}

// UDSlice coordinate: (12 choose 4) combinations = 495
uint16_t State3x3::getUDSlice() const {
    uint16_t coord = 0;
    int k = 3;
    for (int i = 11; i >= 0 && k >= 0; --i) {
        if (ep[i] >= 8) { // Slice edge
            k--;
        } else if (k < i) {
            // C(i, k)
            static const uint16_t nCr[12][5] = {
                {1, 0, 0, 0, 0},
                {1, 1, 0, 0, 0},
                {1, 2, 1, 0, 0},
                {1, 3, 3, 1, 0},
                {1, 4, 6, 4, 1},
                {1, 5, 10, 10, 5},
                {1, 6, 15, 20, 15},
                {1, 7, 21, 35, 35},
                {1, 8, 28, 56, 70},
                {1, 9, 36, 84, 126},
                {1, 10, 45, 120, 210},
                {1, 11, 55, 165, 330}
            };
            if (k >= 0 && k <= 4 && i < 12) {
                coord += nCr[i][k];
            }
        }
    }
    return coord;
}

uint16_t State3x3::getCornerPerm() const {
    uint16_t coord = 0;
    static const uint16_t factorials[8] = {5040, 720, 120, 24, 6, 2, 1, 1};
    for (int i = 0; i < 7; ++i) {
        int count = 0;
        for (int j = i + 1; j < 8; ++j) {
            if (cp[j] < cp[i]) count++;
        }
        coord += count * factorials[i];
    }
    return coord;
}

uint16_t State3x3::getPhase2EdgePerm() const {
    // Permutation of the 8 U/D edges (ep[0..7])
    uint16_t coord = 0;
    static const uint16_t factorials[8] = {5040, 720, 120, 24, 6, 2, 1, 1};
    for (int i = 0; i < 7; ++i) {
        int count = 0;
        for (int j = i + 1; j < 8; ++j) {
            if (ep[j] < ep[i]) count++;
        }
        coord += count * factorials[i];
    }
    return coord;
}

uint16_t State3x3::getUDSliceSorted() const {
    // Permutation of the 4 slice edges (ep[8..11])
    uint16_t coord = 0;
    static const uint16_t factorials[4] = {6, 2, 1, 1};
    for (int i = 8; i < 11; ++i) {
        int count = 0;
        for (int j = i + 1; j < 12; ++j) {
            if (ep[j] < ep[i]) count++;
        }
        coord += count * factorials[i - 8];
    }
    return coord;
}

bool State3x3::validateFacelets(const uint8_t colors[54]) {
    int count[6] = {0};
    for (int i = 0; i < 54; ++i) {
        if (colors[i] > 5) return false;
        count[colors[i]]++;
    }
    for (int i = 0; i < 6; ++i) {
        if (count[i] != 9) return false;
    }
    return true;
}

// 54 facelets layout:
// U: 0..8 (center 4)
// D: 9..17 (center 13)
// L: 18..26 (center 22)
// R: 27..35 (center 31)
// F: 36..44 (center 40)
// B: 45..53 (center 49)
static const int cornerFaces[8][3] = {
    {8, 27, 38}, // URF: U8, R27, F38
    {6, 36, 20}, // UFL: U6, F36, L20
    {0, 18, 47}, // ULB: U0, L18, B47
    {2, 45, 29}, // UBR: U2, B45, R29
    {11, 44, 33}, // DFR: D11, F44, R33
    {9, 26, 42}, // DLF: D9, L26, F42
    {15, 53, 24}, // DBL: D15, B53, L24
    {17, 35, 51}  // DRB: D17, R35, B51
};

static const int edgeFaces[12][2] = {
    {5, 28}, // UR: U5, R28
    {7, 37}, // UF: U7, F37
    {3, 19}, // UL: U3, L19
    {1, 46}, // UB: U1, B46
    {14, 34}, // DR: D14, R34
    {10, 43}, // DF: D10, F43
    {12, 25}, // DL: D12, L25
    {16, 52}, // DB: D16, B52
    {41, 30}, // FR: F41, R30
    {39, 23}, // FL: F39, L23
    {50, 21}, // BL: B50, L21
    {48, 32}  // BR: B48, R32
};

static const uint8_t cornerColorsRef[8][3] = {
    {0, 3, 4}, // URF
    {0, 4, 2}, // UFL
    {0, 2, 5}, // ULB
    {0, 5, 3}, // UBR
    {1, 4, 3}, // DFR
    {1, 2, 4}, // DLF
    {1, 5, 2}, // DBL
    {1, 3, 5}  // DRB
};

static const uint8_t edgeColorsRef[12][2] = {
    {0, 3}, {0, 4}, {0, 2}, {0, 5}, // UR, UF, UL, UB
    {1, 3}, {1, 4}, {1, 2}, {1, 5}, // DR, DF, DL, DB
    {4, 3}, {4, 2}, {5, 2}, {5, 3}  // FR, FL, BL, BR
};

State3x3 State3x3::fromFacelets(const uint8_t colors[54]) {
    State3x3 state;

    // Decode corners
    for (int i = 0; i < 8; ++i) {
        uint8_t c0 = colors[cornerFaces[i][0]];
        uint8_t c1 = colors[cornerFaces[i][1]];
        uint8_t c2 = colors[cornerFaces[i][2]];

        int piece = -1, orient = 0;
        for (int p = 0; p < 8; ++p) {
            const uint8_t* ref = cornerColorsRef[p];
            if (c0 == ref[0] && c1 == ref[1] && c2 == ref[2]) {
                piece = p; orient = 0; break;
            } else if (c1 == ref[0] && c2 == ref[1] && c0 == ref[2]) {
                piece = p; orient = 1; break;
            } else if (c2 == ref[0] && c0 == ref[1] && c1 == ref[2]) {
                piece = p; orient = 2; break;
            }
        }
        if (piece != -1) {
            state.cp[i] = static_cast<uint8_t>(piece);
            state.co[i] = static_cast<uint8_t>(orient);
        }
    }

    // Decode edges
    for (int i = 0; i < 12; ++i) {
        uint8_t e0 = colors[edgeFaces[i][0]];
        uint8_t e1 = colors[edgeFaces[i][1]];

        int piece = -1, orient = 0;
        for (int p = 0; p < 12; ++p) {
            const uint8_t* ref = edgeColorsRef[p];
            if (e0 == ref[0] && e1 == ref[1]) {
                piece = p; orient = 0; break;
            } else if (e1 == ref[0] && e0 == ref[1]) {
                piece = p; orient = 1; break;
            }
        }
        if (piece != -1) {
            state.ep[i] = static_cast<uint8_t>(piece);
            state.eo[i] = static_cast<uint8_t>(orient);
        }
    }

    return state;
}
