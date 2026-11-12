#include "Cube5x5/Solver5x5_Freeslice.hpp"
#include "Cube3x3/Solver3x3_CFOP.hpp"
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>

static const char* FREESLICE_SETUPS[] = {
    "", "U", "U'", "U2", "D", "D'", "D2", "L", "L'", "L2", "R", "R'", "R2", "F", "F'", "F2", "B", "B'", "B2"
};

static const char* FREESLICE_INV_SETUPS[] = {
    "", "U'", "U", "U2", "D'", "D", "D2", "L'", "L", "L2", "R'", "R", "R2", "F'", "F", "F2", "B'", "B", "B2"
};

static const char* FREESLICE_CENTER_COMMS[] = {
    "Rw U Rw'", "Rw U' Rw'", "Rw U2 Rw'", "Rw' U Rw", "Rw' U' Rw", "Rw' U2 Rw",
    "Lw U Lw'", "Lw U' Lw'", "Lw U2 Lw'", "Lw' U Lw", "Lw' U' Lw", "Lw' U2 Lw",
    "Fw U Fw'", "Fw U' Fw'", "Fw U2 Fw'", "Fw' U Fw", "Fw' U' Fw", "Fw' U2 Fw",
    "Bw U Bw'", "Bw U' Bw'", "Bw U2 Bw'", "Bw' U Bw", "Bw' U' Bw", "Bw' U2 Bw",
    "3Rw U 3Rw'", "3Rw U' 3Rw'", "3Rw U2 3Rw'", "3Rw' U 3Rw", "3Rw' U' 3Rw", "3Rw' U2 3Rw"
};

static const char* FREESLICE_TREDGE_COMMS[] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "3Uw' R U R' F R' F' R 3Uw",
    "3Dw R U R' F R' F' R 3Dw'"
};

static const char* EDGE_FLIP_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";

static int countMisplacedCenters5x5Freeslice(const State5x5& s) {
    int misplaced = 0;
    for (int face = 0; face < 6; ++face) {
        uint8_t c = s.facelets[face * 25 + 12];
        for (int r = 1; r <= 3; ++r) {
            for (int col = 1; col <= 3; ++col) {
                if (s.facelets[face * 25 + r * 5 + col] != c) misplaced++;
            }
        }
    }
    return misplaced;
}

static int countPairedTredgesFreeslice(const State5x5& s) {
    struct TredgeInfo {
        int midge_f1, midge_f2, w1_f1, w1_f2, w2_f1, w2_f2;
    };
    static const TredgeInfo tredges[12] = {
        {0*25 + 2, 3*25 + 2,  0*25 + 1, 3*25 + 1,  0*25 + 3, 3*25 + 3},
        {0*25 + 22, 4*25 + 2, 0*25 + 21, 4*25 + 1, 0*25 + 23, 4*25 + 3},
        {0*25 + 10, 2*25 + 2, 0*25 + 5,  2*25 + 1, 0*25 + 15, 2*25 + 3},
        {0*25 + 2,  5*25 + 2, 0*25 + 1,  5*25 + 3, 0*25 + 3,  5*25 + 1},
        {1*25 + 14, 3*25 + 22, 1*25 + 9,  3*25 + 21, 1*25 + 19, 3*25 + 23},
        {1*25 + 2,  4*25 + 22, 1*25 + 1,  4*25 + 23, 1*25 + 3,  4*25 + 21},
        {1*25 + 10, 2*25 + 22, 1*25 + 5,  2*25 + 23, 1*25 + 15, 2*25 + 21},
        {1*25 + 22, 5*25 + 22, 1*25 + 21, 5*25 + 23, 1*25 + 23, 5*25 + 21},
        {4*25 + 14, 3*25 + 10, 4*25 + 9,  3*25 + 5,  4*25 + 19, 3*25 + 15},
        {4*25 + 10, 2*25 + 14, 4*25 + 5,  2*25 + 9,  4*25 + 15, 2*25 + 19},
        {5*25 + 14, 2*25 + 10, 5*25 + 9,  2*25 + 5,  5*25 + 19, 2*25 + 15},
        {5*25 + 10, 3*25 + 14, 5*25 + 5,  3*25 + 9,  5*25 + 15, 3*25 + 19}
    };

    int paired = 0;
    for (int e = 0; e < 12; ++e) {
        uint8_t mf1 = s.facelets[tredges[e].midge_f1];
        uint8_t mf2 = s.facelets[tredges[e].midge_f2];
        uint8_t w1f1 = s.facelets[tredges[e].w1_f1];
        uint8_t w1f2 = s.facelets[tredges[e].w1_f2];
        uint8_t w2f1 = s.facelets[tredges[e].w2_f1];
        uint8_t w2f2 = s.facelets[tredges[e].w2_f2];
        if (w1f1 == mf1 && w1f2 == mf2 && w2f1 == mf1 && w2f2 == mf2) {
            paired++;
        }
    }
    return paired;
}

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
    int numSetups = sizeof(FREESLICE_SETUPS)/sizeof(FREESLICE_SETUPS[0]);
    int numComms = sizeof(FREESLICE_CENTER_COMMS)/sizeof(FREESLICE_CENTER_COMMS[0]);
    int numTComms = sizeof(FREESLICE_TREDGE_COMMS)/sizeof(FREESLICE_TREDGE_COMMS[0]);

    // Phase 1: Center Bars
    std::vector<Move> p1Moves;
    for (int step = 0; step < 48; ++step) {
        int bestMis = countMisplacedCenters5x5Freeslice(curr);
        if (bestMis == 0) break;
        std::string bestSeq = "";
        State5x5 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int j = 0; j < numComms; ++j) {
                std::string seq = "";
                if (FREESLICE_SETUPS[i][0] != '\0') seq += std::string(FREESLICE_SETUPS[i]) + " ";
                seq += FREESLICE_CENTER_COMMS[j];
                if (FREESLICE_INV_SETUPS[i][0] != '\0') seq += " " + std::string(FREESLICE_INV_SETUPS[i]);

                State5x5 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                int mis = countMisplacedCenters5x5Freeslice(test);
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
    for (int step = 0; step < 8; ++step) {
        int bestPaired = countPairedTredgesFreeslice(curr);
        if (bestPaired >= 8) break;
        std::string bestSeq = "";
        State5x5 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int t = 0; t < numTComms; ++t) {
                std::string seq = "";
                if (FREESLICE_SETUPS[i][0] != '\0') seq += std::string(FREESLICE_SETUPS[i]) + " ";
                seq += FREESLICE_TREDGE_COMMS[t];
                if (FREESLICE_INV_SETUPS[i][0] != '\0') seq += " " + std::string(FREESLICE_INV_SETUPS[i]);

                State5x5 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                if (test.areCentersSolved()) {
                    int p = countPairedTredgesFreeslice(test);
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
    for (int step = 0; step < 8; ++step) {
        int bestPaired = countPairedTredgesFreeslice(curr);
        if (bestPaired == 12) break;
        std::string bestSeq = "";
        State5x5 bestState = curr;

        for (int i = 0; i < numSetups; ++i) {
            for (int t = 0; t < numTComms; ++t) {
                std::string seq = "";
                if (FREESLICE_SETUPS[i][0] != '\0') seq += std::string(FREESLICE_SETUPS[i]) + " ";
                seq += FREESLICE_TREDGE_COMMS[t];
                if (FREESLICE_INV_SETUPS[i][0] != '\0') seq += " " + std::string(FREESLICE_INV_SETUPS[i]);

                State5x5 test = curr;
                std::vector<Move> mvs = parseMoves(seq);
                for (auto& m : mvs) test.applyMove(m);

                if (test.areCentersSolved()) {
                    int p = countPairedTredgesFreeslice(test);
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
        for (const auto& m : mvs) p3Moves.push_back(m);
        curr = bestState;
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
