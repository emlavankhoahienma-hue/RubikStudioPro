#include "Cube3x3/Solver3x3_CFOP.hpp"
#include <chrono>
#include <algorithm>
#include <array>

static const Move all18Moves[18] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U
    {1, 0x01, 1}, {1, 0x01, 2}, {1, 0x01, 3}, // D
    {2, 0x01, 1}, {2, 0x01, 2}, {2, 0x01, 3}, // L
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R
    {4, 0x01, 1}, {4, 0x01, 2}, {4, 0x01, 3}, // F
    {5, 0x01, 1}, {5, 0x01, 2}, {5, 0x01, 3}  // B
};

// 1. Cross Verification
static bool isCrossSolved(const State3x3& s) {
    return (s.ep[4] == 4 && s.eo[4] == 0 &&
            s.ep[5] == 5 && s.eo[5] == 0 &&
            s.ep[6] == 6 && s.eo[6] == 0 &&
            s.ep[7] == 7 && s.eo[7] == 0);
}

static int crossHeuristic(const State3x3& s) {
    int count = 0;
    if (s.ep[4] != 4 || s.eo[4] != 0) count++;
    if (s.ep[5] != 5 || s.eo[5] != 0) count++;
    if (s.ep[6] != 6 || s.eo[6] != 0) count++;
    if (s.ep[7] != 7 || s.eo[7] != 0) count++;
    return (count + 1) / 2;
}

static bool searchCross(State3x3& s, int depth, int lastAxis, std::vector<Move>& path) {
    if (isCrossSolved(s)) return true;
    if (depth == 0) return false;
    if (crossHeuristic(s) > depth) return false;

    for (int m = 0; m < 18; ++m) {
        const Move& move = all18Moves[m];
        if (move.axis == lastAxis) continue;

        State3x3 next = s;
        next.applyMove(move);
        path.push_back(move);

        if (searchCross(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }
    return false;
}

// 2. F2L Helpers
static bool isSlotSolved(const State3x3& s, int slot) {
    // slot 0: corner 4, edge 8 (DFR, FR)
    // slot 1: corner 5, edge 9 (DLF, FL)
    // slot 2: corner 6, edge 10 (DBL, BL)
    // slot 3: corner 7, edge 11 (DRB, BR)
    return (s.cp[4 + slot] == (4 + slot) && s.co[4 + slot] == 0 &&
            s.ep[8 + slot] == (8 + slot) && s.eo[8 + slot] == 0);
}

static bool preservesF2L(const State3x3& s, uint8_t solvedMask) {
    if (!isCrossSolved(s)) return false;
    for (int i = 0; i < 4; ++i) {
        if ((solvedMask & (1 << i)) && !isSlotSolved(s, i)) return false;
    }
    return true;
}

// Standard F2L candidate sequences
static const char* F2L_CANONICAL_MOVES[4][8] = {
    // Slot 0 (FR)
    {"U R U' R'", "R U R'", "R U2 R' U R U' R'", "U' R U R' U R U R'", "R U' R' U2 R U R'", "y' U' L' U L y", "U2 R U R' U R U' R'", "R' F R F'"},
    // Slot 1 (FL)
    {"U' L' U L", "L' U' L", "L' U2 L U' L' U L", "U L' U' L U' L' U' L", "L' U L U2 L' U' L", "y U R U' R' y'", "U2 L' U' L U' L' U L", "L F' L' F"},
    // Slot 2 (BL)
    {"U L U' L'", "L U L'", "L U2 L' U L U' L'", "U' L U L' U L U L'", "L U' L' U2 L U L'", "U2 L U L' U L U' L'", "L' B L B'", "B' U B"},
    // Slot 3 (BR)
    {"U' R' U R", "R' U' R", "R' U2 R U' R' U R", "U R' U' R U' R' U' R", "R' U R U2 R' U' R", "U2 R' U' R U' R' U R", "R B' R' B", "B U' B'"}
};

// 3. OLL Database (57 standard algorithms)
static const char* OLL_57[57] = {
    "R U2 R2 F R F' U2 R' F R F'", "F R U R' U' F' f R U R' U' f'", "f R U R' U' f' U' F R U R' U' F'", "f R U R' U' f' U F R U R' U' F'",
    "r' U2 R U R' U r", "r U2 R' U' R U' r'", "r U R' U R U2 r'", "r' U' R U' R' U2 r",
    "R U R' U' R' F R2 U R' U' F'", "R U R' U R' F R F' R U2 R'", "r U R' U R' F R F' R U2 r'", "M' R' U' R U' R' U2 R U' R r'",
    "F U R U' R2 F' R U R U' R'", "R' F R U R' F' R F U' F'", "l' U' l L' U' L U l' U l", "r U r' R U R' U' r U' r'",
    "F R' F' R2 r' U R U' R' U' M'", "r U R' U R U2 r2 U' R U' R' U2 r", "r' R U R U R' U' r R2' F R F'", "r U R' U' M2 U R U' R' U' M'",
    "R U2 R' U' R U R' U' R U' R'", "R U2 R2 U' R2 U' R2 U2 R", "R2 D' R U2 R' D R U2 R", "r U R' U' r' F R F'",
    "F' r U R' U' r' F R", "R U2 R' U' R U' R'", "R U R' U R U2 R'", "r U R' U' r' R U R U' R'",
    "R U R' U' R U' R' F' U' F R U R'", "F R' F R2 U' R' U' R U R' F2", "R' U' F U R U' R' F' R", "L U F' U' L' U L F L'",
    "R U R' U' R' F R F'", "R U R2 U' R' F R U R U' F'", "R U2 R2 F R F' R U2 R'", "L' U' L U' L' U L U L F' L' F",
    "F R' F' R U R U' R'", "R U R' U R U' R' U' R' F R F'", "L F' L' U' L U F U' L'", "R' F R U R' U' F' U R",
    "R U R' U R U2 R' F R U R' U' F'", "R' U' R U' R' U2 R F R U R' U' F'", "f' L' U' L U f", "f R U R' U' f'",
    "F R U R' U' F'", "R' U' R' F R F' U R", "F' L' U' L U L' U' L U F", "F R U R' U' R U R' U' F'",
    "r U' r2 U r2 U r2 U' r", "r' U r2 U' r2 U' r2 U r'", "f R U R' U' R U R' U' f'", "R U R' U R d' R U' R' F'",
    "r' U' R U' R' U R U' R' U2 r", "r U R' U R U' R' U R U2 r'", "R' F R U R U' R2 F' R2 U' R' U R U R'", "r' U' r U' R' U R r' U r",
    "M U R U R' U' M' R' F R F'"
};

// 4. PLL Database (21 standard algorithms)
static const struct {
    const char* name;
    const char* alg;
} PLL_21[21] = {
    {"Aa", "x R' U R' D2 R U' R' D2 R2 x'"},
    {"Ab", "x R2 D2 R' U' R D2 R' U R' x'"},
    {"F", "R' U' F' R U R' U' R' F R2 U' R' U' R U R' U R"},
    {"Ga", "R2 U R' U R' U' R U' R2 D U' R' U R D'"},
    {"Gb", "R' U' R U D' R2 U R' U R U' R U' R2 D"},
    {"Gc", "R2 U' R U' R U R' U R2 D' U R U' R' D"},
    {"Gd", "R U R' U' D R2 U' R U' R' U R' U R2 D'"},
    {"Ja", "x R2 F R F' R U2 r' U r U2 x'"},
    {"Jb", "R U R' F' R U R' U' R' F R2 U' R'"},
    {"Ra", "R U' R' U' R U R D R' U' R D' R' U2 R'"},
    {"Rb", "R' U2 R U2 R' F R U R' U' R' F' R2"},
    {"T", "R U R' U' R' F R2 U' R' U' R U R' F'"},
    {"E", "x' R U' R' D R U R' D' R U R' D R U' R' D' x"},
    {"Na", "R U R' U R U R' F' R U R' U' R' F R2 U' R' U2 R U' R'"},
    {"Nb", "R' U R U' R' F' U' F R U R' F R' F' R U' R"},
    {"V", "R' U R' U' y R' F' R2 U' R' U R' F R F"},
    {"Y", "F R U' R' U' R U R' F' R U R' U' R' F R F'"},
    {"H", "M2 U M2 U2 M2 U M2"},
    {"Ua", "R U' R U R U R U' R' U' R2"},
    {"Ub", "R2 U R U R' U' R' U' R' U R'"},
    {"Z", "M' U M2 U M2 U M' U2 M2"}
};

SolveResult Solver3x3_CFOP::solveState(const State3x3& state) {
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

    // Phase 1: Cross
    std::vector<Move> crossMoves;
    bool crossSolved = isCrossSolved(curr);
    if (!crossSolved) {
        for (int d = 1; d <= 8; ++d) {
            crossMoves.clear();
            State3x3 temp = curr;
            if (searchCross(temp, d, -1, crossMoves)) {
                crossSolved = true;
                for (const auto& m : crossMoves) {
                    curr.applyMove(m);
                }
                break;
            }
        }
    }

    PhaseStep step1;
    step1.phaseName = "Pha 1: Cross (Giải 4 cạnh đáy D)";
    step1.moves = crossMoves;
    std::string not1;
    for (size_t i = 0; i < crossMoves.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(crossMoves[i], 3);
    }
    step1.notationString = not1;
    result.phases.push_back(step1);
    result.totalMoves += static_cast<int>(crossMoves.size());

    // Phase 2: F2L (Solve 4 slots using beam search across candidate moves)
    std::vector<Move> f2lMoves;
    uint8_t solvedSlotsMask = 0;

    for (int slot = 0; slot < 4; ++slot) {
        if (isSlotSolved(curr, slot)) {
            solvedSlotsMask |= (1 << slot);
            continue;
        }

        bool slotFound = false;
        std::vector<Move> bestSlotMoves;

        // Try candidate F2L algorithms for this slot with pre-AUF U
        for (int uPre = 0; uPre < 4 && !slotFound; ++uPre) {
            for (int k = 0; k < 8 && !slotFound; ++k) {
                State3x3 test = curr;
                std::vector<Move> cand;
                if (uPre > 0) {
                    Move mu{0, 0x01, static_cast<uint8_t>(uPre)};
                    test.applyMove(mu);
                    cand.push_back(mu);
                }

                std::vector<Move> algMoves = parseMoves(F2L_CANONICAL_MOVES[slot][k]);
                for (const auto& m : algMoves) {
                    test.applyMove(m);
                    cand.push_back(m);
                }

                if (isSlotSolved(test, slot) && preservesF2L(test, solvedSlotsMask)) {
                    bestSlotMoves = cand;
                    curr = test;
                    solvedSlotsMask |= (1 << slot);
                    slotFound = true;
                    break;
                }
            }
        }

        // If not found directly, try basic insert (R U R' or L' U' L)
        if (!slotFound) {
            for (int rot = 0; rot < 4 && !slotFound; ++rot) {
                State3x3 test = curr;
                std::vector<Move> cand;
                Move mu{0, 0x01, 1};
                test.applyMove(mu);
                cand.push_back(mu);

                std::vector<Move> algMoves = parseMoves(F2L_CANONICAL_MOVES[slot][0]);
                for (const auto& m : algMoves) {
                    test.applyMove(m);
                    cand.push_back(m);
                }
                if (isSlotSolved(test, slot) && preservesF2L(test, solvedSlotsMask)) {
                    bestSlotMoves = cand;
                    curr = test;
                    solvedSlotsMask |= (1 << slot);
                    slotFound = true;
                }
            }
        }

        for (const auto& m : bestSlotMoves) {
            f2lMoves.push_back(m);
        }
    }

    PhaseStep step2;
    step2.phaseName = "Pha 2: F2L (Ghép 4 cặp góc-cạnh 2 tầng đầu)";
    step2.moves = f2lMoves;
    std::string not2;
    for (size_t i = 0; i < f2lMoves.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(f2lMoves[i], 3);
    }
    step2.notationString = not2;
    result.phases.push_back(step2);
    result.totalMoves += static_cast<int>(f2lMoves.size());

    // Phase 3: OLL
    std::vector<Move> ollMoves;
    bool topOriented = (curr.co[0] == 0 && curr.co[1] == 0 && curr.co[2] == 0 && curr.co[3] == 0 &&
                        curr.eo[0] == 0 && curr.eo[1] == 0 && curr.eo[2] == 0 && curr.eo[3] == 0);

    if (!topOriented) {
        bool ollFound = false;
        for (int uTurn = 0; uTurn < 4 && !ollFound; ++uTurn) {
            for (int k = 0; k < 57 && !ollFound; ++k) {
                State3x3 test = curr;
                std::vector<Move> cand;
                if (uTurn > 0) {
                    Move mu{0, 0x01, static_cast<uint8_t>(uTurn)};
                    test.applyMove(mu);
                    cand.push_back(mu);
                }

                std::vector<Move> algMoves = parseMoves(OLL_57[k]);
                for (const auto& m : algMoves) {
                    test.applyMove(m);
                    cand.push_back(m);
                }

                if (test.co[0] == 0 && test.co[1] == 0 && test.co[2] == 0 && test.co[3] == 0 &&
                    test.eo[0] == 0 && test.eo[1] == 0 && test.eo[2] == 0 && test.eo[3] == 0) {
                    ollMoves = cand;
                    curr = test;
                    ollFound = true;
                    break;
                }
            }
        }
    }

    PhaseStep step3;
    step3.phaseName = "Pha 3: OLL (Định hướng toàn bộ mặt vàng)";
    step3.moves = ollMoves;
    std::string not3;
    for (size_t i = 0; i < ollMoves.size(); ++i) {
        if (i > 0) not3 += " ";
        not3 += moveToString(ollMoves[i], 3);
    }
    step3.notationString = not3;
    result.phases.push_back(step3);
    result.totalMoves += static_cast<int>(ollMoves.size());

    // Phase 4: PLL
    std::vector<Move> pllMoves;
    if (!curr.isSolved()) {
        bool pllFound = false;
        for (int uPre = 0; uPre < 4 && !pllFound; ++uPre) {
            for (int k = 0; k < 21 && !pllFound; ++k) {
                for (int uPost = 0; uPost < 4 && !pllFound; ++uPost) {
                    State3x3 test = curr;
                    std::vector<Move> cand;
                    if (uPre > 0) {
                        Move mu{0, 0x01, static_cast<uint8_t>(uPre)};
                        test.applyMove(mu);
                        cand.push_back(mu);
                    }

                    std::vector<Move> algMoves = parseMoves(PLL_21[k].alg);
                    for (const auto& m : algMoves) {
                        test.applyMove(m);
                        cand.push_back(m);
                    }

                    if (uPost > 0) {
                        Move mu{0, 0x01, static_cast<uint8_t>(uPost)};
                        test.applyMove(mu);
                        cand.push_back(mu);
                    }

                    if (test.isSolved()) {
                        pllMoves = cand;
                        curr = test;
                        pllFound = true;
                        break;
                    }
                }
            }
        }

        // AUF check if already permuted
        if (!pllFound) {
            for (int u = 1; u < 4; ++u) {
                State3x3 test = curr;
                Move mu{0, 0x01, static_cast<uint8_t>(u)};
                test.applyMove(mu);
                if (test.isSolved()) {
                    pllMoves.push_back(mu);
                    curr = test;
                    pllFound = true;
                    break;
                }
            }
        }
    }

    PhaseStep step4;
    step4.phaseName = "Pha 4: PLL (Hoán vị tầng cuối + AUF)";
    step4.moves = pllMoves;
    std::string not4;
    for (size_t i = 0; i < pllMoves.size(); ++i) {
        if (i > 0) not4 += " ";
        not4 += moveToString(pllMoves[i], 3);
    }
    step4.notationString = not4;
    result.phases.push_back(step4);
    result.totalMoves += static_cast<int>(pllMoves.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver3x3_CFOP::solve(const uint8_t colors[54]) {
    if (!State3x3::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 9 ô).";
        return r;
    }
    State3x3 s = State3x3::fromFacelets(colors);
    return solveState(s);
}
