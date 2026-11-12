#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "Common/MoveNotation.hpp"
#include "Cube3x3/State3x3.hpp"

// 5x5 Rubik's Cube state managing 150 facelets:
// - 6 Fixed Centers
// - 24 T-Centers
// - 24 X-Centers
// - 12 Midges
// - 24 Wings
// - 8 Corners
// Faces: 0:U, 1:D, 2:L, 3:R, 4:F, 5:B
// Each face has 25 stickers [0..24] in 5x5 row-major order:
// [ 0  1  2  3  4 ]
// [ 5  6  7  8  9 ]
// [10 11 12 13 14 ]
// [15 16 17 18 19 ]
// [20 21 22 23 24 ]
// Fixed center at index 12.
// Centers: rows 1..3, cols 1..3 (9 facelets per face).
class State5x5 {
public:
    std::array<uint8_t, 150> facelets; // 0..5

    State5x5();

    bool isSolved() const;
    bool isValid() const;

    void applyMove(const Move& m);
    void applyMove(uint8_t axis, uint8_t power, uint8_t sliceMask);

    // Reduction status helpers
    bool areCentersSolved() const;
    bool areTredgesPaired() const;
    bool hasEdgeFlipParity() const;

    // Convert reduced 5x5 into equivalent 3x3 state
    State3x3 toReduced3x3() const;

    static State5x5 fromFacelets(const uint8_t colors[150]);
    static bool validateFacelets(const uint8_t colors[150]);
};
