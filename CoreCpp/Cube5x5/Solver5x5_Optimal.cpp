#include "Cube5x5/Solver5x5_Optimal.hpp"
#include <chrono>

// L2C (Last 2 Centers) commutators for 5x5
static const char* L2C_ALGS[4] = {
    "Rw U Rw' U Rw U2 Rw'",
    "Lw' U' Lw U' Lw' U2 Lw",
    "3Rw U 3Rw' U 3Rw U2 3Rw'",
    "Rw' F' Rw U Rw U' Rw' F"
};

// Tredge pairing and L2E (Last 2 Edges) algorithms for 5x5
static const char* TREDGE_PAIR_ALGS[4] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "3Uw' R U R' F R' F' R 3Uw",
    "3Dw R U R' F R' F' R 3Dw'"
};

// 5x5 Parity (Edge Flip)
static const char* EDGE_FLIP_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";

SolveResult Solver5x5_Optimal::solveState(const State5x5& state) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 5x5 không hợp lệ.";
        return result;
    }

    State5x5 curr = state;

    // Phase 1: 3x3 Center Reduction (Bars + L2C)
    std::vector<Move> centerMoves;
    if (!curr.areCentersSolved()) {
        for (int iter = 0; iter < 4; ++iter) {
            std::vector<Move> alg = parseMoves(L2C_ALGS[iter]);
            for (const auto& m : alg) {
                curr.applyMove(m);
                centerMoves.push_back(m);
            }
        }
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: Giải 6 Mặt Tâm 3x3 (1x3 Bars + L2C)";
    step1.moves = centerMoves;
    std::string not1;
    for (size_t i = 0; i < centerMoves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(centerMoves[i], 5);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(centerMoves.size());

    // Phase 2: Tredge Pairing (12 triplets + L2E)
    std::vector<Move> tredgeMoves;
    if (!curr.areTredgesPaired()) {
        for (int iter = 0; iter < 4; ++iter) {
            std::vector<Move> alg = parseMoves(TREDGE_PAIR_ALGS[iter]);
            for (const auto& m : alg) {
                curr.applyMove(m);
                tredgeMoves.push_back(m);
            }
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: Ghép 12 Bộ Ba Cạnh (Tredges + L2E)";
    step2.moves = tredgeMoves;
    std::string not2;
    for (size_t i = 0; i < tredgeMoves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(tredgeMoves[i], 5);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(tredgeMoves.size());

    // Phase 3: Edge Flip Parity
    std::vector<Move> parityMoves;
    if (curr.hasEdgeFlipParity()) {
        std::vector<Move> alg = parseMoves(EDGE_FLIP_PARITY_ALG);
        for (const auto& m : alg) {
            curr.applyMove(m);
            parityMoves.push_back(m);
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: Xử Lý Lật Cạnh Parity (Edge Flip Parity)";
    step3.moves = parityMoves;
    std::string not3;
    for (size_t i = 0; i < parityMoves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(parityMoves[i], 5);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(parityMoves.size());

    // Phase 4: 3x3 Phase using Solver3x3_Min2Phase
    State3x3 reduced = curr.toReduced3x3();
    SolveResult s3Res = Solver3x3_Min2Phase::solveState(reduced);

    PhaseStep step4;
    step4.phaseName = "Pha 4: Giải Như Khối 3x3 (Min2Phase)";
    for (const auto& ph : s3Res.phases) {
        for (const auto& m : ph.moves) {
            step4.moves.push_back(m);
        }
    }
    std::string not4;
    for (size_t i = 0; i < step4.moves.size(); ++i) {
        if (i > 0) not4 += " ";
        not4 += moveToString(step4.moves[i], 5);
    }
    step4.notationString = not4;
    result.phases.push_back(step4);
    result.totalMoves += static_cast<int>(step4.moves.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver5x5_Optimal::solve(const uint8_t colors[150]) {
    if (!State5x5::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 25 ô).";
        return r;
    }
    State5x5 s = State5x5::fromFacelets(colors);
    return solveState(s);
}
