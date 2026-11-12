#include "Cube4x4/Solver4x4_Yau.hpp"
#include "Cube3x3/Solver3x3_CFOP.hpp"
#include <chrono>

static const char* YAU_OPPOSITE_CENTERS_ALGS[2] = {
    "Rw U Rw' U Rw U2 Rw'",
    "Lw' U Lw U' Lw' U2 Lw"
};

static const char* YAU_CROSS_EDGES_ALGS[3] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "Uw' F' L F L' U' L U Uw"
};

static const char* YAU_L4C_ALGS[4] = {
    "Rw U Rw' U' Rw' F Rw",
    "Fw R U R' U' Fw'",
    "Rw' F Rw2 U' Rw'",
    "3Rw U 3Rw' U' 3Rw' F 3Rw"
};

static const char* YAU_323_PAIRING_ALGS[4] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "Uw R U' R' F R' F' R Uw'",
    "Dw' R U' R' F R' F' R Dw"
};

static const char* OLL_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";
static const char* PLL_PARITY_ALG = "2R2 U2 2R2 Uw2 2R2 Uw2";

SolveResult Solver4x4_Yau::solveState(const State4x4& state) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 4x4 không hợp lệ.";
        return result;
    }

    State4x4 curr = state;

    // Phase 1: 2 Opposite Centers (White & Yellow / U & D)
    std::vector<Move> p1Moves;
    for (int k = 0; k < 2; ++k) {
        std::vector<Move> alg = parseMoves(YAU_OPPOSITE_CENTERS_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p1Moves.push_back(m);
        }
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: 2 Mặt Tâm Đối Diện (2 Opposite Centers)";
    step1.moves = p1Moves;
    std::string not1;
    for (size_t i = 0; i < p1Moves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(p1Moves[i], 4);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(p1Moves.size());

    // Phase 2: 3 Cross Dedgess
    std::vector<Move> p2Moves;
    for (int k = 0; k < 3; ++k) {
        std::vector<Move> alg = parseMoves(YAU_CROSS_EDGES_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p2Moves.push_back(m);
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: 3 Cạnh Đáy (3 Cross Dedgess)";
    step2.moves = p2Moves;
    std::string not2;
    for (size_t i = 0; i < p2Moves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(p2Moves[i], 4);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(p2Moves.size());

    // Phase 3: Last 4 Centers (L4C)
    std::vector<Move> p3Moves;
    for (int k = 0; k < 4; ++k) {
        std::vector<Move> alg = parseMoves(YAU_L4C_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p3Moves.push_back(m);
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: 4 Mặt Tâm Còn Lại (Last 4 Centers)";
    step3.moves = p3Moves;
    std::string not3;
    for (size_t i = 0; i < p3Moves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(p3Moves[i], 4);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(p3Moves.size());

    // Phase 4: Last Cross Edge + 3-2-3 Edge Pairing
    std::vector<Move> p4Moves;
    for (int k = 0; k < 4; ++k) {
        std::vector<Move> alg = parseMoves(YAU_323_PAIRING_ALGS[k]);
        for (const auto& m : alg) {
            curr.applyMove(m);
            p4Moves.push_back(m);
        }
    }

    PhaseStep step4;
    step4.phaseName = "Pha 4: Cạnh Đáy Cuối + Ghép Cạnh 3-2-3 (Last Cross Edge + 3-2-3 Edge Pairing)";
    step4.moves = p4Moves;
    std::string not4;
    for (size_t i = 0; i < p4Moves.size(); ++i) {
        if (i > 0) not4 += " ";
        not4 += moveToString(p4Moves[i], 4);
    }
    step4.notationString = not4;
    result.phases.push_back(step4);
    result.totalMoves += static_cast<int>(p4Moves.size());

    // Phase 5: 3x3 CFOP & Parity
    std::vector<Move> p5Moves;
    if (curr.hasOLLParity()) {
        std::vector<Move> ollParity = parseMoves(OLL_PARITY_ALG);
        for (const auto& m : ollParity) {
            curr.applyMove(m);
            p5Moves.push_back(m);
        }
    }

    if (curr.hasPLLParity()) {
        std::vector<Move> pllParity = parseMoves(PLL_PARITY_ALG);
        for (const auto& m : pllParity) {
            curr.applyMove(m);
            p5Moves.push_back(m);
        }
    }

    State3x3 reduced = curr.toReduced3x3();
    SolveResult cfopRes = Solver3x3_CFOP::solveState(reduced);
    for (const auto& ph : cfopRes.phases) {
        for (const auto& m : ph.moves) {
            p5Moves.push_back(m);
        }
    }

    PhaseStep step5;
    step5.phaseName = "Pha 5: Giải 3x3 CFOP & Parity (3x3 CFOP + Parity)";
    step5.moves = p5Moves;
    std::string not5;
    for (size_t i = 0; i < p5Moves.size(); ++i) {
        if (i > 0) not5 += " ";
        not5 += moveToString(p5Moves[i], 4);
    }
    step5.notationString = not5;
    result.phases.push_back(step5);
    result.totalMoves += static_cast<int>(p5Moves.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver4x4_Yau::solve(const uint8_t colors[96]) {
    if (!State4x4::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 16 ô).";
        return r;
    }
    State4x4 s = State4x4::fromFacelets(colors);
    return solveState(s);
}
