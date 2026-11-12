#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube3x3/State3x3.hpp"

// Speedcubing Roux Solver:
// Phase 1 (First Block 1x2x3 on Left): IDA* solving DL, BL, FL edges and DBL, DLF corners.
// Phase 2 (Second Block 1x2x3 on Right): IDA* solving DR, BR, FR edges and DRB, DFR corners.
// Phase 3 (CMLL): Pattern matching solving the 4 top corners.
// Phase 4 (LSE): Last Six Edges solved using strictly <M, U> slice moves.
class Solver3x3_Roux {
public:
    static SolveResult solve(const uint8_t colors[54]);
    static SolveResult solveState(const State3x3& state);
};
