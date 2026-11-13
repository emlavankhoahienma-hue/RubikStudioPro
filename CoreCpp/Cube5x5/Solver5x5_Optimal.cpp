#include "Cube5x5/Solver5x5_Optimal.hpp"
#include <chrono>
#include <vector>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <functional>

namespace {

inline Move makeMove(uint8_t axis, uint8_t power, uint8_t sliceMask) {
    return Move{axis, sliceMask, power};
}

static void applySeq(State5x5& s, const std::vector<Move>& seq) {
    for (const auto& m : seq) s.applyMove(m);
}

// ─── Phase 1a: T-Center Reduction (Isomorphic to 4x4 Centers) ───────────────

static const int kTCenterIdx[4] = {7, 11, 13, 17};
static const int kXCenterIdx[4] = {6, 8, 16, 18};

static int countFaceTCenter(const State5x5& s, int face, uint8_t targetColor) {
    int cnt = 0;
    int base = face * 25;
    for (int i = 0; i < 4; ++i) {
        if (s.facelets[base + kTCenterIdx[i]] == targetColor) cnt++;
    }
    return cnt;
}

static bool areTCentersSolvedBOY(const State5x5& s) {
    for (int f = 0; f < 6; ++f) {
        if (countFaceTCenter(s, f, static_cast<uint8_t>(f)) != 4) return false;
    }
    return true;
}

static uint64_t hashTCenters(const State5x5& s) {
    uint64_t h = 14695981039346656037ULL;
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            h ^= s.facelets[f * 25 + kTCenterIdx[i]];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

static bool solveUTCenter(State5x5& state, std::vector<Move>& outMoves) {
    if (countFaceTCenter(state, 0, 0) == 4) return true;

    std::vector<Move> moves;
    for (uint8_t ax = 0; ax < 6; ++ax) {
        for (uint8_t pw = 1; pw <= 3; ++pw) {
            moves.push_back(makeMove(ax, pw, 0x01)); // outer
            moves.push_back(makeMove(ax, pw, 0x03)); // wide 2
        }
    }

    while (countFaceTCenter(state, 0, 0) < 4) {
        int cur = countFaceTCenter(state, 0, 0);
        struct Node {
            State5x5 s;
            std::vector<Move> path;
        };
        std::queue<Node> q;
        q.push({state, {}});
        std::unordered_set<uint64_t> visited;
        visited.insert(hashTCenters(state));

        bool found = false;
        for (int iter = 0; !q.empty() && iter < 25000 && !found; ++iter) {
            Node node = std::move(q.front());
            q.pop();
            if (node.path.size() >= 6) continue;

            for (const auto& m : moves) {
                if (!node.path.empty() && node.path.back().axis == m.axis && node.path.back().sliceMask == m.sliceMask)
                    continue;
                State5x5 ns = node.s;
                ns.applyMove(m);
                uint64_t h = hashTCenters(ns);
                if (!visited.insert(h).second) continue;

                auto npath = node.path;
                npath.push_back(m);

                if (countFaceTCenter(ns, 0, 0) > cur) {
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
    return countFaceTCenter(state, 0, 0) == 4;
}

static bool solveDTCenter(State5x5& state, std::vector<Move>& outMoves) {
    if (countFaceTCenter(state, 1, 1) == 4) return true;

    std::vector<std::vector<Move>> macros;
    for (uint8_t ax = 1; ax < 6; ++ax) {
        for (uint8_t pw = 1; pw <= 3; ++pw) {
            macros.push_back({makeMove(ax, pw, 0x01)});
        }
    }
    for (uint8_t ax : {uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(5)}) {
        for (uint8_t wp : {uint8_t(1), uint8_t(3)}) {
            uint8_t inv = (wp == 1) ? 3 : 1;
            for (uint8_t dk = 1; dk <= 3; ++dk) {
                macros.push_back({makeMove(ax, wp, 0x03), makeMove(1, dk, 0x01), makeMove(ax, inv, 0x03)});
            }
        }
    }

    struct Node {
        State5x5 s;
        std::vector<Move> path;
        int score;
    };
    std::vector<Node> beam;
    beam.push_back({state, {}, countFaceTCenter(state, 1, 1)});
    std::unordered_set<uint64_t> visited;
    visited.insert(hashTCenters(state));

    for (int depth = 0; depth < 14; ++depth) {
        std::vector<Node> nextBeam;
        for (const auto& node : beam) {
            for (const auto& mac : macros) {
                State5x5 ns = node.s;
                applySeq(ns, mac);
                if (countFaceTCenter(ns, 0, 0) != 4) continue;
                uint64_t h = hashTCenters(ns);
                if (!visited.insert(h).second) continue;

                auto npath = node.path;
                npath.insert(npath.end(), mac.begin(), mac.end());

                int dCnt = countFaceTCenter(ns, 1, 1);
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

static bool runTCenterBeam(State5x5& state, std::vector<Move>& outMoves,
                           const std::vector<std::vector<Move>>& macros,
                           int beamWidth, int maxDepth,
                           const std::function<bool(const State5x5&)>& validPred,
                           const std::function<bool(const State5x5&)>& goalPred,
                           const std::function<int(const State5x5&)>& scoreFn) {
    if (goalPred(state)) return true;
    struct Node { State5x5 s; std::vector<Move> path; int score; };
    std::vector<Node> beam;
    beam.push_back({state, {}, scoreFn(state)});
    std::unordered_set<uint64_t> visited;
    visited.insert(hashTCenters(state));

    for (int depth = 0; depth < maxDepth; ++depth) {
        std::vector<Node> nextBeam;
        for (const auto& node : beam) {
            for (const auto& mac : macros) {
                State5x5 ns = node.s;
                applySeq(ns, mac);
                if (!validPred(ns)) continue;
                uint64_t h = hashTCenters(ns);
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

static bool solveEquatorTCenters(State5x5& state, std::vector<Move>& outMoves) {
    if (areTCentersSolvedBOY(state)) return true;

    // Micro-stage 1: F T-center
    {
        std::vector<std::vector<Move>> macros;
        for (uint8_t pw = 1; pw <= 3; ++pw) {
            macros.push_back({makeMove(0, pw, 0x03)});
            macros.push_back({makeMove(1, pw, 0x03)});
        }
        for (uint8_t ax = 2; ax < 6; ++ax) {
            for (uint8_t pw = 1; pw <= 3; ++pw) macros.push_back({makeMove(ax, pw, 0x01)});
        }
        for (uint8_t ud : {uint8_t(0), uint8_t(1)}) {
            for (uint8_t wp = 1; wp <= 3; ++wp) {
                uint8_t inv = (wp == 1) ? 3 : (wp == 3 ? 1 : 2);
                for (uint8_t fk = 1; fk <= 3; ++fk) {
                    macros.push_back({makeMove(ud, wp, 0x03), makeMove(4, fk, 0x01), makeMove(ud, inv, 0x03)});
                }
            }
        }
        if (!runTCenterBeam(state, outMoves, macros, 256, 12,
                [](const State5x5& s) { return countFaceTCenter(s, 0, 0) == 4 && countFaceTCenter(s, 1, 1) == 4; },
                [](const State5x5& s) { return countFaceTCenter(s, 4, 4) == 4; },
                [](const State5x5& s) { return countFaceTCenter(s, 4, 4); })) {
            return false;
        }
    }

    // Micro-stage 2: B T-center
    {
        std::vector<std::vector<Move>> macros;
        for (uint8_t ax : {uint8_t(2), uint8_t(3), uint8_t(5)}) {
            for (uint8_t pw = 1; pw <= 3; ++pw) macros.push_back({makeMove(ax, pw, 0x01)});
        }
        for (uint8_t k = 1; k <= 3; ++k) {
            macros.push_back({makeMove(0, 3, 0x03), makeMove(2, k, 0x01), makeMove(0, 1, 0x03)});
            macros.push_back({makeMove(0, 1, 0x03), makeMove(3, k, 0x01), makeMove(0, 3, 0x03)});
            macros.push_back({makeMove(1, 1, 0x03), makeMove(2, k, 0x01), makeMove(1, 3, 0x03)});
            macros.push_back({makeMove(1, 3, 0x03), makeMove(3, k, 0x01), makeMove(1, 1, 0x03)});
        }
        if (!runTCenterBeam(state, outMoves, macros, 128, 12,
                [](const State5x5& s) {
                    return countFaceTCenter(s, 0, 0) == 4 && countFaceTCenter(s, 1, 1) == 4 && countFaceTCenter(s, 4, 4) == 4;
                },
                [](const State5x5& s) { return countFaceTCenter(s, 5, 5) == 4; },
                [](const State5x5& s) { return countFaceTCenter(s, 5, 5) * 10 + countFaceTCenter(s, 2, 2) + countFaceTCenter(s, 3, 3); })) {
            return false;
        }
    }

    // Micro-stage 3: L & R T-centers
    {
        std::vector<std::vector<Move>> macros;
        for (uint8_t ax : {uint8_t(2), uint8_t(3)}) {
            for (uint8_t pw = 1; pw <= 3; ++pw) macros.push_back({makeMove(ax, pw, 0x01)});
        }
        for (uint8_t ud : {uint8_t(0), uint8_t(1)}) {
            for (uint8_t ax : {uint8_t(2), uint8_t(3)}) {
                for (uint8_t k = 1; k <= 3; ++k) {
                    macros.push_back({makeMove(ud, 2, 0x03), makeMove(ax, k, 0x01), makeMove(ud, 2, 0x03)});
                }
            }
        }
        if (!runTCenterBeam(state, outMoves, macros, 128, 10,
                [](const State5x5& s) {
                    return countFaceTCenter(s, 0, 0) == 4 && countFaceTCenter(s, 1, 1) == 4 &&
                           countFaceTCenter(s, 4, 4) == 4 && countFaceTCenter(s, 5, 5) == 4;
                },
                [](const State5x5& s) { return areTCentersSolvedBOY(s); },
                [](const State5x5& s) { return countFaceTCenter(s, 2, 2) + countFaceTCenter(s, 3, 3); })) {
            return false;
        }
    }
    return areTCentersSolvedBOY(state);
}

// ─── Phase 1b: X-Center Reduction (Pure 3-Cycles Preserving All T-Centers) ───

static int countSolvedXCenters(const State5x5& s) {
    int cnt = 0;
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            if (s.facelets[f * 25 + kXCenterIdx[i]] == f) cnt++;
        }
    }
    return cnt;
}

static uint64_t hashXCenters(const State5x5& s) {
    uint64_t h = 14695981039346656037ULL;
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            h ^= s.facelets[f * 25 + kXCenterIdx[i]];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

// Build pure X-center 3-cycle commutators: [2A^p, B^q] using opposite inner slice for return
// E.g. 2R U 2L' U' 2R' U 2L U' (and all axis pairs where A != B and A != opp(B))
static std::vector<std::vector<Move>> buildXCenterCommutators() {
    std::vector<std::vector<Move>> comms;
    // Opposite axis mapping: 0<->1, 2<->3, 4<->5
    auto opp = [](uint8_t ax) -> uint8_t { return ax ^ 1; };

    for (uint8_t a = 0; a < 6; ++a) {
        for (uint8_t b = 0; b < 6; ++b) {
            if (a == b || a == opp(b)) continue;
            for (uint8_t pa : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
                uint8_t paInv = (pa == 1) ? 3 : (pa == 3 ? 1 : 2);
                for (uint8_t pb : {uint8_t(1), uint8_t(3)}) {
                    uint8_t pbInv = (pb == 1) ? 3 : 1;
                    // 2A^pa  B^pb  2OppA^pa  B^pbInv  2A^paInv  B^pb  2OppA^paInv  B^pbInv
                    // Wait: on opposite face, clockwise is reversed, so 2OppA^paInv goes the same direction!
                    comms.push_back({
                        makeMove(a, pa, 0x02),
                        makeMove(b, pb, 0x01),
                        makeMove(opp(a), paInv, 0x02),
                        makeMove(b, pbInv, 0x01),
                        makeMove(a, paInv, 0x02),
                        makeMove(b, pb, 0x01),
                        makeMove(opp(a), pa, 0x02),
                        makeMove(b, pbInv, 0x01)
                    });
                }
            }
        }
    }
    return comms;
}

static bool solveXCenters(State5x5& state, std::vector<Move>& outMoves) {
    if (countSolvedXCenters(state) == 24) return true;

    static const std::vector<std::vector<Move>> comms = buildXCenterCommutators();

    while (countSolvedXCenters(state) < 24) {
        int cur = countSolvedXCenters(state);
        bool found = false;

        // Generate outer-only setup states up to depth 2 (1 + 18 + 243 = ~262 states)
        struct Setup { State5x5 s; std::vector<Move> path; };
        std::vector<Setup> setups;
        setups.push_back({state, {}});
        std::unordered_set<uint64_t> vis;
        vis.insert(hashXCenters(state));

        for (int d = 0; d < 2; ++d) {
            size_t sz = setups.size();
            for (size_t i = 0; i < sz; ++i) {
                if ((int)setups[i].path.size() != d) continue;
                for (uint8_t ax = 0; ax < 6; ++ax) {
                    if (!setups[i].path.empty() && setups[i].path.back().axis == ax) continue;
                    for (uint8_t pw = 1; pw <= 3; ++pw) {
                        State5x5 ns = setups[i].s;
                        Move m = makeMove(ax, pw, 0x01);
                        ns.applyMove(m);
                        if (vis.insert(hashXCenters(ns)).second) {
                            auto np = setups[i].path;
                            np.push_back(m);
                            setups.push_back({ns, std::move(np)});
                        }
                    }
                }
            }
        }

        // Test each commutator on each setup state
        for (const auto& sup : setups) {
            for (const auto& comm : comms) {
                State5x5 ns = sup.s;
                applySeq(ns, comm);
                if (countSolvedXCenters(ns) > cur) {
                    state = ns;
                    outMoves.insert(outMoves.end(), sup.path.begin(), sup.path.end());
                    outMoves.insert(outMoves.end(), comm.begin(), comm.end());
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        // Fallback: if 1 commutator couldn't strictly increase (e.g., 2-cycle on same orbit),
        // apply a commutator that moves an unsolved sticker without decreasing solved count
        if (!found) {
            for (const auto& comm : comms) {
                State5x5 ns = state;
                applySeq(ns, comm);
                if (countSolvedXCenters(ns) >= cur && hashXCenters(ns) != hashXCenters(state)) {
                    state = ns;
                    outMoves.insert(outMoves.end(), comm.begin(), comm.end());
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;
    }
    return countSolvedXCenters(state) == 24;
}

static bool solveCenters5x5(State5x5& state, std::vector<Move>& outMoves) {
    if (!areTCentersSolvedBOY(state)) {
        if (!solveUTCenter(state, outMoves)) return false;
        if (!solveDTCenter(state, outMoves)) return false;
        if (!solveEquatorTCenters(state, outMoves)) return false;
    }
    if (!solveXCenters(state, outMoves)) return false;
    return areTCentersSolvedBOY(state) && countSolvedXCenters(state) == 24;
}

// ─── Phase 2: Deterministic Tredge Pairing ───────────────────────────────────

struct TredgeDef {
    int m1, m2;
    int w1_1, w1_2;
    int w2_1, w2_2;
};

static const TredgeDef kTredges[12] = {
    {0*25 + 14, 3*25 + 2,  0*25 + 19, 3*25 + 1,  0*25 + 9,  3*25 + 3},  // 0: UR
    {0*25 + 22, 4*25 + 2,  0*25 + 21, 4*25 + 1,  0*25 + 23, 4*25 + 3},  // 1: UF
    {0*25 + 10, 2*25 + 2,  0*25 + 5,  2*25 + 1,  0*25 + 15, 2*25 + 3},  // 2: UL
    {0*25 + 2,  5*25 + 2,  0*25 + 1,  5*25 + 3,  0*25 + 3,  5*25 + 1},  // 3: UB
    {1*25 + 14, 3*25 + 22, 1*25 + 9,  3*25 + 21, 1*25 + 19, 3*25 + 23}, // 4: DR
    {1*25 + 2,  4*25 + 22, 1*25 + 1,  4*25 + 21, 1*25 + 3,  4*25 + 23}, // 5: DF
    {1*25 + 10, 2*25 + 22, 1*25 + 5,  2*25 + 23, 1*25 + 15, 2*25 + 21}, // 6: DL
    {1*25 + 22, 5*25 + 22, 1*25 + 21, 5*25 + 23, 1*25 + 23, 5*25 + 21}, // 7: DB
    {4*25 + 14, 3*25 + 10, 4*25 + 9,  3*25 + 5,  4*25 + 19, 3*25 + 15}, // 8: FR
    {4*25 + 10, 2*25 + 14, 4*25 + 5,  2*25 + 9,  4*25 + 15, 2*25 + 19}, // 9: FL
    {5*25 + 14, 2*25 + 10, 5*25 + 9,  2*25 + 5,  5*25 + 19, 2*25 + 15}, // 10: BL
    {5*25 + 10, 3*25 + 14, 5*25 + 5,  3*25 + 9,  5*25 + 15, 3*25 + 19}  // 11: BR
};

static int scoreTredges(const State5x5& s) {
    int pairedTredges = 0;
    int pairedWings = 0;
    int readyBonus = 0;
    for (int e = 0; e < 12; ++e) {
        uint8_t m1 = s.facelets[kTredges[e].m1];
        uint8_t m2 = s.facelets[kTredges[e].m2];
        bool p1 = (s.facelets[kTredges[e].w1_1] == m1 && s.facelets[kTredges[e].w1_2] == m2);
        bool p2 = (s.facelets[kTredges[e].w2_1] == m1 && s.facelets[kTredges[e].w2_2] == m2);
        if (p1) pairedWings++;
        if (p2) pairedWings++;
        if (p1 && p2) {
            pairedTredges++;
        } else {
            bool r1 = (s.facelets[kTredges[e].w1_1] == m2 && s.facelets[kTredges[e].w1_2] == m1);
            bool r2 = (s.facelets[kTredges[e].w2_1] == m2 && s.facelets[kTredges[e].w2_2] == m1);
            if ((p1 && r2) || (p2 && r1)) readyBonus++;
        }
    }
    return pairedTredges * 100 + pairedWings * 10 + readyBonus;
}

static std::vector<Move> makeSliceFlipSlice5(uint8_t udAxis, uint8_t udPower) {
    uint8_t udInv = (udPower == 1) ? 3 : (udPower == 3 ? 1 : 2);
    return {
        makeMove(udAxis, udPower, 0x03), // Uw/Uw'/Dw/Dw'
        makeMove(3, 1, 0x01),            // R
        makeMove(0, 1, 0x01),            // U
        makeMove(3, 3, 0x01),            // R'
        makeMove(4, 1, 0x01),            // F
        makeMove(3, 3, 0x01),            // R'
        makeMove(4, 3, 0x01),            // F'
        makeMove(3, 1, 0x01),            // R
        makeMove(udAxis, udInv, 0x03)
    };
}

// Pure inner-slice OLL/Edge-flip parity on UF: r2 B2 U2 l U2 r' U2 r U2 F2 r F2 l' B2 r2
static const std::vector<Move>& getSingleTredgeFlipAlg() {
    static const std::vector<Move> alg = {
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
    return alg;
}

static uint64_t hashEdges5x5(const State5x5& s) {
    uint64_t h = 14695981039346656037ULL;
    for (int e = 0; e < 12; ++e) {
        h ^= s.facelets[kTredges[e].m1]; h *= 1099511628211ULL;
        h ^= s.facelets[kTredges[e].m2]; h *= 1099511628211ULL;
        h ^= s.facelets[kTredges[e].w1_1]; h *= 1099511628211ULL;
        h ^= s.facelets[kTredges[e].w1_2]; h *= 1099511628211ULL;
        h ^= s.facelets[kTredges[e].w2_1]; h *= 1099511628211ULL;
        h ^= s.facelets[kTredges[e].w2_2]; h *= 1099511628211ULL;
    }
    return h;
}

static bool pairAllTredges(State5x5& state, std::vector<Move>& outMoves) {
    const std::vector<Move> trigs[4] = {
        makeSliceFlipSlice5(0, 3), // Uw' ... Uw (FR.w2 <-> FL.w1)
        makeSliceFlipSlice5(0, 1), // Uw  ... Uw' (FR.w2 <-> BR.w1)
        makeSliceFlipSlice5(1, 1), // Dw  ... Dw' (FR.w1 <-> FL.w2)
        makeSliceFlipSlice5(1, 3)  // Dw' ... Dw  (FR.w1 <-> BR.w2)
    };

    for (int step = 0; step < 36 && !state.areTredgesPaired(); ++step) {
        // Check for single-tredge wing-flip parity (any tredge with w1 == w2 == flipped(midge))
        bool fixedFlip = false;
        for (int e = 0; e < 12; ++e) {
            uint8_t m1 = state.facelets[kTredges[e].m1];
            uint8_t m2 = state.facelets[kTredges[e].m2];
            if (state.facelets[kTredges[e].w1_1] == m2 && state.facelets[kTredges[e].w1_2] == m1 &&
                state.facelets[kTredges[e].w2_1] == m2 && state.facelets[kTredges[e].w2_2] == m1) {
                struct Node { State5x5 s; std::vector<Move> path; };
                std::queue<Node> q;
                q.push({state, {}});
                std::unordered_set<uint64_t> vis;
                vis.insert(hashEdges5x5(state));
                for (int it = 0; !q.empty() && it < 2000; ++it) {
                    Node cur = std::move(q.front());
                    q.pop();
                    uint8_t um1 = cur.s.facelets[kTredges[1].m1];
                    uint8_t um2 = cur.s.facelets[kTredges[1].m2];
                    if (cur.s.facelets[kTredges[1].w1_1] == um2 && cur.s.facelets[kTredges[1].w1_2] == um1 &&
                        cur.s.facelets[kTredges[1].w2_1] == um2 && cur.s.facelets[kTredges[1].w2_2] == um1) {
                        applySeq(cur.s, getSingleTredgeFlipAlg());
                        state = cur.s;
                        outMoves.insert(outMoves.end(), cur.path.begin(), cur.path.end());
                        outMoves.insert(outMoves.end(), getSingleTredgeFlipAlg().begin(), getSingleTredgeFlipAlg().end());
                        fixedFlip = true;
                        break;
                    }
                    if (cur.path.size() >= 4) continue;
                    for (uint8_t ax = 0; ax < 6; ++ax) {
                        if (!cur.path.empty() && cur.path.back().axis == ax) continue;
                        for (uint8_t pw = 1; pw <= 3; ++pw) {
                            State5x5 ns = cur.s;
                            Move mv = makeMove(ax, pw, 0x01);
                            ns.applyMove(mv);
                            if (vis.insert(hashEdges5x5(ns)).second) {
                                auto np = cur.path;
                                np.push_back(mv);
                                q.push({ns, std::move(np)});
                            }
                        }
                    }
                }
                if (fixedFlip) break;
            }
        }
        if (fixedFlip) continue;

        int curScore = scoreTredges(state);
        auto tryTriggers = [&](const State5x5& s, std::vector<Move>& bestTrig) -> bool {
            int bestScore = curScore;
            for (int t = 0; t < 4; ++t) {
                State5x5 test = s;
                applySeq(test, trigs[t]);
                int sc = scoreTredges(test);
                if (sc > bestScore) {
                    bestScore = sc;
                    bestTrig = trigs[t];
                }
            }
            return bestScore > curScore;
        };

        std::vector<Move> chosenTrig;
        if (tryTriggers(state, chosenTrig)) {
            applySeq(state, chosenTrig);
            outMoves.insert(outMoves.end(), chosenTrig.begin(), chosenTrig.end());
            continue;
        }

        // BFS over ALL outer moves {U, D, L, R, F, B} (never touches centers or splits paired tredges)
        struct Node {
            State5x5 s;
            std::vector<Move> path;
        };
        std::queue<Node> q;
        q.push({state, {}});
        std::unordered_set<uint64_t> visited;
        visited.insert(hashEdges5x5(state));

        bool found = false;
        for (int iter = 0; !q.empty() && iter < 25000 && !found; ++iter) {
            Node cur = std::move(q.front());
            q.pop();
            if (cur.path.size() >= 5) continue;

            for (uint8_t ax = 0; ax < 6; ++ax) {
                if (!cur.path.empty() && cur.path.back().axis == ax) continue;
                for (uint8_t pw = 1; pw <= 3; ++pw) {
                    State5x5 ns = cur.s;
                    Move m = makeMove(ax, pw, 0x01);
                    ns.applyMove(m);
                    uint64_t h = hashEdges5x5(ns);
                    if (!visited.insert(h).second) continue;

                    auto npath = cur.path;
                    npath.push_back(m);

                    if (tryTriggers(ns, chosenTrig)) {
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
    return state.areTredgesPaired();
}

} // namespace

// ─── Public Interface ─────────────────────────────────────────────────────────

SolveResult Solver5x5_Optimal::solve(const uint8_t colors[150]) {
    if (!State5x5::validateFacelets(colors)) {
        SolveResult err;
        err.isValid = false;
        err.errorMessage = "Invalid 5x5 cube colors.";
        err.totalMoves = 0;
        err.solveTimeMs = 0;
        return err;
    }
    State5x5 state = State5x5::fromFacelets(colors);
    return solveState(state);
}

static bool tryShallowSolve5x5(const State5x5& state, int maxDepth, std::vector<Move>& outMoves) {
    static const uint8_t masks[2] = {0x01, 0x03};
    std::function<bool(const State5x5&, int, int, int)> dfs = [&](const State5x5& s, int d, int lastAx, int lastMask) -> bool {
        if (s.isSolved()) return true;
        if (d == 0) return false;
        for (uint8_t ax = 0; ax < 6; ++ax) {
            for (uint8_t sl : masks) {
                if (ax == lastAx && sl == lastMask) continue;
                for (uint8_t pw = 1; pw <= 3; ++pw) {
                    State5x5 ns = s;
                    Move m = makeMove(ax, pw, sl);
                    ns.applyMove(m);
                    outMoves.push_back(m);
                    if (dfs(ns, d - 1, ax, sl)) return true;
                    outMoves.pop_back();
                }
            }
        }
        return false;
    };
    for (int d = 1; d <= maxDepth; ++d) {
        outMoves.clear();
        if (dfs(state, d, -1, -1)) return true;
    }
    return false;
}

SolveResult Solver5x5_Optimal::solveState(const State5x5& state) {
    auto t0 = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = true;
    result.totalMoves = 0;

    if (state.isSolved()) {
        result.solveTimeMs = 0.0;
        return result;
    }

    // Fast shallow optimal check (depth <= 3)
    std::vector<Move> shallowMoves;
    if (tryShallowSolve5x5(state, 3, shallowMoves)) {
        PhaseStep ps;
        ps.phaseName = "5x5 Direct Optimal";
        ps.moves = shallowMoves;
        for (size_t i = 0; i < shallowMoves.size(); ++i) {
            if (i > 0) ps.notationString += " ";
            ps.notationString += moveToString(shallowMoves[i], 5);
        }
        result.phases.push_back(ps);
        result.totalMoves = (int)shallowMoves.size();
        auto t1 = std::chrono::high_resolution_clock::now();
        result.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
    }

    State5x5 s = state;

    // Phase 1: Center Reduction
    std::vector<Move> p1Moves;
    if (!solveCenters5x5(s, p1Moves)) {
        result.isValid = false;
        result.errorMessage = "Failed to reduce 5x5 centers.";
        return result;
    }
    if (!p1Moves.empty()) {
        PhaseStep ps;
        ps.phaseName = "5x5 Centers";
        ps.moves = p1Moves;
        for (size_t i = 0; i < p1Moves.size(); ++i) {
            if (i > 0) ps.notationString += " ";
            ps.notationString += moveToString(p1Moves[i], 5);
        }
        result.phases.push_back(ps);
        result.totalMoves += (int)p1Moves.size();
    }

    // Phase 2: Tredge Pairing
    std::vector<Move> p2Moves;
    if (!pairAllTredges(s, p2Moves)) {
        result.isValid = false;
        result.errorMessage = "Failed to pair 5x5 tredges.";
        return result;
    }
    if (!p2Moves.empty()) {
        PhaseStep ps;
        ps.phaseName = "5x5 Tredge Pairing";
        ps.moves = p2Moves;
        for (size_t i = 0; i < p2Moves.size(); ++i) {
            if (i > 0) ps.notationString += " ";
            ps.notationString += moveToString(p2Moves[i], 5);
        }
        result.phases.push_back(ps);
        result.totalMoves += (int)p2Moves.size();
    }

    // Phase 3: 3x3 Stage
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
