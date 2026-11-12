#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "Common/MoveNotation.hpp"
#include "Cube3x3/State3x3.hpp"

// 4x4 Rubik's Cube state managing 96 facelets (24 centers, 24 wings, 8 corners).
// Faces: 0:U, 1:D, 2:L, 3:R, 4:F, 5:B
// Each face has 16 stickers indexed [0..15] in row-major order:
// [ 0  1  2  3 ]
// [ 4  5  6  7 ]
// [ 8  9 10 11 ]
// [12 13 14 15 ]
// Centers: {5, 6, 9, 10} on each face.
// Outer turns (sliceMask 0x01), wide turns (sliceMask 0x03), inner slice (sliceMask 0x02).
class State4x4 {
public:
    std::array<uint8_t, 96> facelets; // 0..5 (U, D, L, R, F, B)

    State4x4();

    bool isSolved() const;
    bool isValid() const;

    void applyMove(const Move& m);
    void applyMove(uint8_t axis, uint8_t power, uint8_t sliceMask);

    // Reduction helpers
    bool areCentersSolved() const;
    bool areWingsPaired() const;

    // Check OLL & PLL parity
    bool hasOLLParity() const;
    bool hasPLLParity() const;

    // Convert reduced 4x4 into equivalent 3x3 state
    State3x3 toReduced3x3() const;

    static State4x4 fromFacelets(const uint8_t colors[96]);
    static bool validateFacelets(const uint8_t colors[96]);
};
