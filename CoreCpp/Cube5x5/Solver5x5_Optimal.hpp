#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube5x5/State5x5.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"

// Optimal Reduction Solver for 5x5:
// Phase 1: 3x3 Center Reduction (1x3 Bars + L2C Commutators).
// Phase 2: Tredge Pairing (12 edge triplets + L2E resolution).
// Phase 3: Edge Flip Parity Resolution.
// Phase 4: 3x3 Stage using Solver3x3_Min2Phase.
class Solver5x5_Optimal {
public:
    static void initTables();
    static SolveResult solve(const uint8_t colors[150]);
    static SolveResult solveState(const State5x5& state);
};
