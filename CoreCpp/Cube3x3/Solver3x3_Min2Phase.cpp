#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include <queue>
#include <chrono>
#include <mutex>
#include <cstring>
#include <algorithm>

static int8_t pruneTwist[2187];
static int8_t pruneFlip[2048];
static int8_t pruneUDSlice[495];
static int8_t pruneCP[40320];
static int8_t pruneEP[40320];
static int8_t pruneSliceSorted[24];

static bool tablesReady = false;
static std::mutex gTableMutex;

// 18 moves in Phase 1
static const Move phase1Moves[18] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {1, 0x01, 1}, {1, 0x01, 2}, {1, 0x01, 3}, // D, D2, D'
    {2, 0x01, 1}, {2, 0x01, 2}, {2, 0x01, 3}, // L, L2, L'
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R, R2, R'
    {4, 0x01, 1}, {4, 0x01, 2}, {4, 0x01, 3}, // F, F2, F'
    {5, 0x01, 1}, {5, 0x01, 2}, {5, 0x01, 3}  // B, B2, B'
};

// 10 moves in Phase 2 (G1 subgroup: U, D, R2, L2, F2, B2)
static const Move phase2Moves[10] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {1, 0x01, 1}, {1, 0x01, 2}, {1, 0x01, 3}, // D, D2, D'
    {2, 0x01, 2},                             // L2
    {3, 0x01, 2},                             // R2
    {4, 0x01, 2},                             // F2
    {5, 0x01, 2}                              // B2
};

void Solver3x3_Min2Phase::initTables() {
    std::lock_guard<std::mutex> lock(gTableMutex);
    if (tablesReady) return;

    // 1. Twist pruning (2187 states)
    std::memset(pruneTwist, -1, sizeof(pruneTwist));
    pruneTwist[0] = 0;
    std::queue<uint16_t> qTwist;
    qTwist.push(0);
    while (!qTwist.empty()) {
        uint16_t curr = qTwist.front();
        qTwist.pop();
        int8_t d = pruneTwist[curr];
        State3x3 s;
        s.setTwist(curr);
        for (int m = 0; m < 18; ++m) {
            State3x3 next = s;
            next.applyMove(phase1Moves[m]);
            uint16_t nc = next.getTwist();
            if (pruneTwist[nc] == -1) {
                pruneTwist[nc] = d + 1;
                qTwist.push(nc);
            }
        }
    }

    // 2. Flip pruning (2048 states)
    std::memset(pruneFlip, -1, sizeof(pruneFlip));
    pruneFlip[0] = 0;
    std::queue<uint16_t> qFlip;
    qFlip.push(0);
    while (!qFlip.empty()) {
        uint16_t curr = qFlip.front();
        qFlip.pop();
        int8_t d = pruneFlip[curr];
        State3x3 s;
        s.setFlip(curr);
        for (int m = 0; m < 18; ++m) {
            State3x3 next = s;
            next.applyMove(phase1Moves[m]);
            uint16_t nc = next.getFlip();
            if (pruneFlip[nc] == -1) {
                pruneFlip[nc] = d + 1;
                qFlip.push(nc);
            }
        }
    }

    // 3. UDSlice pruning (495 states)
    std::memset(pruneUDSlice, -1, sizeof(pruneUDSlice));
    pruneUDSlice[0] = 0;
    std::queue<uint16_t> qSlice;
    qSlice.push(0);
    while (!qSlice.empty()) {
        uint16_t curr = qSlice.front();
        qSlice.pop();
        int8_t d = pruneUDSlice[curr];
        State3x3 s;
        // solved slice has edges 8..11 at positions 8..11
        for (int m = 0; m < 18; ++m) {
            State3x3 next = s;
            next.applyMove(phase1Moves[m]);
            uint16_t nc = next.getUDSlice();
            if (pruneUDSlice[nc] == -1) {
                pruneUDSlice[nc] = d + 1;
                qSlice.push(nc);
            }
        }
    }

    // 4. CornerPerm pruning in G1 (40320 states)
    std::memset(pruneCP, -1, sizeof(pruneCP));
    pruneCP[0] = 0;
    std::queue<uint16_t> qCP;
    qCP.push(0);
    while (!qCP.empty()) {
        uint16_t curr = qCP.front();
        qCP.pop();
        int8_t d = pruneCP[curr];
        State3x3 s;
        s.setCornerPerm(curr);
        for (int m = 0; m < 10; ++m) {
            State3x3 next = s;
            next.applyMove(phase2Moves[m]);
            uint16_t nc = next.getCornerPerm();
            if (pruneCP[nc] == -1) {
                pruneCP[nc] = d + 1;
                qCP.push(nc);
            }
        }
    }

    // 5. Phase2EdgePerm pruning in G1 (40320 states)
    std::memset(pruneEP, -1, sizeof(pruneEP));
    pruneEP[0] = 0;
    std::queue<uint16_t> qEP;
    qEP.push(0);
    while (!qEP.empty()) {
        uint16_t curr = qEP.front();
        qEP.pop();
        int8_t d = pruneEP[curr];
        State3x3 s;
        s.setPhase2EdgePerm(curr);
        for (int m = 0; m < 10; ++m) {
            State3x3 next = s;
            next.applyMove(phase2Moves[m]);
            uint16_t nc = next.getPhase2EdgePerm();
            if (pruneEP[nc] == -1) {
                pruneEP[nc] = d + 1;
                qEP.push(nc);
            }
        }
    }

    // 6. UDSliceSorted in G1 (24 states)
    std::memset(pruneSliceSorted, -1, sizeof(pruneSliceSorted));
    pruneSliceSorted[0] = 0;
    std::queue<uint16_t> qSS;
    qSS.push(0);
    while (!qSS.empty()) {
        uint16_t curr = qSS.front();
        qSS.pop();
        int8_t d = pruneSliceSorted[curr];
        State3x3 s;
        for (int m = 0; m < 10; ++m) {
            State3x3 next = s;
            next.applyMove(phase2Moves[m]);
            uint16_t nc = next.getUDSliceSorted();
            if (pruneSliceSorted[nc] == -1) {
                pruneSliceSorted[nc] = d + 1;
                qSS.push(nc);
            }
        }
    }

    tablesReady = true;
}

static bool idaPhase1(State3x3& state, int depth, int lastAxis, std::vector<Move>& path) {
    if (depth == 0) {
        return (state.getTwist() == 0 && state.getFlip() == 0 && state.getUDSlice() == 0);
    }

    uint16_t tw = state.getTwist();
    uint16_t fl = state.getFlip();
    uint16_t sl = state.getUDSlice();

    int8_t h = std::max(pruneTwist[tw], pruneFlip[fl]);
    if (sl < 495 && pruneUDSlice[sl] > h) h = pruneUDSlice[sl];
    if (h > depth) return false;

    for (int m = 0; m < 18; ++m) {
        const Move& move = phase1Moves[m];
        if (move.axis == lastAxis) continue;
        // Don't commute opposing faces redundantly (e.g., U after D is allowed, D after U is allowed once)
        if (lastAxis >= 0 && (move.axis / 2 == lastAxis / 2) && (move.axis < lastAxis)) continue;

        State3x3 next = state;
        next.applyMove(move);
        path.push_back(move);

        if (idaPhase1(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }

    return false;
}

static bool idaPhase2(State3x3& state, int depth, int lastAxis, std::vector<Move>& path) {
    if (depth == 0) {
        return state.isSolved();
    }

    uint16_t cpCoord = state.getCornerPerm();
    uint16_t epCoord = state.getPhase2EdgePerm();
    uint16_t ssCoord = state.getUDSliceSorted();

    int8_t h = 0;
    if (cpCoord < 40320 && pruneCP[cpCoord] > h) h = pruneCP[cpCoord];
    if (epCoord < 40320 && pruneEP[epCoord] > h) h = pruneEP[epCoord];
    if (ssCoord < 24 && pruneSliceSorted[ssCoord] > h) h = pruneSliceSorted[ssCoord];
    if (h > depth) return false;

    for (int m = 0; m < 10; ++m) {
        const Move& move = phase2Moves[m];
        if (move.axis == lastAxis) continue;
        if (lastAxis >= 0 && (move.axis / 2 == lastAxis / 2) && (move.axis < lastAxis)) continue;

        State3x3 next = state;
        next.applyMove(move);
        path.push_back(move);

        if (idaPhase2(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }

    return false;
}

SolveResult Solver3x3_Min2Phase::solveState(const State3x3& state) {
    initTables();
    auto startTime = std::chrono::high_resolution_clock::now();

    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 3x3 không hợp lệ (sai góc, cạnh hoặc hoán vị lẻ).";
        return result;
    }

    if (state.isSolved()) {
        result.isValid = true;
        result.errorMessage = "";
        result.totalMoves = 0;
        PhaseStep step;
        step.phaseName = "Solved";
        step.notationString = "";
        result.phases.push_back(step);
        return result;
    }

    // Phase 1 Search
    std::vector<Move> p1Path;
    bool p1Found = false;
    State3x3 g1State = state;

    for (int d = 0; d <= 12; ++d) {
        p1Path.clear();
        State3x3 s = state;
        if (idaPhase1(s, d, -1, p1Path)) {
            p1Found = true;
            g1State = state;
            for (const auto& m : p1Path) {
                g1State.applyMove(m);
            }
            break;
        }
    }

    if (!p1Found) {
        result.isValid = false;
        result.errorMessage = "Phase 1: Không thể đưa khối về nhóm G1.";
        return result;
    }

    PhaseStep phase1Step;
    phase1Step.phaseName = "Pha 1 (Đưa về nhóm G1: Twist/Flip/Slice = 0)";
    phase1Step.moves = p1Path;
    std::string not1;
    for (size_t i = 0; i < p1Path.size(); ++i) {
        if (i > 0) not1 += " ";
        not1 += moveToString(p1Path[i], 3);
    }
    phase1Step.notationString = not1;
    result.phases.push_back(phase1Step);
    result.totalMoves += static_cast<int>(p1Path.size());

    // Phase 2 Search
    std::vector<Move> p2Path;
    bool p2Found = false;

    if (g1State.isSolved()) {
        p2Found = true;
    } else {
        for (int d = 1; d <= 18; ++d) {
            p2Path.clear();
            State3x3 s = g1State;
            if (idaPhase2(s, d, -1, p2Path)) {
                p2Found = true;
                break;
            }
        }
    }

    if (!p2Found) {
        result.isValid = false;
        result.errorMessage = "Phase 2: Không thể giải hoàn tất nhóm G1.";
        return result;
    }

    PhaseStep phase2Step;
    phase2Step.phaseName = "Pha 2 (Giải hoàn tất trên nhóm G1)";
    phase2Step.moves = p2Path;
    std::string not2;
    for (size_t i = 0; i < p2Path.size(); ++i) {
        if (i > 0) not2 += " ";
        not2 += moveToString(p2Path[i], 3);
    }
    phase2Step.notationString = not2;
    result.phases.push_back(phase2Step);
    result.totalMoves += static_cast<int>(p2Path.size());

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

SolveResult Solver3x3_Min2Phase::solve(const uint8_t colors[54]) {
    if (!State3x3::validateFacelets(colors)) {
        SolveResult r;
        r.isValid = false;
        r.errorMessage = "Số lượng 6 màu không đồng đều (mỗi màu phải đủ 9 ô).";
        return r;
    }
    State3x3 s = State3x3::fromFacelets(colors);
    return solveState(s);
}
