#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube4x4/State4x4.hpp"

// Speedcubing 4x4 Solver using Yau Method:
// Phase 1: 2 Opposite Centers (White & Yellow / U & D)
// Phase 2: 3 Cross Dedgess (e.g. DF, DL, DB)
// Phase 3: Last 4 Centers (F, R, B, L)
// Phase 4: Last Cross Edge + 3-2-3 Edge Pairing
// Phase 5: 3x3 Stage (CFOP with OLL/PLL Parity)
class Solver4x4_Yau {
public:
    static SolveResult solve(const uint8_t colors[96]);
    static SolveResult solveState(const State4x4& state);
};
