#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "Common/MoveNotation.hpp"

// Representation of a 2x2 Rubik's Cube state.
// 8 corners:
// 0: URF, 1: UFL, 2: ULB, 3: UBR, 4: DFR, 5: DLF, 6: DRB, 7: DBL (Fixed)
// perm[i]: which corner is currently at position i (0..7)
// orient[i]: orientation of the corner at position i (0, 1, 2)
// Corner 7 (DBL) is kept fixed at perm[7]=7, orient[7]=0 to eliminate full-cube rotations.
// Allowed moves on the fixed frame: U, R, F (powers 1, 2, 3).
class State2x2 {
public:
    std::array<uint8_t, 8> perm;
    std::array<uint8_t, 8> orient;

    State2x2();

    // Check if the state is physically valid:
    // 1. Valid permutation of 0..7
    // 2. Sum of orientations mod 3 == 0
    bool isValid() const;

    // Check if solved
    bool isSolved() const;

    // Apply a move
    void applyMove(const Move& m);

    // Apply move by axis and power (axis: 0=U, 3=R, 4=F)
    void applyMove(uint8_t axis, uint8_t power);

    // Convert from 24 facelet colors array (0..5: U, D, L, R, F, B)
    static State2x2 fromFacelets(const uint8_t colors[24]);

    // Encode permutation of corners 0..6 (0..5039)
    uint16_t getPermCoord() const;
    void setPermCoord(uint16_t coord);

    // Encode orientation of corners 0..5 (0..728)
    uint16_t getOrientCoord() const;
    void setOrientCoord(uint16_t coord);
};
