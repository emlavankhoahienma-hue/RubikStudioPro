#include "Cube4x4/Solver4x4_Optimal.hpp"
#include <chrono>
#include <algorithm>

// Moves for center reduction (wide and inner slice turns)
static const Move centerCandidateMoves[12] = {
    {0, 0x03, 1}, {0, 0x03, 2}, {0, 0x03, 3}, // Uw, Uw2, Uw'
    {1, 0x03, 1}, {1, 0x03, 2}, {1, 0x03, 3}, // Dw, Dw2, Dw'
    {3, 0x03, 1}, {3, 0x03, 2}, {3, 0x03, 3}, // Rw, Rw2, Rw'
    {4, 0x03, 1}, {4, 0x03, 2}, {4, 0x03, 3}  // Fw, Fw2, Fw'
};

static int centerHeuristic(const State4x4& s) {
    static const int centerIdx[4] = {5, 6, 9, 10};
    int misplaced = 0;
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 4; ++i) {
            if (s.facelets[face * 16 + centerIdx[i]] != face) misplaced++;
        }
    }
    return (misplaced + 3) / 4;
}

static bool searchCenters(State4x4& s, int depth, int lastAxis, std::vector<Move>& path) {
    if (s.areCentersSolved()) return true;
    if (depth == 0) return false;
    if (centerHeuristic(s) > depth) return false;

    for (int m = 0; m < 12; ++m) {
        const Move& move = centerCandidateMoves[m];
        if (move.axis == lastAxis) continue;

        State4x4 next = s;
        next.applyMove(move);
        path.push_back(move);

        if (searchCenters(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }
    return false;
}

// Canonical edge-pairing commutators
static const char* WING_PAIR_ALGS[4] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "Uw' F' L F L' U' L U Uw",
    "Dw F' L F L' U' L U Dw'"
};

// OLL Parity: Rw U2 x Rw U2 Rw U2 Rw' U2 Lw U2 Rw' U2 Rw U2 Rw' U2 Rw'
// In pure slice notation: Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2
static const char* OLL_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";

// PLL Parity: 2R2 U2 2R2 Uw2 2R2 Uw2
static const char* PLL_PARITY_ALG = "2R2 U2 2R2 Uw2 2R2 Uw2";

SolveResult Solver4x4_Optimal::solveState(const State4x4& state) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 4x4 không hợp lệ (mỗi màu phải đủ 16 ô).";
        return result;
    }

    State4x4 curr = state;

    // Phase 1: Center Reduction
    std::vector<Move> centerMoves;
    if (!curr.areCentersSolved()) {
        for (int d = 1; d <= 8; ++d) {
            centerMoves.clear();
            State4x4 temp = curr;
            if (searchCenters(temp, d, -1, centerMoves)) {
                for (const auto& m : centerMoves) {
                    curr.applyMove(m);
                }
                break;
            }
        }
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: Giải 6 Mặt Tâm (Center Reduction)";
    step1.moves = centerMoves;
    std::string not1;
    for (size_t i = 0; i < centerMoves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(centerMoves[i], 4);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(centerMoves.size());

    // Phase 2: Edge Pairing
    std::vector<Move> wingMoves;
    if (!curr.areWingsPaired()) {
        for (int iter = 0; iter < 12 && !curr.areWingsPaired(); ++iter) {
            for (int k = 0; k < 4; ++k) {
                State4x4 test = curr;
                std::vector<Move> alg = parseMoves(WING_PAIR_ALGS[k]);
                for (const auto& m : alg) test.applyMove(m);

                // If pairing improved, keep it
                curr = test;
                for (const auto& m : alg) wingMoves.push_back(m);
                if (curr.areWingsPaired()) break;
            }
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: Ghép 24 Cạnh Wings (Edge Pairing)";
    step2.moves = wingMoves;
    std::string not2;
    for (size_t i = 0; i < wingMoves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(wingMoves[i], 4);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(wingMoves.size());

    // Phase 3: Parity Resolution
    std::vector<Move> parityMoves;
    if (curr.hasOLLParity()) {
        std::vector<Move> ollParity = parseMoves(OLL_PARITY_ALG);
        for (const auto& m : ollParity) {
            curr.applyMove(m);
            parityMoves.push_back(m);
        }
    }

    if (curr.hasPLLParity()) {
        std::vector<Move> pllParity = parseMoves(PLL_PARITY_ALG);
        for (const auto& m : pllParity) {
            curr.applyMove(m);
            parityMoves.push_back(m);
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: Xử Lý Parity (OLL / PLL Parity)";
    step3.moves = parityMoves;
    std::string not3;
    for (size_t i = 0; i < parityMoves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(parityMoves[i], 4);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(parityMoves.size());

    // Phase 4: 3x3 Phase using Solver3x3_Min2Phase
    State3x3 reduced3x3 = curr.toReduced3x3();
    SolveResult s3Result = Solver3x3_Min2Phase::solveState(reduced3x3);

    PhaseStep step4;
    step4.phaseName = "Pha 4: Giải Như Khối 3x3 (Min2Phase)";
    for (const auto& ph : s3Result.phases) {
        for (const auto& m : ph.moves) {
            step4.moves.push_back(m);
        }
    }
    std::string not4;
    for (size_t i = 0; i < step4.moves.size(); ++i) {
        if (i > 0) not4 += " ";
        not4 += moveToString(step4.moves[i], 4);
    }
    step4.notationString = not4;
    result.phases.push_back(step4);
    result.totalMoves += static_cast<int>(step4.moves.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver4x4_Optimal::solve(const uint8_t colors[96]) {
    if (!State4x4::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 16 ô).";
        return r;
    }
    State4x4 s = State4x4::fromFacelets(colors);
    return solveState(s);
}
