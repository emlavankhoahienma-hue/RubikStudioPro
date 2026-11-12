#include "Cube2x2/Solver2x2_OrtegaCLL.hpp"
#include <chrono>

static const Move allowedMoves[9] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R, R2, R'
    {4, 0x01, 1}, {4, 0x01, 2}, {4, 0x01, 3}  // F, F2, F'
};

// Check if bottom face is solved (corners 4, 5, 6, 7 all have orientation 0 and belong to bottom layer)
static bool isBottomFaceSolved(const State2x2& s) {
    for (int i = 4; i < 8; ++i) {
        if (s.perm[i] < 4 || s.orient[i] != 0) return false;
    }
    return true;
}

static bool dfsFirstFace(State2x2& s, int depth, int lastAxis, std::vector<Move>& path) {
    if (isBottomFaceSolved(s)) return true;
    if (depth == 0) return false;

    for (int m = 0; m < 9; ++m) {
        const Move& move = allowedMoves[m];
        if (move.axis == lastAxis) continue;

        State2x2 next = s;
        next.applyMove(move);
        path.push_back(move);

        if (dfsFirstFace(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }
    return false;
}

// 7 Standard OLL Algorithms for 2x2
static const struct {
    const char* name;
    const char* alg;
} OLL_ALGS[7] = {
    {"Sune", "R U R' U R U2 R'"},
    {"Anti-Sune", "R U2 R' U' R U' R'"},
    {"H", "R2 U2 R U2 R2"},
    {"Pi", "F R U R' U' R U R' U' F'"},
    {"T", "R U R' U' R' F R F'"},
    {"U", "F R U R' U' F'"},
    {"L", "F R' F' R U R U' R'"}
};

// Standard PBL Algorithms
static const struct {
    const char* name;
    const char* alg;
} PBL_ALGS[6] = {
    {"Top Adj, Bot Solved", "R U R' U' R' F R2 U' R' U' R U R' F'"},
    {"Top Diag, Bot Solved", "F R U' R' U' R U R' F' R U R' U' R' F R F'"},
    {"Both Adj", "R2 U' R2 U2 F2 U' R2"},
    {"Top Adj, Bot Diag", "R U' R F2 R' U R'"},
    {"Top Diag, Bot Adj", "R2 U' B2 U2 R2 U' R2"},
    {"Both Diag", "R2 F2 R2"}
};

SolveResult Solver2x2_OrtegaCLL::solveState(const State2x2& state) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 2x2 không hợp lệ.";
        return result;
    }

    State2x2 curr = state;

    // Phase 1: First Face (depth 0..5)
    std::vector<Move> faceMoves;
    bool faceSolved = false;
    for (int d = 0; d <= 5; ++d) {
        faceMoves.clear();
        State2x2 temp = curr;
        if (dfsFirstFace(temp, d, -1, faceMoves)) {
            faceSolved = true;
            for (const auto& m : faceMoves) {
                curr.applyMove(m);
            }
            break;
        }
    }

    if (!faceSolved) {
        result.isValid = false;
        result.errorMessage = "Không thể ghép xong mặt đầu trong giới hạn 5 bước.";
        return result;
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: Ghép Mặt Đáy (First Face)";
    step1.moves = faceMoves;
    std::string not1;
    for (size_t i = 0; i < faceMoves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(faceMoves[i], 2);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(faceMoves.size());

    // Phase 2: OLL
    std::vector<Move> ollMoves;
    // Check if top face already oriented (corners 0..3 have orient == 0)
    bool topOriented = (curr.orient[0] == 0 && curr.orient[1] == 0 && curr.orient[2] == 0 && curr.orient[3] == 0);
    if (!topOriented) {
        // Try up to 4 U turns + 7 OLL algorithms
        bool ollFound = false;
        for (int uTurn = 0; uTurn < 4 && !ollFound; ++uTurn) {
            for (int k = 0; k < 7; ++k) {
                State2x2 testState = curr;
                std::vector<Move> testMoves;

                if (uTurn > 0) {
                    Move uMove{0, 0x01, static_cast<uint8_t>(uTurn)};
                    testState.applyMove(uMove);
                    testMoves.push_back(uMove);
                }

                std::vector<Move> algMoves = parseMoves(OLL_ALGS[k].alg);
                for (const auto& m : algMoves) {
                    testState.applyMove(m);
                    testMoves.push_back(m);
                }

                if (testState.orient[0] == 0 && testState.orient[1] == 0 &&
                    testState.orient[2] == 0 && testState.orient[3] == 0) {
                    ollMoves = testMoves;
                    curr = testState;
                    ollFound = true;
                    break;
                }
            }
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: OLL (Định Hướng Mặt Vàng)";
    step2.moves = ollMoves;
    std::string not2;
    for (size_t i = 0; i < ollMoves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(ollMoves[i], 2);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(ollMoves.size());

    // Phase 3: PBL + AUF
    std::vector<Move> pblMoves;
    bool solved = curr.isSolved();

    if (!solved) {
        bool pblFound = false;
        // Try AUF before PBL, test PBL algorithms, then AUF after
        for (int uPre = 0; uPre < 4 && !pblFound; ++uPre) {
            for (int k = 0; k < 6 && !pblFound; ++k) {
                for (int uPost = 0; uPost < 4 && !pblFound; ++uPost) {
                    State2x2 test = curr;
                    std::vector<Move> cand;

                    if (uPre > 0) {
                        Move mPre{0, 0x01, static_cast<uint8_t>(uPre)};
                        test.applyMove(mPre);
                        cand.push_back(mPre);
                    }

                    std::vector<Move> alg = parseMoves(PBL_ALGS[k].alg);
                    for (const auto& m : alg) {
                        test.applyMove(m);
                        cand.push_back(m);
                    }

                    if (uPost > 0) {
                        Move mPost{0, 0x01, static_cast<uint8_t>(uPost)};
                        test.applyMove(mPost);
                        cand.push_back(mPost);
                    }

                    if (test.isSolved()) {
                        pblMoves = cand;
                        curr = test;
                        pblFound = true;
                    }
                }
            }
        }

        // Also check if pure AUF solves it
        if (!pblFound) {
            for (int u = 1; u < 4; ++u) {
                State2x2 test = curr;
                Move mu{0, 0x01, static_cast<uint8_t>(u)};
                test.applyMove(mu);
                if (test.isSolved()) {
                    pblMoves.push_back(mu);
                    curr = test;
                    pblFound = true;
                    break;
                }
            }
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: PBL (Hoán Vị Hai Tầng)";
    step3.moves = pblMoves;
    std::string not3;
    for (size_t i = 0; i < pblMoves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(pblMoves[i], 2);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(pblMoves.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver2x2_OrtegaCLL::solve(const uint8_t colors[24]) {
    State2x2 s = State2x2::fromFacelets(colors);
    return solveState(s);
}
