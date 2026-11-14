#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube4x4/State4x4.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"

// Optimal Reduction Solver for 4x4:
// Phase 1: Center Reduction (24 centers solved into standard BOY arrangement).
// Phase 2: Edge Pairing (24 wings paired into 12 dedges).
// Phase 3: Parity Resolution (OLL parity and PLL parity correction).
// Phase 4: 3x3 Phase using Solver3x3_Min2Phase.
class Solver4x4_Optimal {
public:
    static void initTables();
    static SolveResult solve(const uint8_t colors[96]);
    static SolveResult solveState(const State4x4& state);
};
