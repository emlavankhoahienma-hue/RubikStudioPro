#include "Cube4x4/Solver4x4_Optimal.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>

static const char* CENTER_COMMUTATORS[] = {
    "Rw U Rw'", "Rw U' Rw'", "Rw U2 Rw'", "Rw' U Rw", "Rw' U' Rw", "Rw' U2 Rw",
    "Lw U Lw'", "Lw U' Lw'", "Lw U2 Lw'", "Lw' U Lw", "Lw' U' Lw", "Lw' U2 Lw",
    "Fw U Fw'", "Fw U' Fw'", "Fw U2 Fw'", "Fw' U Fw", "Fw' U' Fw", "Fw' U2 Fw",
    "Bw U Bw'", "Bw U' Bw'", "Bw U2 Bw'", "Bw' U Bw", "Bw' U' Bw", "Bw' U2 Bw",
    "Rw D Rw'", "Rw D' Rw'", "Rw D2 Rw'", "Rw' D Rw", "Rw' D' Rw", "Rw' D2 Rw",
    "Lw D Lw'", "Lw D' Lw'", "Lw D2 Lw'", "Lw' D Lw", "Lw' D' Lw", "Lw' D2 Lw"
};

static const char* SETUPS[] = {
    "", "U", "U'", "U2", "D", "D'", "D2", "L", "L'", "L2", "R", "R'", "R2", "F", "F'", "F2", "B", "B'", "B2"
};

static const char* INVERSE_SETUPS[] = {
    "", "U'", "U", "U2", "D'", "D", "D2", "L'", "L", "L2", "R'", "R", "R2", "F'", "F", "F2", "B'", "B", "B2"
};

static const char* WING_COMMUTATORS[] = {
    "Uw' R U R' F R' F' R Uw",
    "Dw R U R' F R' F' R Dw'",
    "Uw' F' L F L' U' L U Uw",
    "Dw F' L F L' U' L U Dw'"
};

static const char* OLL_PARITY_ALG = "Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2";
static const char* PLL_PARITY_ALG = "2R2 U2 2R2 Uw2 2R2 Uw2";

struct PreparsedComm4x4 {
    std::string notation;
    std::vector<Move> moves;
};

static std::vector<PreparsedComm4x4> sCenterComms4x4;
static std::vector<PreparsedComm4x4> sWingComms4x4;
static std::once_flag sInitCommsFlag4x4;

static void initCommsOnce4x4() {
    std::call_once(sInitCommsFlag4x4, []() {
        int numSetups = sizeof(SETUPS)/sizeof(SETUPS[0]);
        int numComms = sizeof(CENTER_COMMUTATORS)/sizeof(CENTER_COMMUTATORS[0]);
        for (int i = 0; i < numSetups; ++i) {
            for (int j = 0; j < numComms; ++j) {
                std::string seq = "";
                if (SETUPS[i][0] != '\0') seq += std::string(SETUPS[i]) + " ";
                seq += CENTER_COMMUTATORS[j];
                if (INVERSE_SETUPS[i][0] != '\0') seq += " " + std::string(INVERSE_SETUPS[i]);
                sCenterComms4x4.push_back({seq, parseMoves(seq)});
            }
        }

        int numWComms = sizeof(WING_COMMUTATORS)/sizeof(WING_COMMUTATORS[0]);
        for (int i = 0; i < numSetups; ++i) {
            for (int w = 0; w < numWComms; ++w) {
                std::string seq = "";
                if (SETUPS[i][0] != '\0') seq += std::string(SETUPS[i]) + " ";
                seq += WING_COMMUTATORS[w];
                if (INVERSE_SETUPS[i][0] != '\0') seq += " " + std::string(INVERSE_SETUPS[i]);
                sWingComms4x4.push_back({seq, parseMoves(seq)});
            }
        }
    });
}

static int countMisplacedCenters(const State4x4& s) {
    static const int centerIdx[4] = {5, 6, 9, 10};
    int misplaced = 0;
    for (int face = 0; face < 6; ++face) {
        for (int i = 0; i < 4; ++i) {
            if (s.facelets[face * 16 + centerIdx[i]] != face) misplaced++;
        }
    }
    return misplaced;
}

static int countPairedWings(const State4x4& s) {
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

    initCommsOnce4x4();
    State4x4 curr = state;

    // Phase 1: Center Reduction via pre-parsed commutators with setup moves
    std::vector<Move> centerMoves;
    for (int step = 0; step < 24; ++step) {
        int bestMis = countMisplacedCenters(curr);
        if (bestMis == 0) break;
        const PreparsedComm4x4* bestComm = nullptr;
        State4x4 bestState = curr;

        for (const auto& comm : sCenterComms4x4) {
            State4x4 test = curr;
            for (const auto& m : comm.moves) test.applyMove(m);

            int mis = countMisplacedCenters(test);
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

    // Phase 2: Edge Pairing via pre-parsed slice-flip-slice commutators
    std::vector<Move> wingMoves;
    for (int step = 0; step < 12; ++step) {
        int bestPaired = countPairedWings(curr);
        if (bestPaired == 12) break;
        const PreparsedComm4x4* bestComm = nullptr;
        State4x4 bestState = curr;

        for (const auto& comm : sWingComms4x4) {
            State4x4 test = curr;
            for (const auto& m : comm.moves) test.applyMove(m);

            if (test.areCentersSolved()) {
                int p = countPairedWings(test);
                if (p > bestPaired) {
                    bestPaired = p;
                    bestComm = &comm;
                    bestState = test;
                }
            }
        }
        if (!bestComm) break;
        for (const auto& m : bestComm->moves) wingMoves.push_back(m);
        curr = bestState;
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: Ghép 24 Cạnh Wings (Edge Pairing)";
    step2.moves = wingMoves;
    std::string not2;
    for (size_t i = 0; i < wingMoves.size(); ++i) {
        if (i > 0) not1 += " ";
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
