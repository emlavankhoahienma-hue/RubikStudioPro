#pragma once
#include <cstdint>
#include "Common/SolverTypes.hpp"
#include "Cube2x2/State2x2.hpp"

// Optimal 2x2 Solver using IDA* search with precomputed BFS pruning tables
// for Orientation (729 states) and Permutation (5040 states).
// Guaranteed optimal solution <= 11 HTM moves.
class Solver2x2_Optimal {
public:
    static void initTables();
    static SolveResult solve(const uint8_t colors[24]);
    static SolveResult solveState(const State2x2& state);
};
