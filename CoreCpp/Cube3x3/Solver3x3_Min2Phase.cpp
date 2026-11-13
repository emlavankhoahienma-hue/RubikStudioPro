#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include <chrono>
#include <queue>
#include <cstring>
#include <algorithm>
#include <vector>

namespace {

static const Move phase1Moves[18] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {1, 0x01, 1}, {1, 0x01, 2}, {1, 0x01, 3}, // D, D2, D'
    {2, 0x01, 1}, {2, 0x01, 2}, {2, 0x01, 3}, // L, L2, L'
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R, R2, R'
    {4, 0x01, 1}, {4, 0x01, 2}, {4, 0x01, 3}, // F, F2, F'
    {5, 0x01, 1}, {5, 0x01, 2}, {5, 0x01, 3}  // B, B2, B'
};

// Phase 2 G1 move subset: U, U2, U', D, D2, D', L2, R2, F2, B2 (indices in phase1Moves)
static const int phase2MoveIdx[10] = {0, 1, 2, 3, 4, 5, 7, 10, 13, 16};

static bool tablesReady = false;

// 1D Coordinate Move Tables
static uint16_t twistMove[2187][18];
static uint16_t flipMove[2048][18];
static uint16_t sliceMove[495][18];
static uint16_t cpMove[40320][10];
static uint16_t epMove[40320][10];
static uint8_t  ssMove[24][10];

// 2D Combined Pruning Tables (~4.0 MB total)
static int8_t pruneTwistSlice[2187 * 495];
static int8_t pruneFlipSlice[2048 * 495];
static int8_t pruneCPSlice[40320 * 24];
static int8_t pruneEPSlice[40320 * 24];

static void setUDSliceSorted(State3x3& s, uint16_t coord) {
    static const uint16_t fact[4] = {6, 2, 1, 1};
    uint8_t avail[4] = {8, 9, 10, 11};
    int len = 4;
    for (int i = 0; i < 3; ++i) {
        int idx = coord / fact[i];
        coord %= fact[i];
        s.ep[8 + i] = avail[idx];
        for (int j = idx; j < len - 1; ++j) avail[j] = avail[j + 1];
        len--;
    }
    s.ep[11] = avail[0];
}

static void initTablesImpl() {
    if (tablesReady) return;

    // 1. Build Move Tables
    for (uint16_t i = 0; i < 2187; ++i) {
        State3x3 s;
        s.setTwist(i);
        for (int m = 0; m < 18; ++m) {
            State3x3 nx = s;
            nx.applyMove(phase1Moves[m]);
            twistMove[i][m] = nx.getTwist();
        }
    }
    for (uint16_t i = 0; i < 2048; ++i) {
        State3x3 s;
        s.setFlip(i);
        for (int m = 0; m < 18; ++m) {
            State3x3 nx = s;
            nx.applyMove(phase1Moves[m]);
            flipMove[i][m] = nx.getFlip();
        }
    }
    for (uint16_t i = 0; i < 495; ++i) {
        State3x3 s;
        s.setUDSlice(i);
        for (int m = 0; m < 18; ++m) {
            State3x3 nx = s;
            nx.applyMove(phase1Moves[m]);
            sliceMove[i][m] = nx.getUDSlice();
        }
    }
    for (uint16_t i = 0; i < 40320; ++i) {
        State3x3 s;
        s.setCornerPerm(i);
        for (int m = 0; m < 10; ++m) {
            State3x3 nx = s;
            nx.applyMove(phase1Moves[phase2MoveIdx[m]]);
            cpMove[i][m] = nx.getCornerPerm();
        }
    }
    for (uint16_t i = 0; i < 40320; ++i) {
        State3x3 s;
        s.setPhase2EdgePerm(i);
        for (int m = 0; m < 10; ++m) {
            State3x3 nx = s;
            nx.applyMove(phase1Moves[phase2MoveIdx[m]]);
            epMove[i][m] = nx.getPhase2EdgePerm();
        }
    }
    for (uint16_t i = 0; i < 24; ++i) {
        State3x3 s;
        setUDSliceSorted(s, i);
        for (int m = 0; m < 10; ++m) {
            State3x3 nx = s;
            nx.applyMove(phase1Moves[phase2MoveIdx[m]]);
            ssMove[i][m] = static_cast<uint8_t>(nx.getUDSliceSorted());
        }
    }

    // 2. Build 2D BFS Pruning Tables
    // 2a. Twist x UDSlice (1,082,565 states)
    std::memset(pruneTwistSlice, -1, sizeof(pruneTwistSlice));
    {
        std::queue<uint32_t> q;
        pruneTwistSlice[0] = 0;
        q.push(0);
        while (!q.empty()) {
            uint32_t cur = q.front();
            q.pop();
            int8_t d = pruneTwistSlice[cur];
            uint16_t tw = static_cast<uint16_t>(cur / 495);
            uint16_t sl = static_cast<uint16_t>(cur % 495);
            for (int m = 0; m < 18; ++m) {
                uint32_t nxt = static_cast<uint32_t>(twistMove[tw][m]) * 495 + sliceMove[sl][m];
                if (pruneTwistSlice[nxt] == -1) {
                    pruneTwistSlice[nxt] = d + 1;
                    q.push(nxt);
                }
            }
        }
    }

    // 2b. Flip x UDSlice (1,013,760 states)
    std::memset(pruneFlipSlice, -1, sizeof(pruneFlipSlice));
    {
        std::queue<uint32_t> q;
        pruneFlipSlice[0] = 0;
        q.push(0);
        while (!q.empty()) {
            uint32_t cur = q.front();
            q.pop();
            int8_t d = pruneFlipSlice[cur];
            uint16_t fl = static_cast<uint16_t>(cur / 495);
            uint16_t sl = static_cast<uint16_t>(cur % 495);
            for (int m = 0; m < 18; ++m) {
                uint32_t nxt = static_cast<uint32_t>(flipMove[fl][m]) * 495 + sliceMove[sl][m];
                if (pruneFlipSlice[nxt] == -1) {
                    pruneFlipSlice[nxt] = d + 1;
                    q.push(nxt);
                }
            }
        }
    }

    // 2c. CornerPerm x UDSliceSorted (967,680 states)
    std::memset(pruneCPSlice, -1, sizeof(pruneCPSlice));
    {
        std::queue<uint32_t> q;
        pruneCPSlice[0] = 0;
        q.push(0);
        while (!q.empty()) {
            uint32_t cur = q.front();
            q.pop();
            int8_t d = pruneCPSlice[cur];
            uint16_t cp = static_cast<uint16_t>(cur / 24);
            uint8_t  ss = static_cast<uint8_t>(cur % 24);
            for (int m = 0; m < 10; ++m) {
                uint32_t nxt = static_cast<uint32_t>(cpMove[cp][m]) * 24 + ssMove[ss][m];
                if (pruneCPSlice[nxt] == -1) {
                    pruneCPSlice[nxt] = d + 1;
                    q.push(nxt);
                }
            }
        }
    }

    // 2d. EdgePerm x UDSliceSorted (967,680 states)
    std::memset(pruneEPSlice, -1, sizeof(pruneEPSlice));
    {
        std::queue<uint32_t> q;
        pruneEPSlice[0] = 0;
        q.push(0);
        while (!q.empty()) {
            uint32_t cur = q.front();
            q.pop();
            int8_t d = pruneEPSlice[cur];
            uint16_t ep = static_cast<uint16_t>(cur / 24);
            uint8_t  ss = static_cast<uint8_t>(cur % 24);
            for (int m = 0; m < 10; ++m) {
                uint32_t nxt = static_cast<uint32_t>(epMove[ep][m]) * 24 + ssMove[ss][m];
                if (pruneEPSlice[nxt] == -1) {
                    pruneEPSlice[nxt] = d + 1;
                    q.push(nxt);
                }
            }
        }
    }

    tablesReady = true;
}

static bool idaPhase2(uint16_t cp, uint16_t ep, uint8_t ss, int depth, int lastAxis, std::vector<int>& p2Idx) {
    if (depth == 0) {
        return (cp == 0 && ep == 0 && ss == 0);
    }
    int8_t h = std::max(pruneCPSlice[static_cast<uint32_t>(cp) * 24 + ss],
                        pruneEPSlice[static_cast<uint32_t>(ep) * 24 + ss]);
    if (h > depth) return false;

    for (int m = 0; m < 10; ++m) {
        int ax = phase1Moves[phase2MoveIdx[m]].axis;
        if (ax == lastAxis) continue;
        if (lastAxis >= 0 && (ax / 2 == lastAxis / 2) && (ax < lastAxis)) continue;

        p2Idx.push_back(phase2MoveIdx[m]);
        if (idaPhase2(cpMove[cp][m], epMove[ep][m], ssMove[ss][m], depth - 1, ax, p2Idx)) {
            return true;
        }
        p2Idx.pop_back();
    }
    return false;
}

struct SearchContext {
    const State3x3& root;
    std::chrono::high_resolution_clock::time_point startTime;
    int bestTotal;
    std::vector<int> bestP1;
    std::vector<int> bestP2;
    std::vector<int> curP1;
    bool timedOut;
};

static void searchPhase1(SearchContext& ctx, uint16_t tw, uint16_t fl, uint16_t sl, int depth, int lastAxis) {
    if (ctx.timedOut) return;

    if (depth == 0) {
        if (tw != 0 || fl != 0 || sl != 0) return;

        // Compute G1 coordinates once for this leaf
        State3x3 g1 = ctx.root;
        for (int idx : ctx.curP1) g1.applyMove(phase1Moves[idx]);
        uint16_t cp = g1.getCornerPerm();
        uint16_t ep = g1.getPhase2EdgePerm();
        uint8_t  ss = static_cast<uint8_t>(g1.getUDSliceSorted());

        int maxP2 = ctx.bestTotal - 1 - static_cast<int>(ctx.curP1.size());
        int minH = std::max(pruneCPSlice[static_cast<uint32_t>(cp) * 24 + ss],
                            pruneEPSlice[static_cast<uint32_t>(ep) * 24 + ss]);
        if (minH > maxP2) return;

        std::vector<int> p2Idx;
        int lastP1Ax = ctx.curP1.empty() ? -1 : phase1Moves[ctx.curP1.back()].axis;
        for (int d2 = minH; d2 <= maxP2; ++d2) {
            p2Idx.clear();
            if (idaPhase2(cp, ep, ss, d2, (d2 == 0 ? -1 : lastP1Ax), p2Idx)) {
                int tot = static_cast<int>(ctx.curP1.size() + p2Idx.size());
                if (tot < ctx.bestTotal) {
                    ctx.bestTotal = tot;
                    ctx.bestP1 = ctx.curP1;
                    ctx.bestP2 = p2Idx;
                }
                break;
            }
        }

        // Check time budget once we have a valid solution
        if (ctx.bestTotal <= 20) {
            ctx.timedOut = true;
        } else if (ctx.bestTotal < 999) {
            auto now = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(now - ctx.startTime).count();
            if (ms > 30.0) ctx.timedOut = true;
        }
        return;
    }

    int8_t h = std::max(pruneTwistSlice[static_cast<uint32_t>(tw) * 495 + sl],
                        pruneFlipSlice[static_cast<uint32_t>(fl) * 495 + sl]);
    if (h > depth) return;

    for (int m = 0; m < 18; ++m) {
        int ax = phase1Moves[m].axis;
        if (ax == lastAxis) continue;
        if (lastAxis >= 0 && (ax / 2 == lastAxis / 2) && (ax < lastAxis)) continue;

        ctx.curP1.push_back(m);
        searchPhase1(ctx, twistMove[tw][m], flipMove[fl][m], sliceMove[sl][m], depth - 1, ax);
        ctx.curP1.pop_back();
        if (ctx.timedOut) return;
    }
}

} // namespace

void Solver3x3_Min2Phase::initTables() {
    initTablesImpl();
}

SolveResult Solver3x3_Min2Phase::solve(const uint8_t colors[54]) {
    if (!State3x3::validateFacelets(colors)) {
        SolveResult err;
        err.isValid = false;
        err.errorMessage = "Trạng thái 54 ô màu 3x3 không hợp lệ (số lượng màu không đều).";
        err.totalMoves = 0;
        err.solveTimeMs = 0;
        return err;
    }
    State3x3 state = State3x3::fromFacelets(colors);
    return solveState(state);
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

    uint16_t tw0 = state.getTwist();
    uint16_t fl0 = state.getFlip();
    uint16_t sl0 = state.getUDSlice();

    SearchContext ctx{state, startTime, 999, {}, {}, {}, false};
    for (int d1 = 0; d1 <= 12 && !ctx.timedOut; ++d1) {
        ctx.curP1.clear();
        searchPhase1(ctx, tw0, fl0, sl0, d1, -1);
    }

    if (ctx.bestTotal == 999) {
        result.isValid = false;
        result.errorMessage = "Không tìm thấy lời giải Two-Phase.";
        return result;
    }

    if (!ctx.bestP1.empty()) {
        PhaseStep phase1Step;
        phase1Step.phaseName = "Pha 1 (Đưa về nhóm G1: Twist/Flip/Slice = 0)";
        for (int idx : ctx.bestP1) phase1Step.moves.push_back(phase1Moves[idx]);
        for (size_t i = 0; i < phase1Step.moves.size(); ++i) {
            if (i > 0) phase1Step.notationString += " ";
            phase1Step.notationString += moveToString(phase1Step.moves[i], 3);
        }
        result.phases.push_back(phase1Step);
        result.totalMoves += static_cast<int>(phase1Step.moves.size());
    }

    if (!ctx.bestP2.empty()) {
        PhaseStep phase2Step;
        phase2Step.phaseName = "Pha 2 (Giải hoàn tất trên nhóm G1)";
        for (int idx : ctx.bestP2) phase2Step.moves.push_back(phase1Moves[idx]);
        for (size_t i = 0; i < phase2Step.moves.size(); ++i) {
            if (i > 0) phase2Step.notationString += " ";
            phase2Step.notationString += moveToString(phase2Step.moves[i], 3);
        }
        result.phases.push_back(phase2Step);
        result.totalMoves += static_cast<int>(phase2Step.moves.size());
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    return result;
}
