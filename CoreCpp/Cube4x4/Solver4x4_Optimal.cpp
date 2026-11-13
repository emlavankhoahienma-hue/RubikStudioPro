#include "Cube4x4/Solver4x4_Optimal.hpp"
#include <chrono>
#include <vector>
#include <array>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cstring>
#include <functional>

namespace {

// ─── Move Helpers ─────────────────────────────────────────────────────────────

inline Move makeMove(uint8_t axis, uint8_t power, uint8_t sliceMask) {
    return Move{axis, sliceMask, power};
}

static void applySeq(State4x4& s, const std::vector<Move>& seq) {
    for (const auto& m : seq) s.applyMove(m);
}

// ─── Phase 1: Center Reduction (Phased Beam Search) ──────────────────────────

static const int kCenterIdx[4] = {5, 6, 9, 10};

static int countFaceCenter(const State4x4& s, int face, uint8_t targetColor) {
    int cnt = 0;
    int base = face * 16;
    for (int i = 0; i < 4; ++i) {
        if (s.facelets[base + kCenterIdx[i]] == targetColor) cnt++;
    }
    return cnt;
}

static bool areCentersSolvedBOY(const State4x4& s) {
    for (int f = 0; f < 6; ++f) {
        if (countFaceCenter(s, f, static_cast<uint8_t>(f)) != 4) return false;
    }
    return true;
}

static uint64_t hashCenters(const State4x4& s) {
    uint64_t h = 14695981039346656037ULL;
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            h ^= s.facelets[f * 16 + kCenterIdx[i]];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

// Stage 1a: Solve U center (face 0 == 0) progressively (k -> k+1)
static bool solveUCenter(State4x4& state, std::vector<Move>& outMoves) {
    if (countFaceCenter(state, 0, 0) == 4) return true;

    std::vector<Move> moves;
    for (uint8_t ax = 0; ax < 6; ++ax) {
        for (uint8_t pw = 1; pw <= 3; ++pw) {
            moves.push_back(makeMove(ax, pw, 0x01)); // outer
            moves.push_back(makeMove(ax, pw, 0x03)); // wide
        }
    }

    for (int target = countFaceCenter(state, 0, 0) + 1; target <= 4; ++target) {
        if (countFaceCenter(state, 0, 0) >= target) continue;

        struct Node {
            State4x4 s;
            std::vector<Move> path;
        };
        std::queue<Node> q;
        q.push({state, {}});
        std::unordered_set<uint64_t> visited;
        visited.insert(hashCenters(state));

        bool found = false;
        for (int iter = 0; !q.empty() && iter < 25000 && !found; ++iter) {
            Node cur = std::move(q.front());
            q.pop();
            if (cur.path.size() >= 6) continue;

            for (const auto& m : moves) {
                if (!cur.path.empty() && cur.path.back().axis == m.axis && cur.path.back().sliceMask == m.sliceMask)
                    continue;
                State4x4 ns = cur.s;
                ns.applyMove(m);
                uint64_t h = hashCenters(ns);
                if (!visited.insert(h).second) continue;

                auto npath = cur.path;
                npath.push_back(m);

                if (countFaceCenter(ns, 0, 0) >= target) {
                    state = ns;
                    outMoves.insert(outMoves.end(), npath.begin(), npath.end());
                    found = true;
                    break;
                }
                q.push({ns, std::move(npath)});
            }
        }
        if (!found) return false;
    }
    return countFaceCenter(state, 0, 0) == 4;
}

// Stage 1b: Solve D center (face 1 == 1) while preserving U center (face 0 == 0)
static bool solveDCenter(State4x4& state, std::vector<Move>& outMoves) {
    if (countFaceCenter(state, 1, 1) == 4) return true;

    // Build move macros that preserve U center:
    // 1. Outer moves on D, F, B, L, R (never touch centers)
    // 2. Wide triggers: W Dk W' (W in {Rw, Rw', Lw, Lw', Fw, Fw', Bw, Bw'}, k in {1, 2, 3})
    std::vector<std::vector<Move>> macros;
    for (uint8_t ax = 1; ax < 6; ++ax) {
        for (uint8_t pw = 1; pw <= 3; ++pw) {
            macros.push_back({makeMove(ax, pw, 0x01)});
        }
    }
    const uint8_t wideAxes[4] = {2, 3, 4, 5}; // L, R, F, B
    for (uint8_t ax : wideAxes) {
        for (uint8_t wp : {uint8_t(1), uint8_t(3)}) {
            uint8_t inv = (wp == 1) ? 3 : 1;
            for (uint8_t dp = 1; dp <= 3; ++dp) {
                macros.push_back({
                    makeMove(ax, wp, 0x03),
                    makeMove(1, dp, 0x01), // Dk
                    makeMove(ax, inv, 0x03)
                });
            }
        }
    }

    struct Node {
        State4x4 s;
        std::vector<Move> path;
        int score;
    };

    std::vector<Node> beam;
    beam.push_back({state, {}, countFaceCenter(state, 1, 1)});
    std::unordered_set<uint64_t> visited;
    visited.insert(hashCenters(state));

    for (int depth = 0; depth < 14; ++depth) {
        std::vector<Node> nextBeam;
        for (const auto& node : beam) {
            for (const auto& mac : macros) {
                State4x4 ns = node.s;
                applySeq(ns, mac);
                if (countFaceCenter(ns, 0, 0) != 4) continue;
                uint64_t h = hashCenters(ns);
                if (!visited.insert(h).second) continue;

                auto npath = node.path;
                npath.insert(npath.end(), mac.begin(), mac.end());

                int dCnt = countFaceCenter(ns, 1, 1);
                if (dCnt == 4) {
                    state = ns;
                    outMoves.insert(outMoves.end(), npath.begin(), npath.end());
                    return true;
                }
                nextBeam.push_back({ns, std::move(npath), dCnt});
            }
        }
        if (nextBeam.empty()) break;
        std::sort(nextBeam.begin(), nextBeam.end(), [](const Node& a, const Node& b) {
            return a.score > b.score;
        });
        if (nextBeam.size() > 192) nextBeam.resize(192);
        beam = std::move(nextBeam);
    }
    return false;
}

// Stage 1c: Solve F, B, L, R centers while preserving U and D centers
static bool runCenterBeam(State4x4& state, std::vector<Move>& outMoves,
                          const std::vector<std::vector<Move>>& macros,
                          int beamWidth, int maxDepth,
                          const std::function<bool(const State4x4&)>& validPred,
                          const std::function<bool(const State4x4&)>& goalPred,
                          const std::function<int(const State4x4&)>& scoreFn) {
    if (goalPred(state)) return true;
    struct Node {
        State4x4 s;
        std::vector<Move> path;
        int score;
    };
    std::vector<Node> beam;
    beam.push_back({state, {}, scoreFn(state)});
    std::unordered_set<uint64_t> visited;
    visited.insert(hashCenters(state));

    for (int depth = 0; depth < maxDepth; ++depth) {
        std::vector<Node> nextBeam;
        for (const auto& node : beam) {
            for (const auto& mac : macros) {
                State4x4 ns = node.s;
                applySeq(ns, mac);
                if (!validPred(ns)) continue;
                uint64_t h = hashCenters(ns);
                if (!visited.insert(h).second) continue;

                auto npath = node.path;
                npath.insert(npath.end(), mac.begin(), mac.end());

                if (goalPred(ns)) {
                    state = ns;
                    outMoves.insert(outMoves.end(), npath.begin(), npath.end());
                    return true;
                }
                nextBeam.push_back({ns, std::move(npath), scoreFn(ns)});
            }
        }
        if (nextBeam.empty()) break;
        std::sort(nextBeam.begin(), nextBeam.end(), [](const Node& a, const Node& b) {
            return a.score > b.score;
        });
        if ((int)nextBeam.size() > beamWidth) nextBeam.resize(beamWidth);
        beam = std::move(nextBeam);
    }
    return false;
}

static bool solveEquatorCenters(State4x4& state, std::vector<Move>& outMoves, std::string& errStage) {
    if (areCentersSolvedBOY(state)) return true;

    // Micro-stage 1: Solve F center (preserving U, D)
    {
        std::vector<std::vector<Move>> macros;
        for (uint8_t pw = 1; pw <= 3; ++pw) {
            macros.push_back({makeMove(0, pw, 0x03)}); // Uw
            macros.push_back({makeMove(1, pw, 0x03)}); // Dw
        }
        for (uint8_t ax = 2; ax < 6; ++ax) {
            for (uint8_t pw = 1; pw <= 3; ++pw) {
                macros.push_back({makeMove(ax, pw, 0x01)});
            }
        }
        for (uint8_t ud : {uint8_t(0), uint8_t(1)}) {
            for (uint8_t wp = 1; wp <= 3; ++wp) {
                uint8_t inv = (wp == 1) ? 3 : (wp == 3 ? 1 : 2);
                for (uint8_t fk = 1; fk <= 3; ++fk) {
                    macros.push_back({makeMove(ud, wp, 0x03), makeMove(4, fk, 0x01), makeMove(ud, inv, 0x03)});
                }
            }
        }
        if (!runCenterBeam(state, outMoves, macros, 256, 12,
                [](const State4x4& s) { return countFaceCenter(s, 0, 0) == 4 && countFaceCenter(s, 1, 1) == 4; },
                [](const State4x4& s) { return countFaceCenter(s, 4, 4) == 4; },
                [](const State4x4& s) { return countFaceCenter(s, 4, 4); })) {
            errStage = "1c-F";
            return false;
        }
    }

    // Micro-stage 2: Solve B center (preserving U, D, F)
    {
        std::vector<std::vector<Move>> macros;
        for (uint8_t ax : {uint8_t(2), uint8_t(3), uint8_t(5)}) { // L, R, B outer
            for (uint8_t pw = 1; pw <= 3; ++pw) {
                macros.push_back({makeMove(ax, pw, 0x01)});
            }
        }
        // Uw' Lk Uw, Uw Rk Uw', Dw Lk Dw', Dw' Rk Dw (preserve F, U, D)
        for (uint8_t k = 1; k <= 3; ++k) {
            macros.push_back({makeMove(0, 3, 0x03), makeMove(2, k, 0x01), makeMove(0, 1, 0x03)});
            macros.push_back({makeMove(0, 1, 0x03), makeMove(3, k, 0x01), makeMove(0, 3, 0x03)});
            macros.push_back({makeMove(1, 1, 0x03), makeMove(2, k, 0x01), makeMove(1, 3, 0x03)});
            macros.push_back({makeMove(1, 3, 0x03), makeMove(3, k, 0x01), makeMove(1, 1, 0x03)});
        }
        if (!runCenterBeam(state, outMoves, macros, 128, 12,
                [](const State4x4& s) {
                    return countFaceCenter(s, 0, 0) == 4 && countFaceCenter(s, 1, 1) == 4 && countFaceCenter(s, 4, 4) == 4;
                },
                [](const State4x4& s) { return countFaceCenter(s, 5, 5) == 4; },
                [](const State4x4& s) { return countFaceCenter(s, 5, 5) * 10 + countFaceCenter(s, 2, 2) + countFaceCenter(s, 3, 3); })) {
            errStage = "1c-B";
            return false;
        }
    }

    // Micro-stage 3: Solve L and R centers (preserving U, D, F, B)
    {
        std::vector<std::vector<Move>> macros;
        for (uint8_t ax : {uint8_t(2), uint8_t(3)}) { // L, R outer
            for (uint8_t pw = 1; pw <= 3; ++pw) {
                macros.push_back({makeMove(ax, pw, 0x01)});
            }
        }
        for (uint8_t ud : {uint8_t(0), uint8_t(1)}) { // Uw2, Dw2
            for (uint8_t ax : {uint8_t(2), uint8_t(3)}) { // L, R
                for (uint8_t k = 1; k <= 3; ++k) {
                    macros.push_back({makeMove(ud, 2, 0x03), makeMove(ax, k, 0x01), makeMove(ud, 2, 0x03)});
                }
            }
        }
        if (!runCenterBeam(state, outMoves, macros, 128, 10,
                [](const State4x4& s) {
                    return countFaceCenter(s, 0, 0) == 4 && countFaceCenter(s, 1, 1) == 4 &&
                           countFaceCenter(s, 4, 4) == 4 && countFaceCenter(s, 5, 5) == 4;
                },
                [](const State4x4& s) { return areCentersSolvedBOY(s); },
                [](const State4x4& s) { return countFaceCenter(s, 2, 2) + countFaceCenter(s, 3, 3); })) {
            errStage = "1c-LR";
            return false;
        }
    }

    return areCentersSolvedBOY(state);
}

static bool solveCenters4x4(State4x4& state, std::vector<Move>& outMoves, std::string& errStage) {
    if (areCentersSolvedBOY(state)) return true;
    if (!solveUCenter(state, outMoves)) { errStage = "1a-U"; return false; }
    if (!solveDCenter(state, outMoves)) { errStage = "1b-D"; return false; }
    if (!solveEquatorCenters(state, outMoves, errStage)) return false;
    return areCentersSolvedBOY(state);
}

// ─── Phase 2: Deterministic Wing Pairing ─────────────────────────────────────

struct WingPairDef {
    int w1_f1, w1_f2;
    int w2_f1, w2_f2;
};

static const WingPairDef kWingPairs[12] = {
    {0*16 + 7,  3*16 + 1,  0*16 + 11, 3*16 + 2},  // 0: UR
    {0*16 + 13, 4*16 + 1,  0*16 + 14, 4*16 + 2},  // 1: UF
    {0*16 + 4,  2*16 + 1,  0*16 + 8,  2*16 + 2},  // 2: UL
    {0*16 + 1,  5*16 + 2,  0*16 + 2,  5*16 + 1},  // 3: UB
    {1*16 + 7,  3*16 + 14, 1*16 + 11, 3*16 + 13}, // 4: DR
    {1*16 + 1,  4*16 + 13, 1*16 + 2,  4*16 + 14}, // 5: DF
    {1*16 + 4,  2*16 + 13, 1*16 + 8,  2*16 + 14}, // 6: DL
    {1*16 + 13, 5*16 + 14, 1*16 + 14, 5*16 + 13}, // 7: DB
    {4*16 + 7,  3*16 + 4,  4*16 + 11, 3*16 + 8},  // 8: FR
    {4*16 + 4,  2*16 + 7,  4*16 + 8,  2*16 + 11}, // 9: FL
    {5*16 + 7,  2*16 + 4,  5*16 + 11, 2*16 + 8},  // 10: BL
    {5*16 + 4,  3*16 + 7,  5*16 + 8,  3*16 + 11}  // 11: BR
};

static bool isDedgePaired(const State4x4& s, int e) {
    return s.facelets[kWingPairs[e].w1_f1] == s.facelets[kWingPairs[e].w2_f1] &&
           s.facelets[kWingPairs[e].w1_f2] == s.facelets[kWingPairs[e].w2_f2];
}

static int countPairedDedges(const State4x4& s) {
    int cnt = 0;
    for (int e = 0; e < 12; ++e) {
        if (isDedgePaired(s, e)) cnt++;
    }
    return cnt;
}

// Flip algorithm on FR: R U R' F R' F' R
static std::vector<Move> makeSliceFlipSlice(uint8_t uwPower) {
    uint8_t uwInv = (uwPower == 1) ? 3 : (uwPower == 3 ? 1 : 2);
    return {
        makeMove(0, uwPower, 0x03), // Uw / Uw' / Uw2
        makeMove(3, 1, 0x01),       // R
        makeMove(0, 1, 0x01),       // U
        makeMove(3, 3, 0x01),       // R'
        makeMove(4, 1, 0x01),       // F
        makeMove(3, 3, 0x01),       // R'
        makeMove(4, 3, 0x01),       // F'
        makeMove(3, 1, 0x01),       // R
        makeMove(0, uwInv, 0x03)    // Uw' / Uw / Uw2
    };
}

// Bring an unpaired dedge to FR (slot 8) using outer moves only (max depth 4)
static bool bringUnpairedToFR(State4x4& state, std::vector<Move>& outMoves) {
    if (!isDedgePaired(state, 8)) return true;

    struct Node {
        State4x4 s;
        std::vector<Move> path;
    };
    std::queue<Node> q;
    q.push({state, {}});

    for (int iter = 0; !q.empty() && iter < 1500; ++iter) {
        Node cur = std::move(q.front());
        q.pop();
        if (cur.path.size() >= 4) continue;
        for (uint8_t ax = 0; ax < 6; ++ax) {
            if (!cur.path.empty() && cur.path.back().axis == ax) continue;
            for (uint8_t pw = 1; pw <= 3; ++pw) {
                State4x4 ns = cur.s;
                Move m = makeMove(ax, pw, 0x01);
                ns.applyMove(m);
                auto npath = cur.path;
                npath.push_back(m);
                if (!isDedgePaired(ns, 8)) {
                    state = ns;
                    outMoves.insert(outMoves.end(), npath.begin(), npath.end());
                    return true;
                }
                q.push({ns, std::move(npath)});
            }
        }
    }
    return false;
}

// Hash only the 24 wing stickers for outer-BFS
static uint64_t hashWings(const State4x4& s) {
    uint64_t h = 14695981039346656037ULL;
    for (int e = 0; e < 12; ++e) {
        h ^= s.facelets[kWingPairs[e].w1_f1]; h *= 1099511628211ULL;
        h ^= s.facelets[kWingPairs[e].w1_f2]; h *= 1099511628211ULL;
        h ^= s.facelets[kWingPairs[e].w2_f1]; h *= 1099511628211ULL;
        h ^= s.facelets[kWingPairs[e].w2_f2]; h *= 1099511628211ULL;
    }
    return h;
}

static bool pairAllWings(State4x4& state, std::vector<Move>& outMoves) {
    const std::vector<Move> trig1 = makeSliceFlipSlice(3); // Uw' (R U R' F R' F' R) Uw
    const std::vector<Move> trig2 = makeSliceFlipSlice(1); // Uw  (R U R' F R' F' R) Uw'

    for (int step = 0; step < 16 && !state.areWingsPaired(); ++step) {
        if (!bringUnpairedToFR(state, outMoves)) return false;

        int curPaired = countPairedDedges(state);
        uint8_t c1a = state.facelets[kWingPairs[8].w1_f1];
        uint8_t c1b = state.facelets[kWingPairs[8].w1_f2];

        // Check if trig1 or trig2 works immediately, or search {U, D, L, B} outer moves (fixing FR)
        // to bring the partner wing of FR.w1 into FL or BR so trig1 or trig2 increases pairedCount
        // without breaking any already-paired dedges.
        auto tryTriggers = [&](const State4x4& s, std::vector<Move>& bestTrig) -> bool {
            for (const auto* trig : {&trig1, &trig2}) {
                State4x4 test = s;
                applySeq(test, *trig);
                if (countPairedDedges(test) > curPaired) {
                    bestTrig = *trig;
                    return true;
                }
            }
            return false;
        };

        std::vector<Move> chosenTrig;
        if (tryTriggers(state, chosenTrig)) {
            applySeq(state, chosenTrig);
            outMoves.insert(outMoves.end(), chosenTrig.begin(), chosenTrig.end());
            continue;
        }

        // BFS over {U(0), D(1), L(2), B(5)} outer moves (never touches FR)
        struct Node {
            State4x4 s;
            std::vector<Move> path;
        };
        std::queue<Node> q;
        q.push({state, {}});
        std::unordered_set<uint64_t> visited;
        visited.insert(hashWings(state));

        bool found = false;
        const uint8_t safeAxes[4] = {0, 1, 2, 5}; // U, D, L, B

        for (int iter = 0; !q.empty() && iter < 12000 && !found; ++iter) {
            Node cur = std::move(q.front());
            q.pop();
            if (cur.path.size() >= 6) continue;

            for (uint8_t ax : safeAxes) {
                if (!cur.path.empty() && cur.path.back().axis == ax) continue;
                for (uint8_t pw = 1; pw <= 3; ++pw) {
                    State4x4 ns = cur.s;
                    Move m = makeMove(ax, pw, 0x01);
                    ns.applyMove(m);
                    uint64_t h = hashWings(ns);
                    if (!visited.insert(h).second) continue;

                    auto npath = cur.path;
                    npath.push_back(m);

                    // Quick pre-check: is partner of FR.w1 in FL or BR?
                    bool inFL = (ns.facelets[kWingPairs[9].w1_f1] == c1a && ns.facelets[kWingPairs[9].w1_f2] == c1b) ||
                                (ns.facelets[kWingPairs[9].w2_f1] == c1a && ns.facelets[kWingPairs[9].w2_f2] == c1b) ||
                                (ns.facelets[kWingPairs[9].w1_f1] == c1b && ns.facelets[kWingPairs[9].w1_f2] == c1a) ||
                                (ns.facelets[kWingPairs[9].w2_f1] == c1b && ns.facelets[kWingPairs[9].w2_f2] == c1a);
                    bool inBR = (ns.facelets[kWingPairs[11].w1_f1] == c1a && ns.facelets[kWingPairs[11].w1_f2] == c1b) ||
                                (ns.facelets[kWingPairs[11].w2_f1] == c1a && ns.facelets[kWingPairs[11].w2_f2] == c1b) ||
                                (ns.facelets[kWingPairs[11].w1_f1] == c1b && ns.facelets[kWingPairs[11].w1_f2] == c1a) ||
                                (ns.facelets[kWingPairs[11].w2_f1] == c1b && ns.facelets[kWingPairs[11].w2_f2] == c1a);

                    if ((inFL || inBR) && tryTriggers(ns, chosenTrig)) {
                        applySeq(ns, chosenTrig);
                        state = ns;
                        outMoves.insert(outMoves.end(), npath.begin(), npath.end());
                        outMoves.insert(outMoves.end(), chosenTrig.begin(), chosenTrig.end());
                        found = true;
                        break;
                    }
                    q.push({ns, std::move(npath)});
                }
                if (found) break;
            }
        }
        if (!found) return false;
    }
    return state.areWingsPaired();
}

} // namespace

// ─── Public Interface ─────────────────────────────────────────────────────────

SolveResult Solver4x4_Optimal::solve(const uint8_t colors[96]) {
    if (!State4x4::validateFacelets(colors)) {
        SolveResult err;
        err.isValid = false;
        err.errorMessage = "Invalid 4x4 cube colors.";
        err.totalMoves = 0;
        err.solveTimeMs = 0;
        return err;
    }
    State4x4 state = State4x4::fromFacelets(colors);
    return solveState(state);
}

SolveResult Solver4x4_Optimal::solveState(const State4x4& state) {
    auto t0 = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = true;
    result.totalMoves = 0;

    if (state.isSolved()) {
        result.solveTimeMs = 0.0;
        return result;
    }

    State4x4 s = state;

    // Phase 1: Center Reduction
    std::vector<Move> p1Moves;
    std::string errStage;
    if (!solveCenters4x4(s, p1Moves, errStage)) {
        result.isValid = false;
        result.errorMessage = "Failed to reduce 4x4 centers at " + errStage;
        return result;
    }
    if (!p1Moves.empty()) {
        PhaseStep ps;
        ps.phaseName = "4x4 Centers";
        ps.moves = p1Moves;
        for (size_t i = 0; i < p1Moves.size(); ++i) {
            if (i > 0) ps.notationString += " ";
            ps.notationString += moveToString(p1Moves[i], 4);
        }
        result.phases.push_back(ps);
        result.totalMoves += (int)p1Moves.size();
    }

    // Phase 2: Edge Pairing
    std::vector<Move> p2Moves;
    if (!pairAllWings(s, p2Moves)) {
        result.isValid = false;
        result.errorMessage = "Failed to pair 4x4 edges.";
        return result;
    }
    if (!p2Moves.empty()) {
        PhaseStep ps;
        ps.phaseName = "4x4 Edge Pairing";
        ps.moves = p2Moves;
        for (size_t i = 0; i < p2Moves.size(); ++i) {
            if (i > 0) ps.notationString += " ";
            ps.notationString += moveToString(p2Moves[i], 4);
        }
        result.phases.push_back(ps);
        result.totalMoves += (int)p2Moves.size();
    }

    // Phase 3: Parity Resolution
    std::vector<Move> parityMoves;

    if (s.hasOLLParity()) {
        // Pure inner-slice OLL Parity: r2 B2 U2 l U2 r' U2 r U2 F2 r F2 l' B2 r2
        const Move ollAlg[] = {
            makeMove(3, 2, 0x02), // 2R2
            makeMove(5, 2, 0x01), // B2
            makeMove(0, 2, 0x01), // U2
            makeMove(2, 1, 0x02), // 2L
            makeMove(0, 2, 0x01), // U2
            makeMove(3, 3, 0x02), // 2R'
            makeMove(0, 2, 0x01), // U2
            makeMove(3, 1, 0x02), // 2R
            makeMove(0, 2, 0x01), // U2
            makeMove(4, 2, 0x01), // F2
            makeMove(3, 1, 0x02), // 2R
            makeMove(4, 2, 0x01), // F2
            makeMove(2, 3, 0x02), // 2L'
            makeMove(5, 2, 0x01), // B2
            makeMove(3, 2, 0x02)  // 2R2
        };
        for (const auto& m : ollAlg) {
            s.applyMove(m);
            parityMoves.push_back(m);
        }
    }

    if (s.hasPLLParity()) {
        // 2R2 U2 2R2 Uw2 2R2 u2
        const Move pllAlg[] = {
            makeMove(3, 2, 0x02), // 2R2
            makeMove(0, 2, 0x01), // U2
            makeMove(3, 2, 0x02), // 2R2
            makeMove(0, 2, 0x03), // Uw2
            makeMove(3, 2, 0x02), // 2R2
            makeMove(0, 2, 0x02)  // 2U2
        };
        for (const auto& m : pllAlg) {
            s.applyMove(m);
            parityMoves.push_back(m);
        }
    }

    if (!parityMoves.empty()) {
        PhaseStep ps;
        ps.phaseName = "4x4 Parity";
        ps.moves = parityMoves;
        for (size_t i = 0; i < parityMoves.size(); ++i) {
            if (i > 0) ps.notationString += " ";
            ps.notationString += moveToString(parityMoves[i], 4);
        }
        result.phases.push_back(ps);
        result.totalMoves += (int)parityMoves.size();
    }

    // Phase 4: 3x3 Stage
    State3x3 s3 = s.toReduced3x3();
    if (!s3.isSolved()) {
        SolveResult res3 = Solver3x3_Min2Phase::solveState(s3);
        if (!res3.isValid) {
            result.isValid = false;
            result.errorMessage = "3x3 reduction failed: " + res3.errorMessage;
            return result;
        }
        for (const auto& p : res3.phases) {
            result.phases.push_back(p);
            result.totalMoves += (int)p.moves.size();
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}
