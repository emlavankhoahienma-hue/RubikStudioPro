#include "Cube4x4/Solver4x4_Yau.hpp"
#include "Cube3x3/Solver3x3_CFOP.hpp"
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>

static const char* YAU_SETUPS[] = {
    "", "U", "U'", "U2", "D", "D'", "D2", "L", "L'", "L2", "R", "R'", "R2", "F", "F'", "F2", "B", "B'", "B2"
};

static const char* YAU_INVERSE_SETUPS[] = {
    "", "U'", "U", "U2", "D'", "D", "D2", "L'", "L", "L2", "R'", "R", "R2", "F'", "F", "F2", "B'", "B", "B2"
};

static const char* YAU_CENTER_COMMS[] = {
    "Rw U Rw'", "Rw U' Rw'", "Rw U2 Rw'", "Rw' U Rw", "Rw' U' Rw", "Rw' U2 Rw",
    "Lw U Lw'", "Lw U' Lw'", "Lw U2 Lw'", "Lw' U Lw", "Lw' U' Lw", "Lw' U2 Lw",
    "Fw U Fw'", "Fw U' Fw'", "Fw U2 Fw'", "Fw' U Fw", "Fw' U' Fw", "Fw' U2 Fw",
    "Bw U Bw'", "Bw U' Bw'", "Bw U2 Bw'", "Bw' U Bw", "Bw' U' Bw", "Bw' U2 Bw"
};

static const char* YAU_WING_COMMS[] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "Uw' F' L F L' U' L U Uw",
    "Dw F' L F L' U' L U Dw'"
};

static const char* OLL_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";
static const char* PLL_PARITY_ALG = "2R2 U2 2R2 Uw2 2R2 Uw2";

static int countMisplacedCentersYau(const State4x4& s, int faceA = 0, int faceB = 1) {
    static const int centerIdx[4] = {5, 6, 9, 10};
    int misplaced = 0;
    for (int i = 0; i < 4; ++i) {
        if (s.facelets[faceA * 16 + centerIdx[i]] != faceA) misplaced++;
        if (s.facelets[faceB * 16 + centerIdx[i]] != faceB) misplaced++;
    }
    return misplaced;
}

static int countTotalMisplacedCenters(const State4x4& s) {
    static const int centerIdx[4] = {5, 6, 9, 10};
    int misplaced = 0;
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 4; ++i) {
            if (s.facelets[face * 16 + centerIdx[i]] != face) misplaced++;
        }
    }
    return misplaced;
}

static int countPairedWingsYau(const State4x4& s) {
    struct WingPairInfo { int w1_1, w1_2, w2_1, w2_2; };
    static const WingPairInfo wingPairs[12] = {
        {0*16 + 7,  3*16 + 1,  0*16 + 11, 3*16 + 2},
        {0*16 + 13, 4*16 + 1,  0*16 + 14, 4*16 + 2},
        {0*16 + 4,  2*16 + 1,  0*16 + 8,  2*16 + 2},
        {0*16 + 1,  5*16 + 2,  0*16 + 2,  5*16 + 1},
        {1*16 + 7,  3*16 + 14, 1*16 + 11, 3*16 + 13},
        {1*16 + 1,  4*16 + 13, 1*16 + 2,  4*16 + 14},
        {1*16 + 4,  2*16 + 13, 1*16 + 8,  2*16 + 14},
        {1*16 + 13, 5*16 + 14, 1*16 + 14, 5*16 + 13},
        {4*16 + 7,  3*16 + 4,  4*16 + 11, 3*16 + 8},
        {4*16 + 4,  2*16 + 7,  4*16 + 8,  2*16 + 11},
        {5*16 + 7,  2*16 + 4,  5*16 + 11, 2*16 + 8},
        {5*16 + 4,  3*16 + 7,  5*16 + 8,  3*16 + 11}
    };
    int paired = 0;
    for (int i = 0; i < 12; ++i) {
        if (s.facelets[wingPairs[i].w1_1] == s.facelets[wingPairs[i].w2_1] &&
            s.facelets[wingPairs[i].w1_2] == s.facelets[wingPairs[i].w2_2]) {
            paired++;
        }
    }
    return paired;
}

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
    int numSetups = sizeof(YAU_SETUPS)/sizeof(YAU_SETUPS[0]);
    int numComms = sizeof(YAU_CENTER_COMMS)/sizeof(YAU_CENTER_COMMS[0]);
    int numWComms = sizeof(YAU_WING_COMMS)/sizeof(YAU_WING_COMMS[0]);

    // Phase 1: 2 Opposite Centers (White & Yellow / U & D)
    std::vector<Move> p1Moves;
    for (int step = 0; step < 12; ++step) {
        int bestMis = countMisplacedCentersYau(curr, 0, 1);
        if (bestMis == 0) break;
        std::string bestSeq = "";
        State4x4 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int j = 0; j < numComms; ++j) {
                std::string seq = "";
                if (YAU_SETUPS[i][0] != '\0') seq += std::string(YAU_SETUPS[i]) + " ";
                seq += YAU_CENTER_COMMS[j];
                if (YAU_INVERSE_SETUPS[i][0] != '\0') seq += " " + std::string(YAU_INVERSE_SETUPS[i]);

                State4x4 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                int mis = countMisplacedCentersYau(test, 0, 1);
                if (mis < bestMis) {
                    bestMis = mis;
                    bestSeq = seq;
                    bestState = test;
                }
            }
        }
        if (bestSeq.empty()) break;
        std::vector<Move> mvs = parseMoves(bestSeq);
        for (const auto& m : mvs) p1Moves.push_back(m);
        curr = bestState;
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
    for (int step = 0; step < 3; ++step) {
        int bestPaired = countPairedWingsYau(curr);
        std::string bestSeq = "";
        State4x4 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int w = 0; w < numWComms; ++w) {
                std::string seq = "";
                if (YAU_SETUPS[i][0] != '\0') seq += std::string(YAU_SETUPS[i]) + " ";
                seq += YAU_WING_COMMS[w];
                if (YAU_INVERSE_SETUPS[i][0] != '\0') seq += " " + std::string(YAU_INVERSE_SETUPS[i]);

                State4x4 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                if (countMisplacedCentersYau(test, 0, 1) == 0) {
                    int p = countPairedWingsYau(test);
                    if (p > bestPaired) {
                        bestPaired = p;
                        bestSeq = seq;
                        bestState = test;
                    }
                }
            }
        }
        if (bestSeq.empty()) break;
        std::vector<Move> mvs = parseMoves(bestSeq);
        for (const auto& m : mvs) p2Moves.push_back(m);
        curr = bestState;
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
    for (int step = 0; step < 16; ++step) {
        int bestMis = countTotalMisplacedCenters(curr);
        if (bestMis == 0) break;
        std::string bestSeq = "";
        State4x4 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int j = 0; j < numComms; ++j) {
                std::string seq = "";
                if (YAU_SETUPS[i][0] != '\0') seq += std::string(YAU_SETUPS[i]) + " ";
                seq += YAU_CENTER_COMMS[j];
                if (YAU_INVERSE_SETUPS[i][0] != '\0') seq += " " + std::string(YAU_INVERSE_SETUPS[i]);

                State4x4 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                int mis = countTotalMisplacedCenters(test);
                if (mis < bestMis) {
                    bestMis = mis;
                    bestSeq = seq;
                    bestState = test;
                }
            }
        }
        if (bestSeq.empty()) break;
        std::vector<Move> mvs = parseMoves(bestSeq);
        for (const auto& m : mvs) p3Moves.push_back(m);
        curr = bestState;
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
    for (int step = 0; step < 12; ++step) {
        int bestPaired = countPairedWingsYau(curr);
        if (bestPaired == 12) break;
        std::string bestSeq = "";
        State4x4 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int w = 0; w < numWComms; ++w) {
                std::string seq = "";
                if (YAU_SETUPS[i][0] != '\0') seq += std::string(YAU_SETUPS[i]) + " ";
                seq += YAU_WING_COMMS[w];
                if (YAU_INVERSE_SETUPS[i][0] != '\0') seq += " " + std::string(YAU_INVERSE_SETUPS[i]);

                State4x4 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                if (curr.areCentersSolved()) {
                    int p = countPairedWingsYau(test);
                    if (p > bestPaired) {
                        bestPaired = p;
                        bestSeq = seq;
                        bestState = test;
                    }
                }
            }
        }
        if (bestSeq.empty()) break;
        std::vector<Move> mvs = parseMoves(bestSeq);
        for (const auto& m : mvs) p4Moves.push_back(m);
        curr = bestState;
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
