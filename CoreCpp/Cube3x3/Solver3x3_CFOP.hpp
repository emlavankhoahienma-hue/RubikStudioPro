#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube3x3/State3x3.hpp"

// Speedcubing CFOP Solver (Fridrich Method):
// Phase 1 (Cross): IDA* solving 4 bottom edges (DR, DF, DL, DB) in <= 8 moves.
// Phase 2 (F2L): Beam search (beam width 8) solving the 4 corner-edge slots.
// Phase 3 (OLL): Pattern matching against 57 standard OLL algorithms.
// Phase 4 (PLL): Pattern matching against 21 standard PLL algorithms + AUF.
class Solver3x3_CFOP {
public:
    static SolveResult solve(const uint8_t colors[54]);
    static SolveResult solveState(const State3x3& state);
};
