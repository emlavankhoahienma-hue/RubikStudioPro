#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "Common/MoveNotation.hpp"

// Standard 3x3 Rubik's cube state representation:
// Corners (8):
// 0: URF, 1: UFL, 2: ULB, 3: UBR, 4: DFR, 5: DLF, 6: DBL, 7: DRB
// Edges (12):
// 0: UR, 1: UF, 2: UL, 3: UB, 4: DR, 5: DF, 6: DL, 7: DB, 8: FR, 9: FL, 10: BL, 11: BR
class State3x3 {
public:
    std::array<uint8_t, 8> cp;   // Corner permutation 0..7
    std::array<uint8_t, 8> co;   // Corner orientation 0..2
    std::array<uint8_t, 12> ep;  // Edge permutation 0..11
    std::array<uint8_t, 12> eo;  // Edge orientation 0..1

    State3x3();

    bool isSolved() const;

    // Checks 4 mathematical conditions:
    // 1. Valid permutation of corners and edges
    // 2. sum(co) % 3 == 0
    // 3. sum(eo) % 2 == 0
    // 4. cornerParity == edgeParity
    bool isValid() const;

    void applyMove(const Move& m);
    void applyMove(uint8_t axis, uint8_t power, uint8_t sliceMask = 0x01);

    // Coordinate encoders for Kociemba Two-Phase
    uint16_t getTwist() const;           // 0..2186 (3^7 - 1)
    void setTwist(uint16_t twist);

    uint16_t getFlip() const;            // 0..2047 (2^11 - 1)
    void setFlip(uint16_t flip);

    uint16_t getUDSlice() const;         // 0..494 (12 choose 4 - 1)
    void setUDSlice(uint16_t slice);

    uint16_t getCornerPerm() const;      // 0..40319 (8! - 1)
    void setCornerPerm(uint16_t cpCoord);

    uint16_t getPhase2EdgePerm() const;  // 0..40319 (8! - 1 for 8 edges in U/D layers)
    uint16_t getUDSliceSorted() const;   // 0..23 (4! - 1 for slice edges permutation)

    // Convert from 54 facelets array (0..5: U, D, L, R, F, B)
    static State3x3 fromFacelets(const uint8_t colors[54]);
    static bool validateFacelets(const uint8_t colors[54]);
};
