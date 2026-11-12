#include "Cube2x2/Solver2x2_Optimal.hpp"
#include <queue>
#include <vector>
#include <chrono>
#include <mutex>
#include <cstring>

static int8_t pruneOrient[729];
static int8_t prunePerm[5040];
static bool tablesInitialized = false;
static std::mutex initMutex;

// 9 standard moves on U, R, F
static const Move allowedMoves[9] = {
    {0, 0x01, 1}, {0, 0x01, 2}, {0, 0x01, 3}, // U, U2, U'
    {3, 0x01, 1}, {3, 0x01, 2}, {3, 0x01, 3}, // R, R2, R'
    {4, 0x01, 1}, {4, 0x01, 2}, {4, 0x01, 3}  // F, F2, F'
};

void Solver2x2_Optimal::initTables() {
    std::lock_guard<std::mutex> lock(initMutex);
    if (tablesInitialized) return;

    // 1. BFS for Orientation pruning table (729 states)
    std::memset(pruneOrient, -1, sizeof(pruneOrient));
    pruneOrient[0] = 0;
    std::queue<uint16_t> qOrient;
    qOrient.push(0);

    while (!qOrient.empty()) {
        uint16_t curr = qOrient.front();
        qOrient.pop();
        int8_t dist = pruneOrient[curr];

        State2x2 s;
        s.setOrientCoord(curr);

        for (int m = 0; m < 9; ++m) {
            State2x2 next = s;
            next.applyMove(allowedMoves[m]);
            uint16_t nextCoord = next.getOrientCoord();
            if (pruneOrient[nextCoord] == -1) {
                pruneOrient[nextCoord] = dist + 1;
                qOrient.push(nextCoord);
            }
        }
    }

    // 2. BFS for Permutation pruning table (5040 states)
    std::memset(prunePerm, -1, sizeof(prunePerm));
    prunePerm[0] = 0;
    std::queue<uint16_t> qPerm;
    qPerm.push(0);

    while (!qPerm.empty()) {
        uint16_t curr = qPerm.front();
        qPerm.pop();
        int8_t dist = prunePerm[curr];

        State2x2 s;
        s.setPermCoord(curr);

        for (int m = 0; m < 9; ++m) {
            State2x2 next = s;
            next.applyMove(allowedMoves[m]);
            uint16_t nextCoord = next.getPermCoord();
            if (prunePerm[nextCoord] == -1) {
                prunePerm[nextCoord] = dist + 1;
                qPerm.push(nextCoord);
            }
        }
    }

    tablesInitialized = true;
}

static bool idaSearch(State2x2& state, int depth, int lastAxis, std::vector<Move>& path) {
    if (depth == 0) {
        return state.isSolved();
    }

    uint16_t oCoord = state.getOrientCoord();
    uint16_t pCoord = state.getPermCoord();
    int8_t h = std::max(pruneOrient[oCoord], prunePerm[pCoord]);
    if (h > depth) return false;

    for (int m = 0; m < 9; ++m) {
        const Move& move = allowedMoves[m];
        if (move.axis == lastAxis) continue;

        State2x2 next = state;
        next.applyMove(move);
        path.push_back(move);

        if (idaSearch(next, depth - 1, move.axis, path)) {
            return true;
        }

        path.pop_back();
    }

    return false;
}

SolveResult Solver2x2_Optimal::solveState(const State2x2& state) {
    initTables();
    auto startTime = std::chrono::high_resolution_clock::now();

    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;
    result.solveTimeMs = 0;

    if (!result.isValid) {
        result.errorMessage = "Trạng thái khối 2x2 không hợp lệ hoặc bị lật góc vật lý.";
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

    std::vector<Move> path;
    bool found = false;

    // IDA* search up to depth 11 (God's number for 2x2 HTM is 11)
    for (int depth = 1; depth <= 11; ++depth) {
        path.clear();
        State2x2 s = state;
        if (idaSearch(s, depth, -1, path)) {
            found = true;
            break;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    if (found) {
        result.isValid = true;
        result.totalMoves = static_cast<int>(path.size());

        PhaseStep step;
        step.phaseName = "Optimal Solution";
        step.moves = path;
        std::string notation;
        for (size_t i = 0; i < path.size(); ++i) {
            if (i > 0) notation += " ";
            notation += moveToString(path[i], 2);
        }
        step.notationString = notation;
        result.phases.push_back(step);
    } else {
        result.isValid = false;
        result.errorMessage = "Không tìm thấy lời giải trong giới hạn 11 bước.";
    }

    return result;
}

SolveResult Solver2x2_Optimal::solve(const uint8_t colors[24]) {
    State2x2 state = State2x2::fromFacelets(colors);
    return solveState(state);
}
