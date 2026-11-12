#include "Cube5x5/Solver5x5_Optimal.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>

static const char* CENTER_5x5_COMMS[] = {
    "Rw U Rw'", "Rw U' Rw'", "Rw U2 Rw'", "Rw' U Rw", "Rw' U' Rw", "Rw' U2 Rw",
    "Lw U Lw'", "Lw U' Lw'", "Lw U2 Lw'", "Lw' U Lw", "Lw' U' Lw", "Lw' U2 Lw",
    "Fw U Fw'", "Fw U' Fw'", "Fw U2 Fw'", "Fw' U Fw", "Fw' U' Fw", "Fw' U2 Fw",
    "Bw U Bw'", "Bw U' Bw'", "Bw U2 Bw'", "Bw' U Bw", "Bw' U' Bw", "Bw' U2 Bw",
    "3Rw U 3Rw'", "3Rw U' 3Rw'", "3Rw U2 3Rw'", "3Rw' U 3Rw", "3Rw' U' 3Rw", "3Rw' U2 3Rw",
    "3Fw U 3Fw'", "3Fw U' 3Fw'", "3Fw U2 3Fw'", "3Fw' U 3Fw", "3Fw' U' 3Fw", "3Fw' U2 3Fw"
};

static const char* SETUPS_5x5[] = {
    "", "U", "U'", "U2", "D", "D'", "D2", "L", "L'", "L2", "R", "R'", "R2", "F", "F'", "F2", "B", "B'", "B2"
};

static const char* INV_SETUPS_5x5[] = {
    "", "U'", "U", "U2", "D'", "D", "D2", "L'", "L", "L2", "R'", "R", "R2", "F'", "F", "F2", "B'", "B", "B2"
};

static const char* TREDGE_COMMS[] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "3Uw' R U R' F R' F' R 3Uw",
    "3Dw R U R' F R' F' R 3Dw'"
};

static const char* EDGE_FLIP_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";

struct PreparsedComm5x5 {
    std::string notation;
    std::vector<Move> moves;
};

static std::vector<PreparsedComm5x5> sCenterComms5x5;
static std::vector<PreparsedComm5x5> sTredgeComms5x5;
static std::once_flag sInitCommsFlag5x5;

static void initCommsOnce5x5() {
    std::call_once(sInitCommsFlag5x5, []() {
        int numSetups = sizeof(SETUPS_5x5)/sizeof(SETUPS_5x5[0]);
        int numComms = sizeof(CENTER_5x5_COMMS)/sizeof(CENTER_5x5_COMMS[0]);
        for (int i = 0; i < numSetups; ++i) {
            for (int j = 0; j < numComms; ++j) {
                std::string seq = "";
                if (SETUPS_5x5[i][0] != '\0') seq += std::string(SETUPS_5x5[i]) + " ";
                seq += CENTER_5x5_COMMS[j];
                if (INV_SETUPS_5x5[i][0] != '\0') seq += " " + std::string(INV_SETUPS_5x5[i]);
                sCenterComms5x5.push_back({seq, parseMoves(seq)});
            }
        }

        int numTComms = sizeof(TREDGE_COMMS)/sizeof(TREDGE_COMMS[0]);
        for (int i = 0; i < numSetups; ++i) {
            for (int t = 0; t < numTComms; ++t) {
                std::string seq = "";
                if (SETUPS_5x5[i][0] != '\0') seq += std::string(SETUPS_5x5[i]) + " ";
                seq += TREDGE_COMMS[t];
                if (INV_SETUPS_5x5[i][0] != '\0') seq += " " + std::string(INV_SETUPS_5x5[i]);
                sTredgeComms5x5.push_back({seq, parseMoves(seq)});
            }
        }
    });
}

static int countMisplacedCenters5x5(const State5x5& s) {
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

static int countPairedTredges(const State5x5& s) {
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

    initCommsOnce5x5();
    State5x5 curr = state;

    // Phase 1: 3x3 Center Reduction via pre-parsed commutators with setups
    std::vector<Move> centerMoves;
    for (int step = 0; step < 48; ++step) {
        int bestMis = countMisplacedCenters5x5(curr);
        if (bestMis == 0) break;
        const PreparsedComm5x5* bestComm = nullptr;
        State5x5 bestState = curr;

        for (const auto& comm : sCenterComms5x5) {
            State5x5 test = curr;
            for (const auto& m : comm.moves) test.applyMove(m);

            int mis = countMisplacedCenters5x5(test);
            if (mis < bestMis) {
                bestMis = mis;
                bestComm = &comm;
                bestState = test;
            }
        }
        if (!bestComm) break;
        for (const auto& m : bestComm->moves) centerMoves.push_back(m);
        curr = bestState;
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

    // Phase 2: Tredge Pairing via pre-parsed slice-flip-slice commutators
    std::vector<Move> tredgeMoves;
    for (int step = 0; step < 24; ++step) {
        int bestPaired = countPairedTredges(curr);
        if (bestPaired == 12) break;
        const PreparsedComm5x5* bestComm = nullptr;
        State5x5 bestState = curr;

        for (const auto& comm : sTredgeComms5x5) {
            State5x5 test = curr;
            for (const auto& m : comm.moves) test.applyMove(m);

            if (test.areCentersSolved()) {
                int p = countPairedTredges(test);
                if (p > bestPaired) {
                    bestPaired = p;
                    bestComm = &comm;
                    bestState = test;
                }
            }
        }
        if (!bestComm) break;
        for (const auto& m : bestComm->moves) tredgeMoves.push_back(m);
        curr = bestState;
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
