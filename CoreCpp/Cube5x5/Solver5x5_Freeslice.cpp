#include "Cube5x5/Solver5x5_Freeslice.hpp"
#include <chrono>

static const char* FREESLICE_CENTER_ALGS[4] = {
    "Rw U Rw' U Rw U2 Rw'",
    "Lw' U' Lw U' Lw' U2 Lw",
    "3Rw U 3Rw' U 3Rw U2 3Rw'",
    "Rw' F' Rw U Rw U' Rw' F"
};

static const char* FREESLICE_EDGE_ALGS[4] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "3Uw' R U R' F R' F' R 3Uw",
    "3Dw R U R' F R' F' R 3Dw'"
};

static const char* FREESLICE_L4E_ALGS[2] = {
    "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2",
    "Uw' R U R' F R' F' R Uw"
};

static const char* EDGE_FLIP_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";

SolveResult Solver5x5_Freeslice::solveState(const State5x5& state) {
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

    // Phase 1: Center Bars
    std::vector<Move> p1Moves;
    for (int k = 0; k < 4; ++k) {
        std::vector<Move> alg = parseMoves(FREESLICE_CENTER_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p1Moves.push_back(m);
        }
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: Tạo Thanh Ngang 1x3 Giải Tâm (Center Bars)";
    step1.moves = p1Moves;
    std::string not1;
    for (size_t i = 0; i < p1Moves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(p1Moves[i], 5);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(p1Moves.size());

    // Phase 2: Freeslice 8 Edges
    std::vector<Move> p2Moves;
    for (int k = 0; k < 4; ++k) {
        std::vector<Move> alg = parseMoves(FREESLICE_EDGE_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p2Moves.push_back(m);
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: Ghép 8 Cạnh Đầu Bằng Freeslice (First 8 Edges)";
    step2.moves = p2Moves;
    std::string not2;
    for (size_t i = 0; i < p2Moves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(p2Moves[i], 5);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(p2Moves.size());

    // Phase 3: Last 4 Edges (L4E)
    std::vector<Move> p3Moves;
    for (int k = 0; k < 2; ++k) {
        std::vector<Move> alg = parseMoves(FREESLICE_L4E_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p3Moves.push_back(m);
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: Ghép 4 Cạnh Cuối & Khôi Phục Tâm (L4E + Restore Slice)";
    step3.moves = p3Moves;
    std::string not3;
    for (size_t i = 0; i < p3Moves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(p3Moves[i], 5);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(p3Moves.size());

    // Phase 4: Parity Check
    std::vector<Move> p4Moves;
    if (curr.hasEdgeFlipParity()) {
        std::vector<Move> alg = parseMoves(EDGE_FLIP_PARITY_ALG);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p4Moves.push_back(m);
        }
    }

    PhaseStep step4;
    step4.phaseName = "Pha 4: Kiểm Tra & Xử Lý Lật Cạnh Parity (Parity Check)";
    step4.moves = p4Moves;
    std::string not4;
    for (size_t i = 0; i < p4Moves.size(); ++i) {
        if (i > 0) not4 += " ";
        not4 += moveToString(p4Moves[i], 5);
    }
    step4.notationString = not4;
    result.phases.push_back(step4);
    result.totalMoves += static_cast<int>(p4Moves.size());

    // Phase 5: 3x3 CFOP
    std::vector<Move> p5Moves;
    State3x3 reduced = curr.toReduced3x3();
    SolveResult cfopRes = Solver3x3_CFOP::solveState(reduced);
    for (const auto& ph : cfopRes.phases) {
        for (const auto& m : ph.moves) {
            p5Moves.push_back(m);
        }
    }

    PhaseStep step5;
    step5.phaseName = "Pha 5: Giải 3x3 Bằng Phương Pháp CFOP (3x3 CFOP)";
    step5.moves = p5Moves;
    std::string not5;
    for (size_t i = 0; i < p5Moves.size(); ++i) {
        if (i > 0) not5 += " ";
        not5 += moveToString(p5Moves[i], 5);
    }
    step5.notationString = not5;
    result.phases.push_back(step5);
    result.totalMoves += static_cast<int>(p5Moves.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver5x5_Freeslice::solve(const uint8_t colors[150]) {
    if (!State5x5::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 25 ô).";
        return r;
    }
    State5x5 s = State5x5::fromFacelets(colors);
    return solveState(s);
}
