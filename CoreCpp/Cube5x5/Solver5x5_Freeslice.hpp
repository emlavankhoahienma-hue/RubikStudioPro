#pragma once
#include <cstdint>
#include <vector>
#include "Common/SolverTypes.hpp"
#include "Cube5x5/State5x5.hpp"
#include "Cube3x3/Solver3x3_CFOP.hpp"

// Speedcubing 5x5 Solver using Freeslice Method:
// Phase 1: Center Bars (Tạo các thanh ngang 1x3 giải 6 mặt tâm).
// Phase 2: Freeslice 8 Edges (Ghép 8 cạnh đầu tiên bằng kỹ thuật Freeslice).
// Phase 3: Last 4 Edges (L4E) + Slice Alignment (Ghép 4 cạnh cuối và khôi phục tâm).
// Phase 4: Parity Check (Kiểm tra & lật cạnh).
// Phase 5: 3x3 CFOP (Giải hoàn tất 3x3).
class Solver5x5_Freeslice {
public:
    static SolveResult solve(const uint8_t colors[150]);
    static SolveResult solveState(const State5x5& state);
};
