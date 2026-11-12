#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube2x2/State2x2.hpp"

// Speedcubing 2x2 Solver using Ortega / CLL method:
// Phase 1: First Face (IDDFS depth <= 5)
// Phase 2: OLL (7 standard cases)
// Phase 3: PBL (5 standard cases: adjacent/diagonal swaps)
class Solver2x2_OrtegaCLL {
public:
    static SolveResult solve(const uint8_t colors[24]);
    static SolveResult solveState(const State2x2& state);
};
