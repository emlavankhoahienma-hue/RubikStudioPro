#include "Cube3x3/Solver3x3_Roux.hpp"
#include <chrono>
#include <algorithm>

static const Move all18Moves[18] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U
    {1, 0x01, 1}, {1, 0x01, 2}, {1, 0x01, 3}, // D
    {2, 0x01, 1}, {2, 0x01, 2}, {2, 0x01, 3}, // L
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R
    {4, 0x01, 1}, {4, 0x01, 2}, {4, 0x01, 3}, // F
    {5, 0x01, 1}, {5, 0x01, 2}, {5, 0x01, 3}  // B
};

// 1. First Block (FB): DL(6), FL(9), BL(10), DLF(5), DBL(6)
static bool isFirstBlockSolved(const State3x3& s) {
    return (s.ep[6] == 6 && s.eo[6] == 0 &&
            s.ep[9] == 9 && s.eo[9] == 0 &&
            s.ep[10] == 10 && s.eo[10] == 0 &&
            s.cp[5] == 5 && s.co[5] == 0 &&
            s.cp[6] == 6 && s.co[6] == 0);
}

static int fbHeuristic(const State3x3& s) {
    int cnt = 0;
    if (s.ep[6] != 6 || s.eo[6] != 0) cnt++;
    if (s.ep[9] != 9 || s.eo[9] != 0) cnt++;
    if (s.ep[10] != 10 || s.eo[10] != 0) cnt++;
    if (s.cp[5] != 5 || s.co[5] != 0) cnt++;
    if (s.cp[6] != 6 || s.co[6] != 0) cnt++;
    return (cnt + 1) / 2;
}

static bool searchFB(State3x3& s, int depth, int lastAxis, std::vector<Move>& path) {
    if (isFirstBlockSolved(s)) return true;
    if (depth == 0) return false;
    if (fbHeuristic(s) > depth) return false;

    for (int m = 0; m < 18; ++m) {
        const Move& move = all18Moves[m];
        if (move.axis == lastAxis) continue;

        State3x3 next = s;
        next.applyMove(move);
        path.push_back(move);

        if (searchFB(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }
    return false;
}

// 2. Second Block (SB): DR(4), FR(8), BR(11), DFR(4), DRB(7)
static bool isSecondBlockSolved(const State3x3& s) {
    return (s.ep[4] == 4 && s.eo[4] == 0 &&
            s.ep[8] == 8 && s.eo[8] == 0 &&
            s.ep[11] == 11 && s.eo[11] == 0 &&
            s.cp[4] == 4 && s.co[4] == 0 &&
            s.cp[7] == 7 && s.co[7] == 0);
}

// Allowed moves for SB preserving FB: <R, r, U, M>
// M slice follows L axis (axis 2, sliceMask 0x02)
static const Move sbAllowedMoves[9] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R, R2, R'
    {2, 0x02, 1}, {2, 0x02, 2}, {2, 0x02, 3}  // M, M2, M'
};

static bool searchSB(State3x3& s, int depth, int lastAxis, std::vector<Move>& path) {
    if (isSecondBlockSolved(s) && isFirstBlockSolved(s)) return true;
    if (depth == 0) return false;

    for (int m = 0; m < 9; ++m) {
        const Move& move = sbAllowedMoves[m];
        if (move.axis == lastAxis && move.sliceMask == 0x01) continue;

        State3x3 next = s;
        next.applyMove(move);
        if (!isFirstBlockSolved(next)) continue; // Must preserve Left Block

        path.push_back(move);
        if (searchSB(next, depth - 1, move.axis, path)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

// 3. CMLL Algorithms
static const char* CMLL_ALGS[14] = {
    "R U R' U R U2 R'",
    "R U2 R' U' R U' R'",
    "R2 U2 R U2 R2",
    "F R U R' U' R U R' U' F'",
    "R U R' U' R' F R F'",
    "F R U R' U' F'",
    "F R' F' R U R U' R'",
    "R U R' U' R' F R2 U' R' U' R U R' F'",
    "F R U' R' U' R U R' F' R U R' U' R' F R F'",
    "R2 D R' U2 R D' R' U2 R'",
    "r U R' U' r' F R F'",
    "R U2 R2 F R F' U2 R' F R F'",
    "R' F R B' R' F' R B",
    "R U R' F' R U R' U' R' F R2 U' R'"
};

// 4. LSE <M, U> moves
static const Move lseMoves[6] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {2, 0x02, 1}, {2, 0x02, 2}, {2, 0x02, 3}  // M, M2, M'
};

static bool searchLSE(State3x3& s, int depth, int lastAxis, std::vector<Move>& path) {
    if (s.isSolved()) return true;
    if (depth == 0) return false;

    // Count unsolved edges in LSE
    int unsolved = 0;
    static const int lseEdgeIdx[6] = {0, 1, 2, 3, 5, 7};
    for (int i = 0; i < 6; ++i) {
        int e = lseEdgeIdx[i];
        if (s.ep[e] != e || s.eo[e] != 0) unsolved++;
    }
    if ((unsolved + 1) / 2 > depth) return false;

    for (int m = 0; m < 6; ++m) {
        const Move& move = lseMoves[m];
        if (move.axis == lastAxis) continue;

        State3x3 next = s;
        next.applyMove(move);
        path.push_back(move);

        if (searchLSE(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }
    return false;
}

SolveResult Solver3x3_Roux::solveState(const State3x3& state) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 3x3 không hợp lệ.";
        return result;
    }

    State3x3 curr = state;

    // Phase 1: First Block
    std::vector<Move> fbMoves;
    bool fbSolved = isFirstBlockSolved(curr);
    if (!fbSolved) {
        for (int d = 1; d <= 8; ++d) {
            fbMoves.clear();
            State3x3 temp = curr;
            if (searchFB(temp, d, -1, fbMoves)) {
                fbSolved = true;
                for (const auto& m : fbMoves) {
                    curr.applyMove(m);
                }
                break;
            }
        }
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: First Block 1x2x3 (Khối trái)";
    step1.moves = fbMoves;
    std::string not1;
    for (size_t i = 0; i < fbMoves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(fbMoves[i], 3);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(fbMoves.size());

    // Phase 2: Second Block
    std::vector<Move> sbMoves;
    bool sbSolved = isSecondBlockSolved(curr);
    if (!sbSolved) {
        for (int d = 1; d <= 9; ++d) {
            sbMoves.clear();
            State3x3 temp = curr;
            if (searchSB(temp, d, -1, sbMoves)) {
                sbSolved = true;
                for (const auto& m : sbMoves) {
                    curr.applyMove(m);
                }
                break;
            }
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: Second Block 1x2x3 (Khối phải)";
    step2.moves = sbMoves;
    std::string not2;
    for (size_t i = 0; i < sbMoves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(sbMoves[i], 3);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(sbMoves.size());

    // Phase 3: CMLL
    std::vector<Move> cmllMoves;
    bool cornersSolved = (curr.cp[0] == 0 && curr.co[0] == 0 &&
                          curr.cp[1] == 1 && curr.co[1] == 0 &&
                          curr.cp[2] == 2 && curr.co[2] == 0 &&
                          curr.cp[3] == 3 && curr.co[3] == 0);

    if (!cornersSolved) {
        bool cmllFound = false;
        for (int uTurn = 0; uTurn < 4 && !cmllFound; ++uTurn) {
            for (int k = 0; k < 14 && !cmllFound; ++k) {
                State3x3 test = curr;
                std::vector<Move> cand;
                if (uTurn > 0) {
                    Move mu{0, 0x01, static_cast<uint8_t>(uTurn)};
                    test.applyMove(mu);
                    cand.push_back(mu);
                }

                std::vector<Move> algMoves = parseMoves(CMLL_ALGS[k]);
                for (const auto& m : algMoves) {
                    test.applyMove(m);
                    cand.push_back(m);
                }

                if (test.cp[0] == 0 && test.co[0] == 0 &&
                    test.cp[1] == 1 && test.co[1] == 0 &&
                    test.cp[2] == 2 && test.co[2] == 0 &&
                    test.cp[3] == 3 && test.co[3] == 0 &&
                    isFirstBlockSolved(test) && isSecondBlockSolved(test)) {
                    cmllMoves = cand;
                    curr = test;
                    cmllFound = true;
                    break;
                }
            }
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: CMLL (Giải 4 góc tầng trên)";
    step3.moves = cmllMoves;
    std::string not3;
    for (size_t i = 0; i < cmllMoves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(cmllMoves[i], 3);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(cmllMoves.size());

    // Phase 4: LSE
    std::vector<Move> lsePath;
    bool lseSolved = curr.isSolved();
    if (!lseSolved) {
        for (int d = 1; d <= 12; ++d) {
            lsePath.clear();
            State3x3 temp = curr;
            if (searchLSE(temp, d, -1, lsePath)) {
                lseSolved = true;
                for (const auto& m : lsePath) {
                    curr.applyMove(m);
                }
                break;
            }
        }
    }

    PhaseStep step4;
    step4.phaseName = "Pha 4: LSE (Giải 6 cạnh cuối bằng <M, U>)";
    step4.moves = lsePath;
    std::string not4;
    for (size_t i = 0; i < lsePath.size(); ++i) {
        if (i > 0) not4 += " ";
        not4 += moveToString(lsePath[i], 3);
    }
    step4.notationString = not4;
    result.phases.push_back(step4);
    result.totalMoves += static_cast<int>(lsePath.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver3x3_Roux::solve(const uint8_t colors[54]) {
    if (!State3x3::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 9 ô).";
        return r;
    }
    State3x3 s = State3x3::fromFacelets(colors);
    return solveState(s);
}
