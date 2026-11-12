#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube3x3/State3x3.hpp"

// Kociemba Two-Phase Algorithm for 3x3:
// Phase 1: Search into subgroup G1 = <U, D, R2, L2, F2, B2>
//          using Twist (2187), Flip (2048), and UDSlice (495) pruning tables.
// Phase 2: Solve in G1 using CornerPerm (40320), Phase2EdgePerm (40320),
//          and UDSliceSorted (24) pruning tables.
// Total solution <= 20 moves.
class Solver3x3_Min2Phase {
public:
    static void initTables();
    static SolveResult solve(const uint8_t colors[54]);
    static SolveResult solveState(const State3x3& state);
};
