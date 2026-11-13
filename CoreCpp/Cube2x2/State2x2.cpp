#include "Cube2x2/State2x2.hpp"
#include <algorithm>
#include <numeric>

State2x2::State2x2() {
    for (uint8_t i = 0; i < 8; ++i) {
        perm[i] = i;
        orient[i] = 0;
    }
}

bool State2x2::isValid() const {
    uint8_t count[8] = {0};
    uint32_t orientSum = 0;
    for (int i = 0; i < 8; ++i) {
        if (perm[i] > 7) return false;
        if (orient[i] > 2) return false;
        count[perm[i]]++;
        orientSum += orient[i];
    }
    for (int i = 0; i < 8; ++i) {
        if (count[i] != 1) return false;
    }
    return (orientSum % 3) == 0;
}

bool State2x2::isSolved() const {
    for (uint8_t i = 0; i < 8; ++i) {
        if (perm[i] != i || orient[i] != 0) return false;
    }
    return true;
}

void State2x2::applyMove(const Move& m) {
    applyMove(m.axis, m.power);
}

void State2x2::applyMove(uint8_t axis, uint8_t power) {
    if (power == 0 || power > 3) return;

    for (uint8_t p = 0; p < power; ++p) {
        if (axis == 0) { // U move
            // Cyclic permutation: 0 <- 3 <- 2 <- 1 <- 0 (URF, UBR, ULB, UFL)
            uint8_t tp = perm[0];
            uint8_t to = orient[0];
            perm[0] = perm[3]; orient[0] = orient[3];
            perm[3] = perm[2]; orient[3] = orient[2];
            perm[2] = perm[1]; orient[2] = orient[1];
            perm[1] = tp;      orient[1] = to;
        } else if (axis == 3) { // R move
            // Corners: 0(URF), 3(UBR), 6(DRB), 4(DFR)
            // Clockwise looking at R (+X): 3 <- 0, 6 <- 3, 4 <- 6, 0 <- 4
            uint8_t p0 = perm[0], o0 = orient[0];
            uint8_t p3 = perm[3], o3 = orient[3];
            uint8_t p4 = perm[4], o4 = orient[4];
            uint8_t p6 = perm[6], o6 = orient[6];

            perm[3] = p0; orient[3] = static_cast<uint8_t>((o0 + 1) % 3);
            perm[6] = p3; orient[6] = static_cast<uint8_t>((o3 + 2) % 3);
            perm[4] = p6; orient[4] = static_cast<uint8_t>((o6 + 1) % 3);
            perm[0] = p4; orient[0] = static_cast<uint8_t>((o4 + 2) % 3);
        } else if (axis == 4) { // F move
            // Corners: 1(UFL), 0(URF), 4(DFR), 5(DLF)
            // Clockwise: 0 <- 1, 4 <- 0, 5 <- 4, 1 <- 5
            uint8_t p0 = perm[0], o0 = orient[0];
            uint8_t p1 = perm[1], o1 = orient[1];
            uint8_t p4 = perm[4], o4 = orient[4];
            uint8_t p5 = perm[5], o5 = orient[5];

            perm[0] = p1; orient[0] = static_cast<uint8_t>((o1 + 1) % 3);
            perm[4] = p0; orient[4] = static_cast<uint8_t>((o0 + 2) % 3);
            perm[5] = p4; orient[5] = static_cast<uint8_t>((o4 + 1) % 3);
            perm[1] = p5; orient[1] = static_cast<uint8_t>((o5 + 2) % 3);
        }
    }
}

// Corner definitions with facelet indices:
// 0: URF -> U(3),  R(12), F(17)
// 1: UFL -> U(2),  F(16), L(9)
// 2: ULB -> U(0),  L(8),  B(21)
// 3: UBR -> U(1),  B(20), R(13)
// 4: DFR -> D(5),  F(19), R(14)
// 5: DLF -> D(4),  L(11), F(18)
// 6: DRB -> D(7),  R(15), B(22)
// 7: DBL -> D(6),  B(23), L(10)
static const int cornerFacelets[8][3] = {
    {3, 12, 17}, // URF
    {2, 16, 9},  // UFL
    {0, 8, 21},  // ULB
    {1, 20, 13}, // UBR
    {5, 19, 14}, // DFR
    {4, 11, 18}, // DLF
    {7, 15, 22}, // DRB
    {6, 23, 10}  // DBL
};

State2x2 State2x2::fromFacelets(const uint8_t colors[24]) {
    State2x2 state;

    // Standard colors: U=0, D=1, L=2, R=3, F=4, B=5
    // Corner pieces standard color triples:
    static const uint8_t cornerColors[8][3] = {
        {0, 3, 4}, // URF: U, R, F
        {0, 4, 2}, // UFL: U, F, L
        {0, 2, 5}, // ULB: U, L, B
        {0, 5, 3}, // UBR: U, B, R
        {1, 4, 3}, // DFR: D, F, R
        {1, 2, 4}, // DLF: D, L, F
        {1, 3, 5}, // DRB: D, R, B
        {1, 5, 2}  // DBL: D, B, L
    };

    for (int i = 0; i < 8; ++i) {
        uint8_t c0 = colors[cornerFacelets[i][0]];
        uint8_t c1 = colors[cornerFacelets[i][1]];
        uint8_t c2 = colors[cornerFacelets[i][2]];

        int foundPiece = -1;
        int foundOrient = -1;

        for (int p = 0; p < 8; ++p) {
            const uint8_t* ref = cornerColors[p];
            if (c0 == ref[0] && c1 == ref[1] && c2 == ref[2]) {
                foundPiece = p; foundOrient = 0; break;
            } else if (c1 == ref[0] && c2 == ref[1] && c0 == ref[2]) {
                foundPiece = p; foundOrient = 1; break;
            } else if (c2 == ref[0] && c0 == ref[1] && c1 == ref[2]) {
                foundPiece = p; foundOrient = 2; break;
            }
        }

        if (foundPiece != -1) {
            state.perm[i] = static_cast<uint8_t>(foundPiece);
            state.orient[i] = static_cast<uint8_t>(foundOrient);
        } else {
            state.perm[i] = static_cast<uint8_t>(i);
            state.orient[i] = 0;
        }
    }

    return state;
}

// Lehmer code for 7 elements (corners 0..6), range 0..5039
uint16_t State2x2::getPermCoord() const {
    uint16_t coord = 0;
    static const uint16_t factorials[7] = {720, 120, 24, 6, 2, 1, 1};

    for (int i = 0; i < 6; ++i) {
        int count = 0;
        for (int j = i + 1; j < 7; ++j) {
            if (perm[j] < perm[i]) count++;
        }
        coord += count * factorials[i];
    }
    return coord;
}

void State2x2::setPermCoord(uint16_t coord) {
    static const uint16_t factorials[7] = {720, 120, 24, 6, 2, 1, 1};
    std::vector<uint8_t> available = {0, 1, 2, 3, 4, 5, 6};

    for (int i = 0; i < 6; ++i) {
        int idx = coord / factorials[i];
        coord %= factorials[i];
        perm[i] = available[idx];
        available.erase(available.begin() + idx);
    }
    perm[6] = available[0];
    perm[7] = 7;
}

// Orientation coordinate for first 6 corners (range 0..728 = 3^6 - 1)
uint16_t State2x2::getOrientCoord() const {
    uint16_t coord = 0;
    for (int i = 0; i < 6; ++i) {
        coord = coord * 3 + orient[i];
    }
    return coord;
}

void State2x2::setOrientCoord(uint16_t coord) {
    uint32_t sum = 0;
    for (int i = 5; i >= 0; --i) {
        orient[i] = coord % 3;
        sum += orient[i];
        coord /= 3;
    }
    orient[6] = static_cast<uint8_t>((3 - (sum % 3)) % 3);
    orient[7] = 0;
}
