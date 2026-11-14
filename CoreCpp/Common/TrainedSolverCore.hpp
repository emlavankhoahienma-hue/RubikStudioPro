#pragma once
#include <array>
#include <vector>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstring>
#include <chrono>
#include <mutex>

#include "Common/FastCubeTables.hpp"
#include "Common/TrainedTables.hpp"
#include "Common/MoveOptimizer.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"

namespace TrainedSolver {

using namespace FastCube;
using namespace TrainedData;

// ─── Stage Move Subsets for Centers ───────────────────────────────────────────
// In MoveCatalog::moves36:
// index m = axis*6 + (wide?3:0) + (power-1)
// axis 0:U, 1:D, 2:L, 3:R, 4:F, 5:B
// wide=0 => 0..2 (outer), wide=1 => 3..5 (wide, pow 1,2,3)

struct StageMoveMaps {
    int s2Moves[28]; // indices into moves36: 18 outer + Uw(3..5), Dw(9..11), Lw2(16), Rw2(22), Fw2(28), Bw2(34)
    int s3Moves[24]; // indices into moves36: 18 outer + Uw2(4), Dw2(10), Lw2(16), Rw2(22), Fw2(28), Bw2(34)

    // Fast 1D coordinate transition tables built per center perm[36][24]
    uint8_t udMove70[70][28];
    uint16_t lrMove12870[12870][28];
    uint8_t s70Move24[70][24];

    void init(const uint8_t perm[36][24]) {
        int k2 = 0;
        for (int ax = 0; ax < 6; ++ax) {
            for (int p = 0; p < 3; ++p) s2Moves[k2++] = ax * 6 + p; // outer
        }
        s2Moves[k2++] = 0 * 6 + 3; s2Moves[k2++] = 0 * 6 + 4; s2Moves[k2++] = 0 * 6 + 5; // Uw, Uw2, Uw'
        s2Moves[k2++] = 1 * 6 + 3; s2Moves[k2++] = 1 * 6 + 4; s2Moves[k2++] = 1 * 6 + 5; // Dw, Dw2, Dw'
        s2Moves[k2++] = 2 * 6 + 4; // Lw2
        s2Moves[k2++] = 3 * 6 + 4; // Rw2
        s2Moves[k2++] = 4 * 6 + 4; // Fw2
        s2Moves[k2++] = 5 * 6 + 4; // Bw2

        int k3 = 0;
        for (int ax = 0; ax < 6; ++ax) {
            for (int p = 0; p < 3; ++p) s3Moves[k3++] = ax * 6 + p;
            s3Moves[k3++] = ax * 6 + 4; // wide2 for all 6 axes
        }

        // Build udMove70 (70 x 28) on slots [0..7]
        for (uint32_t r = 0; r < 70; ++r) {
            uint8_t mask[8];
            unrankComb<8, 4>(r, mask);
            for (int mi = 0; mi < 28; ++mi) {
                int m = s2Moves[mi];
                uint8_t nmask[8];
                for (int i = 0; i < 8; ++i) nmask[i] = mask[perm[m][i]];
                udMove70[r][mi] = static_cast<uint8_t>(rankComb<8, 4>(nmask));
            }
        }

        // Build lrMove12870 (12870 x 28) on slots [8..23]
        for (uint32_t r = 0; r < 12870; ++r) {
            uint8_t mask[16];
            unrankComb<16, 8>(r, mask);
            for (int mi = 0; mi < 28; ++mi) {
                int m = s2Moves[mi];
                uint8_t nmask[16];
                for (int i = 0; i < 16; ++i) nmask[i] = mask[perm[m][8 + i] - 8];
                lrMove12870[r][mi] = static_cast<uint16_t>(rankComb<16, 8>(nmask));
            }
        }

        // Build s70Move24 (70 x 24) mapping any pair of opposite faces (8 slots) cleanly.
        // Wait: U/D uses slots [0..7], L/R uses [8..15], F/B uses [16..23] -> build 3 small tables?
        // Actually Stage 3 computes each block directly via s70 tables? No! U<->D uses [0..7], L<->R uses [8..15], F<->B uses [16..23]!
        // Let's keep 3 exact 70x24 tables (only 5 KB total!) for maximum speed & zero symmetry bugs!
        for (uint32_t r = 0; r < 70; ++r) {
            uint8_t mask[8];
            unrankComb<8, 4>(r, mask);
            for (int mi = 0; mi < 24; ++mi) {
                int m = s3Moves[mi];
                uint8_t nmU[8], nmL[8], nmF[8];
                for (int i = 0; i < 8; ++i) {
                    nmU[i] = mask[perm[m][i]];
                    nmL[i] = mask[perm[m][8 + i] - 8];
                    nmF[i] = mask[perm[m][16 + i] - 16];
                }
                s70MoveU[r][mi] = static_cast<uint8_t>(rankComb<8, 4>(nmU));
                s70MoveL[r][mi] = static_cast<uint8_t>(rankComb<8, 4>(nmL));
                s70MoveF[r][mi] = static_cast<uint8_t>(rankComb<8, 4>(nmF));
            }
        }
    }

    uint8_t s70MoveU[70][24];
    uint8_t s70MoveL[70][24];
    uint8_t s70MoveF[70][24];
};

// ─── Runtime Singleton Holding All Unpacked Tables & Stage Maps ───────────────

struct ArenaStep {
    int parent;
    int16_t m;
};

struct FlatBwdMap {
    std::vector<uint64_t> slots; // 0 = empty; else ((key + 1ULL) << 4) | dist
    uint32_t mask = 0;

    void init(size_t pow2Size) {
        slots.assign(pow2Size, 0ULL);
        mask = static_cast<uint32_t>(pow2Size - 1);
    }
    static inline uint64_t mix64(uint64_t x) {
        x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27; x *= 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
    bool insert(uint64_t key, uint8_t dist) {
        uint64_t tagged = ((key + 1ULL) << 4) | (dist & 0xFU);
        uint32_t idx = static_cast<uint32_t>(mix64(key)) & mask;
        while (slots[idx] != 0ULL) {
            if ((slots[idx] >> 4) == (key + 1ULL)) return false;
            idx = (idx + 1U) & mask;
        }
        slots[idx] = tagged;
        return true;
    }
    int find(uint64_t key) const {
        if (slots.empty()) return -1;
        uint32_t idx = static_cast<uint32_t>(mix64(key)) & mask;
        while (slots[idx] != 0ULL) {
            if ((slots[idx] >> 4) == (key + 1ULL)) return static_cast<int>(slots[idx] & 0xFU);
            idx = (idx + 1U) & mask;
        }
        return -1;
    }
};

struct EngineContext {
    TableBank bank;
    StageMoveMaps map4;
    StageMoveMaps map5T;
    StageMoveMaps map5X;
    FlatBwdMap bwd5_s2;
    FlatBwdMap bwd5_s3;
    bool initialized = false;
    std::mutex initMtx;

    static void buildCenterTablesBFS(const uint8_t perm[36][24],
                                     const StageMoveMaps& maps,
                                     std::vector<uint8_t>& outUD,
                                     std::vector<uint8_t>& outS2,
                                     std::vector<uint8_t>& outS3,
                                     const std::function<void(const char*, int, uint32_t, uint32_t)>& progressCb = nullptr) {
        // 1. Stage 1 UD BFS (735,471 states)
        outUD.assign((TableBank::kSizeUD + 1) / 2, 0xFF);
        {
            setNibble(outUD.data(), 0, 0);
            std::vector<uint32_t> frontier = {0};
            uint32_t visitedCnt = 1;
            for (int depth = 0; !frontier.empty() && depth < 14; ++depth) {
                if (progressCb) progressCb("UD", depth, visitedCnt, TableBank::kSizeUD);
                std::vector<uint32_t> nextFrontier;
                nextFrontier.reserve(frontier.size() * 4);
                uint8_t curMask[24], nxtMask[24];
                for (uint32_t u : frontier) {
                    unrankComb<24, 8>(u, curMask);
                    for (int m = 0; m < 36; ++m) {
                        for (int i = 0; i < 24; ++i) nxtMask[i] = curMask[perm[m][i]];
                        uint32_t v = rankComb<24, 8>(nxtMask);
                        if (getNibble(outUD.data(), v) == 15) {
                            setNibble(outUD.data(), v, static_cast<uint8_t>(depth + 1));
                            nextFrontier.push_back(v);
                            visitedCnt++;
                        }
                    }
                }
                frontier = std::move(nextFrontier);
            }
            if (progressCb) progressCb("UD", 99, visitedCnt, TableBank::kSizeUD);
        }

        // 2. Stage 2 Split BFS (70 * 12870 = 900,900 states)
        outS2.assign((TableBank::kSizeS2 + 1) / 2, 0xFF);
        {
            setNibble(outS2.data(), 0, 0);
            std::vector<uint32_t> frontier = {0};
            uint32_t visitedCnt = 1;
            for (int depth = 0; !frontier.empty() && depth < 14; ++depth) {
                if (progressCb) progressCb("S2", depth, visitedCnt, TableBank::kSizeS2);
                std::vector<uint32_t> nextFrontier;
                nextFrontier.reserve(frontier.size() * 6);
                for (uint32_t idx : frontier) {
                    uint32_t u = idx / 12870;
                    uint32_t l = idx % 12870;
                    for (int mi = 0; mi < 28; ++mi) {
                        uint32_t nu = maps.udMove70[u][mi];
                        uint32_t nl = maps.lrMove12870[l][mi];
                        uint32_t v = nu * 12870 + nl;
                        if (getNibble(outS2.data(), v) == 15) {
                            setNibble(outS2.data(), v, static_cast<uint8_t>(depth + 1));
                            nextFrontier.push_back(v);
                            visitedCnt++;
                        }
                    }
                }
                frontier = std::move(nextFrontier);
            }
            if (progressCb) progressCb("S2", 99, visitedCnt, TableBank::kSizeS2);
        }

        // 3. Stage 3 Final Centers BFS (70 * 70 * 70 = 343,000 states)
        outS3.assign((TableBank::kSizeS3 + 1) / 2, 0xFF);
        {
            setNibble(outS3.data(), 0, 0);
            std::vector<uint32_t> frontier = {0};
            uint32_t visitedCnt = 1;
            for (int depth = 0; !frontier.empty() && depth < 14; ++depth) {
                if (progressCb) progressCb("S3", depth, visitedCnt, TableBank::kSizeS3);
                std::vector<uint32_t> nextFrontier;
                nextFrontier.reserve(frontier.size() * 6);
                for (uint32_t idx : frontier) {
                    uint32_t f = idx % 70;
                    uint32_t rem = idx / 70;
                    uint32_t l = rem % 70;
                    uint32_t u = rem / 70;
                    for (int mi = 0; mi < 24; ++mi) {
                        uint32_t nu = maps.s70MoveU[u][mi];
                        uint32_t nl = maps.s70MoveL[l][mi];
                        uint32_t nf = maps.s70MoveF[f][mi];
                        uint32_t v = (nu * 70 + nl) * 70 + nf;
                        if (getNibble(outS3.data(), v) == 15) {
                            setNibble(outS3.data(), v, static_cast<uint8_t>(depth + 1));
                            nextFrontier.push_back(v);
                            visitedCnt++;
                        }
                    }
                }
                frontier = std::move(nextFrontier);
            }
            if (progressCb) progressCb("S3", 99, visitedCnt, TableBank::kSizeS3);
        }
    }

    void ensureInitialized() {
        std::lock_guard<std::mutex> lk(initMtx);
        if (initialized) return;

        const auto& cat = catalog();
        map4.init(cat.c4Perm);
        map5T.init(cat.t5Perm);
        map5X.init(cat.x5Perm);

        loadCompressedInto(bank);
        if (!bank.ready) {
            buildCenterTablesBFS(cat.c4Perm, map4, bank.c4_ud, bank.c4_s2, bank.c4_s3);
            buildCenterTablesBFS(cat.t5Perm, map5T, bank.t5_ud, bank.t5_s2, bank.t5_s3);
            buildCenterTablesBFS(cat.x5Perm, map5X, bank.x5_ud, bank.x5_s2, bank.x5_s3);
            bank.ready = true;
        }

        // Build compact backward joint tables for 5x5 Centers Stage 2 (d<=4) and Stage 3 (d<=5)
        bwd5_s2.init(32768);
        {
            struct BN2 { uint16_t ut, lt, ux, lx; int8_t lm; };
            std::vector<BN2> cur = {{0, 0, 0, 0, -1}};
            bwd5_s2.insert(0ULL, 0);
            for (uint8_t d = 1; d <= 4; ++d) {
                std::vector<BN2> nxt;
                for (const auto& c : cur) {
                    for (int mi = 0; mi < 28; ++mi) {
                        int m = map5T.s2Moves[mi];
                        if (c.lm >= 0 && cat.moves36[m].axis == cat.moves36[c.lm].axis &&
                            cat.moves36[m].sliceMask == cat.moves36[c.lm].sliceMask) continue;
                        uint16_t nut = map5T.udMove70[c.ut][mi], nlt = map5T.lrMove12870[c.lt][mi];
                        uint16_t nux = map5X.udMove70[c.ux][mi], nlx = map5X.lrMove12870[c.lx][mi];
                        uint64_t k = (((uint64_t)nut * 12870ULL + nlt) << 22) | ((uint64_t)nux * 12870ULL + nlx);
                        if (bwd5_s2.insert(k, d) && d < 4) nxt.push_back({nut, nlt, nux, nlx, static_cast<int8_t>(m)});
                    }
                }
                cur = std::move(nxt);
            }
        }
        bwd5_s3.init(524288);
        {
            struct BN3 { uint8_t ut, lt, ft, ux, lx, fx; int8_t lm; };
            std::vector<BN3> cur = {{0, 0, 0, 0, 0, 0, -1}};
            bwd5_s3.insert(0ULL, 0);
            for (uint8_t d = 1; d <= 5; ++d) {
                std::vector<BN3> nxt;
                nxt.reserve(cur.size() * 8);
                for (const auto& c : cur) {
                    for (int mi = 0; mi < 24; ++mi) {
                        int m = map5T.s3Moves[mi];
                        if (c.lm >= 0 && cat.moves36[m].axis == cat.moves36[c.lm].axis &&
                            cat.moves36[m].sliceMask == cat.moves36[c.lm].sliceMask) continue;
                        uint8_t nut = map5T.s70MoveU[c.ut][mi], nlt = map5T.s70MoveL[c.lt][mi], nft = map5T.s70MoveF[c.ft][mi];
                        uint8_t nux = map5X.s70MoveU[c.ux][mi], nlx = map5X.s70MoveL[c.lx][mi], nfx = map5X.s70MoveF[c.fx][mi];
                        uint32_t it = ((uint32_t)nut * 70U + nlt) * 70U + nft;
                        uint32_t ix = ((uint32_t)nux * 70U + nlx) * 70U + nfx;
                        uint64_t k = ((uint64_t)it << 20) | ix;
                        if (bwd5_s3.insert(k, d) && d < 5) nxt.push_back({nut, nlt, nft, nux, nlx, nfx, static_cast<int8_t>(m)});
                    }
                }
                cur = std::move(nxt);
            }
        }
        initialized = true;
    }
};

inline EngineContext& engineCtx() {
    static EngineContext inst;
    return inst;
}

// ─── Exact Center Coordinates Extraction ──────────────────────────────────────

inline uint32_t getCoordUD(const std::array<uint8_t, 24>& c) {
    uint8_t mask[24];
    for (int i = 0; i < 24; ++i) mask[i] = (c[i] == 0 || c[i] == 1) ? 1 : 0;
    return rankComb<24, 8>(mask);
}

inline uint32_t getCoordS2(const std::array<uint8_t, 24>& c) {
    uint8_t uMask[8], lrMask[16];
    for (int i = 0; i < 8; ++i) uMask[i] = (c[i] == 0) ? 1 : 0;
    for (int i = 0; i < 16; ++i) lrMask[i] = (c[8 + i] == 2 || c[8 + i] == 3) ? 1 : 0;
    return rankComb<8, 4>(uMask) * 12870U + rankComb<16, 8>(lrMask);
}

inline uint32_t getCoordS3(const std::array<uint8_t, 24>& c) {
    uint8_t uMask[8], lMask[8], fMask[8];
    for (int i = 0; i < 8; ++i) {
        uMask[i] = (c[i] == 0) ? 1 : 0;
        lMask[i] = (c[8 + i] == 2) ? 1 : 0;
        fMask[i] = (c[16 + i] == 4) ? 1 : 0;
    }
    return (rankComb<8, 4>(uMask) * 70U + rankComb<8, 4>(lMask)) * 70U + rankComb<8, 4>(fMask);
}

// ─── Whole-Cube 3-Axis Rotation Symmetry (U/D, F/B, L/R) ─────────────────────

struct CubeSymmetry {
    std::array<uint8_t, 96> rotX4{}, rotZ4{};
    std::array<uint8_t, 150> rotX5{}, rotZ5{};
    std::array<uint8_t, 24> rotXc4{}, rotZc4{};
    std::array<uint8_t, 24> rotXt5{}, rotZt5{}, rotXx5{}, rotZx5{};
    static constexpr uint8_t axRot[3][6] = {
        {0, 1, 2, 3, 4, 5}, // sym 0: Identity (U/D)
        {5, 4, 2, 3, 0, 1}, // sym 1: rotX (F/B -> U/D)
        {3, 2, 0, 1, 4, 5}  // sym 2: rotZ (L/R -> U/D)
    };
    static constexpr uint8_t invAxRot[3][6] = {
        {0, 1, 2, 3, 4, 5},
        {4, 5, 2, 3, 1, 0},
        {2, 3, 1, 0, 4, 5}
    };

    CubeSymmetry() {
        const auto& cat = catalog();
        auto buildMap = [](int N, auto& outX, auto& outZ) {
            double H = (N - 1) * 0.5 + 1.0;
            double mid = (N - 1) * 0.5;
            auto to3D = [N, H, mid](int idx, double& x, double& y, double& z) {
                int f = idx / (N * N), p = idx % (N * N), r = p / N, c = p % N;
                switch (f) {
                    case 0: x = c - mid; y = H;       z = r - mid; break;
                    case 1: x = c - mid; y = -H;      z = mid - r; break;
                    case 2: x = -H;      y = mid - r; z = c - mid; break;
                    case 3: x = H;       y = mid - r; z = mid - c; break;
                    case 4: x = c - mid; y = mid - r; z = H;       break;
                    default:x = mid - c; y = mid - r; z = -H;      break;
                }
            };
            auto find3D = [N, &to3D](double vx, double vy, double vz) -> uint8_t {
                for (int i = 0; i < 6 * N * N; ++i) {
                    double ux, uy, uz;
                    to3D(i, ux, uy, uz);
                    if (std::abs(ux - vx) < 0.01 && std::abs(uy - vy) < 0.01 && std::abs(uz - vz) < 0.01)
                        return static_cast<uint8_t>(i);
                }
                return 0;
            };
            for (int i = 0; i < 6 * N * N; ++i) {
                double x, y, z;
                to3D(i, x, y, z);
                outX[i] = find3D(x, z, -y);
                outZ[i] = find3D(y, -x, z);
            }
        };
        buildMap(4, rotX4, rotZ4);
        buildMap(5, rotX5, rotZ5);

        auto deriveSub24 = [](const int facelets[24], const auto& rotPerm, std::array<uint8_t, 24>& out) {
            for (int i = 0; i < 24; ++i) {
                uint8_t dstF = rotPerm[facelets[i]];
                for (int j = 0; j < 24; ++j) {
                    if (facelets[j] == dstF) { out[j] = static_cast<uint8_t>(i); break; }
                }
            }
        };
        deriveSub24(cat.c4Facelet, rotX4, rotXc4);
        deriveSub24(cat.c4Facelet, rotZ4, rotZc4);
        deriveSub24(cat.t5Facelet, rotX5, rotXt5);
        deriveSub24(cat.t5Facelet, rotZ5, rotZt5);
        deriveSub24(cat.x5Facelet, rotX5, rotXx5);
        deriveSub24(cat.x5Facelet, rotZ5, rotZx5);
    }

    State4x4 rotateState4(const State4x4& s, int sym) const {
        if (sym == 0) return s;
        State4x4 out;
        const auto& rp = (sym == 1) ? rotX4 : rotZ4;
        for (int i = 0; i < 96; ++i) out.facelets[rp[i]] = axRot[sym][s.facelets[i]];
        return out;
    }
    State5x5 rotateState5(const State5x5& s, int sym) const {
        if (sym == 0) return s;
        State5x5 out;
        const auto& rp = (sym == 1) ? rotX5 : rotZ5;
        for (int i = 0; i < 150; ++i) out.facelets[rp[i]] = axRot[sym][s.facelets[i]];
        return out;
    }
    std::array<uint8_t, 24> rotateArr24(const std::array<uint8_t, 24>& a, const std::array<uint8_t, 24>& perm, int sym) const {
        if (sym == 0) return a;
        std::array<uint8_t, 24> out;
        for (int i = 0; i < 24; ++i) out[i] = axRot[sym][a[perm[i]]];
        return out;
    }
    std::vector<Move> unmapMoves(std::vector<Move> mvs, int sym) const {
        if (sym == 0) return mvs;
        for (auto& m : mvs) m.axis = invAxRot[sym][m.axis];
        return mvs;
    }
};

inline const CubeSymmetry& cubeSym() {
    static const CubeSymmetry inst;
    return inst;
}

// ─── 4x4 Optimal Multi-Path Center Solver ─────────────────────────────────────

inline std::vector<std::vector<Move>> solveCenters4x4SingleOri(const std::array<uint8_t, 24>& startC, int topK) {
    auto& ctx = engineCtx();
    ctx.ensureInitialized();
    const auto& cat = catalog();
    const auto& maps = ctx.map4;
    const auto& bank = ctx.bank;

    // Helper: find up to M distinct shortest solutions for Stage 1
    struct Seq { std::array<uint8_t, 24> c; std::vector<int> path; };
    std::vector<Seq> s1Out;
    {
        uint32_t r0 = getCoordUD(startC);
        int h0 = getNibble(bank.c4_ud.data(), r0);
        if (h0 == 0) {
            s1Out.push_back({startC, {}});
        } else {
            int maxKeep = std::max(4, topK);
            std::vector<int> curPath;
            std::function<void(std::array<uint8_t, 24>&, int, int)> dfs =
                [&](std::array<uint8_t, 24>& curC, int depth, int lastAx) {
                if ((int)s1Out.size() >= maxKeep) return;
                uint32_t r = getCoordUD(curC);
                int h = getNibble(bank.c4_ud.data(), r);
                if (h > depth) return;
                if (depth == 0) {
                    if (h == 0) s1Out.push_back({curC, curPath});
                    return;
                }
                for (int m = 0; m < 36; ++m) {
                    int ax = cat.moves36[m].axis;
                    if (ax == lastAx && ((m % 6 < 3) == (curPath.back() % 6 < 3))) continue;
                    std::array<uint8_t, 24> nx;
                    for (int i = 0; i < 24; ++i) nx[i] = curC[cat.c4Perm[m][i]];
                    curPath.push_back(m);
                    dfs(nx, depth - 1, ax);
                    curPath.pop_back();
                    if ((int)s1Out.size() >= maxKeep) return;
                }
            };
            for (int d = h0; d <= 10 && s1Out.empty(); ++d) {
                auto tmp = startC;
                dfs(tmp, d, -1);
            }
        }
    }

    // Stage 2 for each S1 branch
    std::vector<Seq> s2Out;
    {
        int maxKeep = std::max(6, topK * 2);
        for (const auto& st : s1Out) {
            uint32_t idx0 = getCoordS2(st.c);
            uint32_t u0 = idx0 / 12870, l0 = idx0 % 12870;
            int h0 = getNibble(bank.c4_s2.data(), idx0);
            if (h0 == 0) {
                s2Out.push_back(st);
                continue;
            }
            std::vector<int> curPath = st.path;
            int foundHere = 0;
            std::function<void(uint32_t, uint32_t, int, int)> dfs =
                [&](uint32_t u, uint32_t l, int depth, int lastM) {
                if (foundHere >= 3 || (int)s2Out.size() >= maxKeep) return;
                uint32_t idx = u * 12870 + l;
                int h = getNibble(bank.c4_s2.data(), idx);
                if (h > depth) return;
                if (depth == 0) {
                    if (h == 0) {
                        std::array<uint8_t, 24> cc = startC;
                        for (int m : curPath) {
                            std::array<uint8_t, 24> nc;
                            for (int i = 0; i < 24; ++i) nc[i] = cc[cat.c4Perm[m][i]];
                            cc = nc;
                        }
                        s2Out.push_back({cc, curPath});
                        foundHere++;
                    }
                    return;
                }
                for (int mi = 0; mi < 28; ++mi) {
                    int m = maps.s2Moves[mi];
                    if (lastM >= 0 && cat.moves36[m].axis == cat.moves36[lastM].axis &&
                        cat.moves36[m].sliceMask == cat.moves36[lastM].sliceMask) continue;
                    curPath.push_back(m);
                    dfs(maps.udMove70[u][mi], maps.lrMove12870[l][mi], depth - 1, m);
                    curPath.pop_back();
                    if (foundHere >= 3 || (int)s2Out.size() >= maxKeep) return;
                }
            };
            for (int d = h0; d <= 12 && foundHere == 0; ++d) {
                dfs(u0, l0, d, curPath.empty() ? -1 : curPath.back());
            }
        }
    }

    // Stage 3 for each S2 branch
    std::vector<std::vector<Move>> finalSols;
    for (const auto& st : s2Out) {
        uint32_t idx0 = getCoordS3(st.c);
        uint32_t f0 = idx0 % 70, rem0 = idx0 / 70, l0 = rem0 % 70, u0 = rem0 / 70;
        int h0 = getNibble(bank.c4_s3.data(), idx0);
        if (h0 == 0) {
            std::vector<Move> mv;
            for (int m : st.path) mv.push_back(cat.moves36[m]);
            finalSols.push_back(MoveOptimizer::optimize(mv, 4));
            if ((int)finalSols.size() >= topK) break;
            continue;
        }
        std::vector<int> curPath = st.path;
        bool solved = false;
        std::function<bool(uint32_t, uint32_t, uint32_t, int, int)> dfs =
            [&](uint32_t u, uint32_t l, uint32_t f, int depth, int lastM) -> bool {
            uint32_t idx = (u * 70 + l) * 70 + f;
            int h = getNibble(bank.c4_s3.data(), idx);
            if (h > depth) return false;
            if (depth == 0) return (h == 0);
            for (int mi = 0; mi < 24; ++mi) {
                int m = maps.s3Moves[mi];
                if (lastM >= 0 && cat.moves36[m].axis == cat.moves36[lastM].axis &&
                    cat.moves36[m].sliceMask == cat.moves36[lastM].sliceMask) continue;
                curPath.push_back(m);
                if (dfs(maps.s70MoveU[u][mi], maps.s70MoveL[l][mi], maps.s70MoveF[f][mi], depth - 1, m))
                    return true;
                curPath.pop_back();
            }
            return false;
        };
        for (int d = h0; d <= 12 && !solved; ++d) {
            solved = dfs(u0, l0, f0, d, curPath.empty() ? -1 : curPath.back());
        }
        if (solved) {
            std::vector<Move> mv;
            for (int m : curPath) mv.push_back(cat.moves36[m]);
            finalSols.push_back(MoveOptimizer::optimize(mv, 4));
            if ((int)finalSols.size() >= topK) break;
        }
    }

    std::sort(finalSols.begin(), finalSols.end(), [](const auto& a, const auto& b) {
        return a.size() < b.size();
    });
    return finalSols;
}

inline std::vector<std::vector<Move>> solveCenters4x4Multi(const Fast4x4& start, int topK) {
    const auto& sym = cubeSym();
    std::vector<std::vector<Move>> all;
    for (int s = 0; s < 3; ++s) {
        auto rc = (s == 0) ? start.centers
                : sym.rotateArr24(start.centers, (s == 1) ? sym.rotXc4 : sym.rotZc4, s);
        auto res = solveCenters4x4SingleOri(rc, std::max(2, topK));
        for (auto& path : res) {
            all.push_back(sym.unmapMoves(std::move(path), s));
        }
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
    // Keep topK diverse lengths/paths
    if ((int)all.size() > topK * 2) all.resize(topK * 2);
    return all;
}

// ─── Pure-X Center Commutator Catalog (Preserves all 24 T-Center Faces!) ─────

struct PXMacro {
    std::vector<int> moves;
    std::array<uint8_t, 24> xPerm;
};

struct PureXCatalog {
    std::vector<PXMacro> s1; // moves X between {U,D} and {L,R,F,B}
    std::vector<PXMacro> s2; // keeps {U,D}, moves X between {L,R} and {F,B}
    std::vector<PXMacro> s3; // keeps {U,D} and {L,R}/{F,B}, moves X U<->D, L<->R, or F<->B

    PureXCatalog() {
        const auto& cat = catalog();
        const auto& maps = engineCtx().map5T;
        auto invMove = [](int m) -> int {
            int ax = m / 6, base = m % 6;
            return ax * 6 + ((base >= 3) ? 3 : 0) + (2 - (base % 3));
        };
        std::unordered_set<uint64_t> seenX;
        auto hashPerm = [](const std::array<uint8_t, 24>& p) -> uint64_t {
            uint64_t h = 14695981039346656037ULL;
            for (int i = 0; i < 24; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
            return h;
        };

        auto addIfPureX = [&](const std::vector<int>& seq) {
            std::array<uint8_t, 24> t, x;
            for (int i = 0; i < 24; ++i) { t[i] = i / 4; x[i] = i; }
            for (int m : seq) {
                std::array<uint8_t, 24> nt, nx;
                for (int i = 0; i < 24; ++i) {
                    nt[i] = t[cat.t5Perm[m][i]];
                    nx[i] = x[cat.x5Perm[m][i]];
                }
                t = nt; x = nx;
            }
            for (int i = 0; i < 24; ++i) if (t[i] != i / 4) return;
            int diffX = 0;
            bool keepUD = true, keepS2 = true, chgUD = false, chgLRFB = false;
            for (int i = 0; i < 24; ++i) {
                int srcF = x[i] / 4, dstF = i / 4;
                if (srcF != dstF) diffX++;
                if ((srcF < 2) != (dstF < 2)) { keepUD = false; chgUD = true; }
                if ((srcF / 2) != (dstF / 2)) { keepS2 = false; chgLRFB = true; }
            }
            if (diffX == 0 || diffX > 6) return;
            if (!seenX.insert(hashPerm(x)).second) return;

            PXMacro pm{seq, x};
            if (chgUD) s1.push_back(pm);
            if (keepUD && chgLRFB) s2.push_back(pm);
            if (keepS2) s3.push_back(pm);
        };

        // 1. Short 6-move wide2-outer conjugates (e.g., Uw2 L Uw2 Fw2 R' Fw2)
        std::vector<int> wide2s, wides, outers = {-1}, outer18;
        for (int ax = 0; ax < 6; ++ax) {
            wide2s.push_back(ax * 6 + 4);
            for (int p = 3; p < 6; ++p) wides.push_back(ax * 6 + p);
            for (int p = 0; p < 3; ++p) { outers.push_back(ax * 6 + p); outer18.push_back(ax * 6 + p); }
        }
        for (int w1 : wide2s) {
            for (int o1 : outer18) {
                if ((w1 / 6) == (o1 / 6)) continue;
                for (int w2 : wide2s) {
                    if ((w1 / 6) == (w2 / 6)) continue;
                    for (int o2 : outer18) {
                        if ((w2 / 6) == (o2 / 6)) continue;
                        addIfPureX({w1, o1, w1, w2, o2, w2});
                    }
                }
            }
        }
        // 2. Wide commutators [A, S B S'] (6..8 moves)
        for (int a : wides) {
            for (int s : outers) {
                for (int b : wides) {
                    if ((a / 6) == (b / 6)) continue;
                    std::vector<int> seq = {a};
                    if (s >= 0) seq.push_back(s);
                    seq.push_back(b);
                    if (s >= 0) seq.push_back(invMove(s));
                    seq.push_back(invMove(a));
                    if (s >= 0) seq.push_back(s);
                    seq.push_back(invMove(b));
                    if (s >= 0) seq.push_back(invMove(s));
                    addIfPureX(seq);
                }
            }
        }
        auto byLen = [](const PXMacro& a, const PXMacro& b) { return a.moves.size() < b.moves.size(); };
        std::sort(s1.begin(), s1.end(), byLen);
        std::sort(s2.begin(), s2.end(), byLen);
        std::sort(s3.begin(), s3.end(), byLen);
        (void)maps;
    }
};

inline const PureXCatalog& pureXCatalog() {
    static const PureXCatalog inst;
    return inst;
}

// ─── 5x5 Joint T+X Multi-Path Center Solver (Beam + Pure-X Guaranteed Finish) ───

inline std::vector<std::vector<Move>> solveCenters5x5SingleOri(const std::array<uint8_t, 24>& startT,
                                                              const std::array<uint8_t, 24>& startX,
                                                              int topK) {
    auto& ctx = engineCtx();
    ctx.ensureInitialized();
    const auto& cat = catalog();
    const auto& mapT = ctx.map5T;
    const auto& mapX = ctx.map5X;
    const auto& bank = ctx.bank;
    const auto& pxc = pureXCatalog();

    struct Seq5 { std::array<uint8_t, 24> t, x; std::vector<int> path; };

    auto applyMoveArr = [&](std::array<uint8_t, 24>& arr, const uint8_t perm[24]) {
        std::array<uint8_t, 24> na;
        for (int i = 0; i < 24; ++i) na[i] = arr[perm[i]];
        arr = na;
    };
    auto applySeq5 = [&](std::array<uint8_t, 24>& t, std::array<uint8_t, 24>& x, const std::vector<int>& seq) {
        for (int m : seq) {
            applyMoveArr(t, cat.t5Perm[m]);
            applyMoveArr(x, cat.x5Perm[m]);
        }
    };
    auto hashTX = [](const std::array<uint8_t, 24>& t, const std::array<uint8_t, 24>& x) -> uint64_t {
        uint64_t h = 14695981039346656037ULL;
        for (int i = 0; i < 24; ++i) {
            h ^= static_cast<uint64_t>(t[i]) * 8ULL + x[i];
            h *= 1099511628211ULL;
        }
        return h;
    };

    // Fast 1-2 outer setup + PureX fixer for Stage 1 / Stage 2 / Stage 3
    auto fixXWithPureX = [&](std::array<uint8_t, 24> t, std::array<uint8_t, 24> x, std::vector<int> path,
                             const std::vector<PXMacro>& macros,
                             const std::function<int(const std::array<uint8_t, 24>&)>& badFn,
                             const std::vector<int>& allowedSetupMoves) -> Seq5 {
        for (int guard = 0; guard < 12; ++guard) {
            int curBad = badFn(x);
            if (curBad == 0) break;

            int bestBad = curBad;
            double bestCostPerFix = 1e9;
            std::vector<int> bestSeq;
            std::array<uint8_t, 24> bestNX = x;

            auto evalXSetup = [&](const std::array<uint8_t, 24>& sx, const std::vector<int>& setup) {
                for (const auto& pm : macros) {
                    std::array<uint8_t, 24> nx;
                    for (int i = 0; i < 24; ++i) nx[i] = sx[pm.xPerm[i]];
                    int b = badFn(nx);
                    int gain = curBad - b;
                    if (gain <= 0) continue;
                    double totalLen = (double)(setup.size() + pm.moves.size());
                    double cpf = totalLen / (double)gain - (b == 0 ? 5.0 : 0.0);
                    if (cpf < bestCostPerFix - 1e-6 || (std::abs(cpf - bestCostPerFix) < 1e-6 && b < bestBad)) {
                        bestCostPerFix = cpf;
                        bestBad = b;
                        bestNX = nx;
                        bestSeq = setup;
                        bestSeq.insert(bestSeq.end(), pm.moves.begin(), pm.moves.end());
                    }
                }
            };

            // Check both 0 and 1 outer setup moves so a 1-move setup fixing 2 centers beats a 0-move setup fixing 1!
            evalXSetup(x, {});
            if (bestBad > 0) {
                for (int m1 : allowedSetupMoves) {
                    auto nx1 = x;
                    applyMoveArr(nx1, cat.x5Perm[m1]);
                    evalXSetup(nx1, {m1});
                }
            }
            // 2 outer moves on distinct axes only if 0/1 made no progress
            if (bestBad == curBad) {
                for (int m1 : allowedSetupMoves) {
                    auto nx1 = x;
                    applyMoveArr(nx1, cat.x5Perm[m1]);
                    for (int m2 : allowedSetupMoves) {
                        if ((m2 / 6) <= (m1 / 6)) continue;
                        auto nx2 = nx1;
                        applyMoveArr(nx2, cat.x5Perm[m2]);
                        evalXSetup(nx2, {m1, m2});
                        if (bestBad == 0) break;
                    }
                    if (bestBad == 0) break;
                }
            }

            if (bestBad >= curBad || bestSeq.empty()) break;
            applySeq5(t, x, bestSeq);
            path.insert(path.end(), bestSeq.begin(), bestSeq.end());
        }
        return {t, x, path};
    };

    std::vector<int> allOuter18, lrfbOuter12;
    for (int ax = 0; ax < 6; ++ax) {
        for (int p = 0; p < 3; ++p) {
            allOuter18.push_back(ax * 6 + p);
            if (ax >= 2) lrfbOuter12.push_back(ax * 6 + p);
        }
    }
    auto badS1 = [](const std::array<uint8_t, 24>& x) {
        int c = 0; for (int i = 0; i < 8; ++i) if (x[i] >= 2) c++;
        return c;
    };
    auto badS2 = [](const std::array<uint8_t, 24>& x) {
        int c = 0; for (int i = 8; i < 16; ++i) if (x[i] >= 4) c++;
        return c;
    };
    auto badS3 = [](const std::array<uint8_t, 24>& x) {
        int c = 0; for (int i = 0; i < 24; ++i) if (x[i] != (i / 4)) c++;
        return c;
    };

    // Exact 4x4-style IDA* helper on T-centers ONLY (guarantees 100% T-center convergence!)
    auto solveT_S1 = [&](const std::array<uint8_t, 24>& startT) -> std::vector<int> {
        int h0 = getNibble(bank.t5_ud.data(), getCoordUD(startT));
        if (h0 == 0) return {};
        std::vector<int> p;
        std::function<bool(std::array<uint8_t, 24>&, int, int)> dfs =
            [&](std::array<uint8_t, 24>& cur, int d, int lm) -> bool {
            int h = getNibble(bank.t5_ud.data(), getCoordUD(cur));
            if (h > d) return false;
            if (d == 0) return (h == 0);
            for (int m = 0; m < 36; ++m) {
                if (lm >= 0 && cat.moves36[m].axis == cat.moves36[lm].axis &&
                    (m % 6 < 3) == (lm % 6 < 3)) continue;
                auto nx = cur;
                applyMoveArr(nx, cat.t5Perm[m]);
                p.push_back(m);
                if (dfs(nx, d - 1, m)) return true;
                p.pop_back();
            }
            return false;
        };
        for (int d = h0; d <= 10; ++d) {
            auto tmp = startT;
            if (dfs(tmp, d, -1)) return p;
        }
        return {};
    };

    auto solveT_S2 = [&](const std::array<uint8_t, 24>& startT) -> std::vector<int> {
        uint32_t idx0 = getCoordS2(startT);
        int h0 = getNibble(bank.t5_s2.data(), idx0);
        if (h0 == 0) return {};
        uint32_t u0 = idx0 / 12870, l0 = idx0 % 12870;
        std::vector<int> p;
        std::function<bool(uint32_t, uint32_t, int, int)> dfs =
            [&](uint32_t u, uint32_t l, int d, int lm) -> bool {
            int h = getNibble(bank.t5_s2.data(), u * 12870 + l);
            if (h > d) return false;
            if (d == 0) return (h == 0);
            for (int mi = 0; mi < 28; ++mi) {
                int m = mapT.s2Moves[mi];
                if (lm >= 0 && cat.moves36[m].axis == cat.moves36[lm].axis &&
                    cat.moves36[m].sliceMask == cat.moves36[lm].sliceMask) continue;
                p.push_back(m);
                if (dfs(mapT.udMove70[u][mi], mapT.lrMove12870[l][mi], d - 1, m)) return true;
                p.pop_back();
            }
            return false;
        };
        for (int d = h0; d <= 12; ++d) {
            if (dfs(u0, l0, d, -1)) return p;
        }
        return {};
    };

    auto solveT_S3 = [&](const std::array<uint8_t, 24>& startT) -> std::vector<int> {
        uint32_t idx0 = getCoordS3(startT);
        int h0 = getNibble(bank.t5_s3.data(), idx0);
        if (h0 == 0) return {};
        uint32_t f0 = idx0 % 70, r0 = idx0 / 70, l0 = r0 % 70, u0 = r0 / 70;
        std::vector<int> p;
        std::function<bool(uint32_t, uint32_t, uint32_t, int, int)> dfs =
            [&](uint32_t u, uint32_t l, uint32_t f, int d, int lm) -> bool {
            int h = getNibble(bank.t5_s3.data(), (u * 70 + l) * 70 + f);
            if (h > d) return false;
            if (d == 0) return (h == 0);
            for (int mi = 0; mi < 24; ++mi) {
                int m = mapT.s3Moves[mi];
                if (lm >= 0 && cat.moves36[m].axis == cat.moves36[lm].axis &&
                    cat.moves36[m].sliceMask == cat.moves36[lm].sliceMask) continue;
                p.push_back(m);
                if (dfs(mapT.s70MoveU[u][mi], mapT.s70MoveL[l][mi], mapT.s70MoveF[f][mi], d - 1, m)) return true;
                p.pop_back();
            }
            return false;
        };
        for (int d = h0; d <= 12; ++d) {
            if (dfs(u0, l0, f0, d, -1)) return p;
        }
        return {};
    };

    auto addTOnly = [](std::vector<Seq5>& pool, Seq5 cand, const std::function<int(const std::array<uint8_t, 24>&)>& bFn) {
        size_t scoreNew = cand.path.size() + static_cast<size_t>(bFn(cand.x)) * 6;
        if (pool.size() < 24) {
            pool.push_back(std::move(cand));
            return;
        }
        size_t worstIdx = 0, worstSc = 0;
        for (size_t i = 0; i < pool.size(); ++i) {
            size_t sc = pool[i].path.size() + static_cast<size_t>(bFn(pool[i].x)) * 6;
            if (sc > worstSc) { worstSc = sc; worstIdx = i; }
        }
        if (scoreNew < worstSc) pool[worstIdx] = std::move(cand);
    };

    // Tiny exact joint IDA* snapper when max(ht, hx) <= 3 (completes (0,0) in <= 3 moves without Pure-X!)
    auto snapS1 = [&](const std::array<uint8_t, 24>& t0, const std::array<uint8_t, 24>& x0,
                      int lm0, std::vector<int>& suffix) -> bool {
        int ht = getNibble(bank.t5_ud.data(), getCoordUD(t0));
        int hx = getNibble(bank.x5_ud.data(), getCoordUD(x0));
        int d0 = std::max(ht, hx);
        if (d0 == 0) return true;
        if (d0 > 3) return false;
        std::function<bool(const std::array<uint8_t, 24>&, const std::array<uint8_t, 24>&, int, int)> dfs =
            [&](const std::array<uint8_t, 24>& tc, const std::array<uint8_t, 24>& xc, int d, int lm) -> bool {
            int htc = getNibble(bank.t5_ud.data(), getCoordUD(tc));
            int hxc = getNibble(bank.x5_ud.data(), getCoordUD(xc));
            if (std::max(htc, hxc) > d) return false;
            if (d == 0) return (htc == 0 && hxc == 0);
            for (int m = 0; m < 36; ++m) {
                if (lm >= 0 && cat.moves36[m].axis == cat.moves36[lm].axis &&
                    cat.moves36[m].sliceMask == cat.moves36[lm].sliceMask) continue;
                auto nt = tc, nx = xc;
                applyMoveArr(nt, cat.t5Perm[m]);
                applyMoveArr(nx, cat.x5Perm[m]);
                suffix.push_back(m);
                if (dfs(nt, nx, d - 1, m)) return true;
                suffix.pop_back();
            }
            return false;
        };
        for (int d = d0; d <= 3; ++d) {
            suffix.clear();
            if (dfs(t0, x0, d, lm0)) return true;
        }
        return false;
    };

    auto adjPairs = [](const std::array<uint8_t, 24>& t, const std::array<uint8_t, 24>& x, auto matchFn) {
        int b = 0;
        for (int f = 0; f < 6; ++f) {
            int o = f * 4;
            b += matchFn(f, t[o+0], x[o+0]) + matchFn(f, t[o+0], x[o+1]);
            b += matchFn(f, t[o+1], x[o+0]) + matchFn(f, t[o+1], x[o+2]);
            b += matchFn(f, t[o+2], x[o+1]) + matchFn(f, t[o+2], x[o+3]);
            b += matchFn(f, t[o+3], x[o+2]) + matchFn(f, t[o+3], x[o+3]);
        }
        return b;
    };
    auto barS1 = [&](const std::array<uint8_t, 24>& t, const std::array<uint8_t, 24>& x) {
        return adjPairs(t, x, [](int, uint8_t a, uint8_t b){ return (a < 2 && b < 2) ? 1 : 0; });
    };
    auto barS2 = [&](const std::array<uint8_t, 24>& t, const std::array<uint8_t, 24>& x) {
        return adjPairs(t, x, [](int f, uint8_t a, uint8_t b){
            if (f < 2) return (a == b) ? 2 : 0;
            return (a == b) ? 2 : (((a >> 1) == (b >> 1)) ? 1 : 0);
        });
    };
    auto barS3 = [&](const std::array<uint8_t, 24>& t, const std::array<uint8_t, 24>& x) {
        return adjPairs(t, x, [](int, uint8_t a, uint8_t b){ return (a == b) ? 1 : 0; });
    };

    auto appendArena = [](const std::vector<ArenaStep>& arena, int nodeId, std::vector<int> base) {
        std::vector<int> rev;
        for (int p = nodeId; p >= 0; p = arena[p].parent) rev.push_back(arena[p].m);
        for (auto it = rev.rbegin(); it != rev.rend(); ++it) base.push_back(*it);
        return base;
    };

    // ─── STAGE 1: U/D Split (Arena + Bar Score) ───────────────────────────────
    std::vector<Seq5> s1Direct, s1TOnly;
    {
        int ht0 = getNibble(bank.t5_ud.data(), getCoordUD(startT));
        int hx0 = getNibble(bank.x5_ud.data(), getCoordUD(startX));
        std::vector<int> sfx;
        if (ht0 == 0 && hx0 == 0) s1Direct.push_back({startT, startX, {}});
        else if (snapS1(startT, startX, -1, sfx)) {
            auto t = startT, x = startX;
            applySeq5(t, x, sfx);
            s1Direct.push_back({t, x, sfx});
        } else {
            if (ht0 == 0) addTOnly(s1TOnly, {startT, startX, {}}, badS1);
            struct B1 { std::array<uint8_t, 24> t, x; int nid; int16_t lm, len; double sc; };
            std::vector<ArenaStep> arena; arena.reserve(65536);
            std::vector<B1> beam = {{startT, startX, -1, -1, 0, 0.0}};
            std::unordered_set<uint64_t> vis; vis.reserve(32768); vis.insert(hashTX(startT, startX));
            for (int step = 0; step < 16 && (int)s1Direct.size() < 6 && !beam.empty(); ++step) {
                std::vector<B1> nextB;
                nextB.reserve(beam.size() * 18);
                for (const auto& c : beam) {
                    for (int m = 0; m < 36; ++m) {
                        if (c.lm >= 0 && cat.moves36[m].axis == cat.moves36[c.lm].axis &&
                            cat.moves36[m].sliceMask == cat.moves36[c.lm].sliceMask) continue;
                        std::array<uint8_t, 24> nt = c.t, nx = c.x;
                        applyMoveArr(nt, cat.t5Perm[m]);
                        applyMoveArr(nx, cat.x5Perm[m]);
                        if (!vis.insert(hashTX(nt, nx)).second) continue;
                        int ht = getNibble(bank.t5_ud.data(), getCoordUD(nt));
                        int hx = getNibble(bank.x5_ud.data(), getCoordUD(nx));
                        arena.push_back({c.nid, static_cast<int16_t>(m)});
                        int nid = static_cast<int>(arena.size()) - 1;
                        if (ht == 0 && hx == 0) {
                            s1Direct.push_back({nt, nx, appendArena(arena, nid, {})});
                        } else {
                            if (ht == 0) addTOnly(s1TOnly, {nt, nx, appendArena(arena, nid, {})}, badS1);
                            double sc = std::max(ht, hx) * 5.0 + (ht + hx) * 3.0 - barS1(nt, nx) * 1.1;
                            nextB.push_back({nt, nx, nid, static_cast<int16_t>(m), static_cast<int16_t>(c.len + 1), sc});
                        }
                    }
                }
                std::sort(nextB.begin(), nextB.end(), [](const B1& a, const B1& b){ return a.sc < b.sc; });
                if ((int)nextB.size() > 450) nextB.resize(450);
                beam = std::move(nextB);
            }
            if (s1Direct.empty()) {
                for (size_t i = 0; i < std::min<size_t>(20, beam.size()) && (int)s1Direct.size() < 4; ++i) {
                    if (snapS1(beam[i].t, beam[i].x, beam[i].lm, sfx)) {
                        auto t2 = beam[i].t, x2 = beam[i].x;
                        applySeq5(t2, x2, sfx);
                        auto np2 = appendArena(arena, beam[i].nid, {});
                        np2.insert(np2.end(), sfx.begin(), sfx.end());
                        s1Direct.push_back({t2, x2, np2});
                    }
                }
            }
        }
        if (s1Direct.empty() && s1TOnly.empty()) {
            auto tp = solveT_S1(startT);
            auto t = startT, x = startX;
            applySeq5(t, x, tp);
            s1TOnly.push_back({t, x, tp});
        }
        if ((int)s1Direct.size() < 4 && !s1TOnly.empty()) {
            std::sort(s1TOnly.begin(), s1TOnly.end(), [&](const Seq5& a, const Seq5& b) {
                return (a.path.size() + badS1(a.x) * 8) < (b.path.size() + badS1(b.x) * 8);
            });
            for (size_t i = 0; i < s1TOnly.size() && (int)s1Direct.size() < 4; ++i) {
                Seq5 fixed = fixXWithPureX(s1TOnly[i].t, s1TOnly[i].x, s1TOnly[i].path, pxc.s1, badS1, allOuter18);
                if (badS1(fixed.t) == 0 && badS1(fixed.x) == 0) s1Direct.push_back(std::move(fixed));
            }
        }
    }
    if (s1Direct.empty()) return {};

    // ─── STAGE 2: L/R vs F/B Split (Arena + bwd5_s2 d<=4 + Bar Score) ──────────
    std::vector<Seq5> s2Direct, s2TOnly;
    {
        std::vector<std::vector<int>> basePaths;
        struct B2 {
            uint16_t ut, lt, ux, lx;
            std::array<uint8_t, 24> t, x;
            int baseIdx, nid;
            int16_t lm, len;
            double sc;
        };
        std::vector<ArenaStep> arena; arena.reserve(65536);
        std::vector<B2> beam;
        std::unordered_set<uint64_t> vis; vis.reserve(32768);

        auto tryTraceBwd2 = [&](std::array<uint8_t, 24> t, std::array<uint8_t, 24> x,
                                uint16_t ut, uint16_t lt, uint16_t ux, uint16_t lx,
                                int rem, std::vector<int> path) {
            int lm = -1;
            for (int step = 0; step < rem; ++step) {
                for (int mi = 0; mi < 28; ++mi) {
                    int m = mapT.s2Moves[mi];
                    if (lm >= 0 && cat.moves36[m].axis == cat.moves36[lm].axis &&
                        cat.moves36[m].sliceMask == cat.moves36[lm].sliceMask) continue;
                    uint16_t nut = mapT.udMove70[ut][mi], nlt = mapT.lrMove12870[lt][mi];
                    uint16_t nux = mapX.udMove70[ux][mi], nlx = mapX.lrMove12870[lx][mi];
                    uint64_t bk = (((uint64_t)nut * 12870ULL + nlt) << 22) | ((uint64_t)nux * 12870ULL + nlx);
                    if (ctx.bwd5_s2.find(bk) == rem - 1 - step) {
                        applyMoveArr(t, cat.t5Perm[m]);
                        applyMoveArr(x, cat.x5Perm[m]);
                        path.push_back(m);
                        ut = nut; lt = nlt; ux = nux; lx = nlx; lm = m;
                        break;
                    }
                }
            }
            if (getCoordS2(t) == 0 && getCoordS2(x) == 0 && badS2(t) == 0 && badS2(x) == 0) {
                s2Direct.push_back({t, x, std::move(path)});
            }
        };

        for (const auto& s : s1Direct) {
            uint32_t it = getCoordS2(s.t), ix = getCoordS2(s.x);
            uint16_t ut = static_cast<uint16_t>(it / 12870), lt = static_cast<uint16_t>(it % 12870);
            uint16_t ux = static_cast<uint16_t>(ix / 12870), lx = static_cast<uint16_t>(ix % 12870);
            uint64_t bk = (((uint64_t)ut * 12870ULL + lt) << 22) | ((uint64_t)ux * 12870ULL + lx);
            int dBwd = ctx.bwd5_s2.find(bk);
            if (dBwd >= 0) {
                tryTraceBwd2(s.t, s.x, ut, lt, ux, lx, dBwd, s.path);
            } else {
                if (getNibble(bank.t5_s2.data(), it) == 0) addTOnly(s2TOnly, s, badS2);
                if (vis.insert(hashTX(s.t, s.x)).second) {
                    int bIdx = static_cast<int>(basePaths.size());
                    basePaths.push_back(s.path);
                    beam.push_back({ut, lt, ux, lx, s.t, s.x, bIdx, -1, -1, static_cast<int16_t>(s.path.size()), (double)s.path.size()});
                }
            }
        }
        for (int step = 0; step < 18 && (int)s2Direct.size() < 6 && !beam.empty(); ++step) {
            std::vector<B2> nextB;
            nextB.reserve(beam.size() * 14);
            for (const auto& c : beam) {
                for (int mi = 0; mi < 28; ++mi) {
                    int m = mapT.s2Moves[mi];
                    if (c.lm >= 0 && cat.moves36[m].axis == cat.moves36[c.lm].axis &&
                        cat.moves36[m].sliceMask == cat.moves36[c.lm].sliceMask) continue;
                    std::array<uint8_t, 24> nt = c.t, nx = c.x;
                    applyMoveArr(nt, cat.t5Perm[m]);
                    applyMoveArr(nx, cat.x5Perm[m]);
                    if (!vis.insert(hashTX(nt, nx)).second) continue;
                    uint16_t nut = mapT.udMove70[c.ut][mi], nlt = mapT.lrMove12870[c.lt][mi];
                    uint16_t nux = mapX.udMove70[c.ux][mi], nlx = mapX.lrMove12870[c.lx][mi];
                    uint64_t bk = (((uint64_t)nut * 12870ULL + nlt) << 22) | ((uint64_t)nux * 12870ULL + nlx);
                    arena.push_back({c.nid, static_cast<int16_t>(m)});
                    int nid = static_cast<int>(arena.size()) - 1;
                    int dBwd = ctx.bwd5_s2.find(bk);
                    if (dBwd >= 0) {
                        tryTraceBwd2(nt, nx, nut, nlt, nux, nlx, dBwd, appendArena(arena, nid, basePaths[c.baseIdx]));
                    } else {
                        int ht = getNibble(bank.t5_s2.data(), (uint32_t)nut * 12870U + nlt);
                        int hx = getNibble(bank.x5_s2.data(), (uint32_t)nux * 12870U + nlx);
                        if (ht == 0) addTOnly(s2TOnly, {nt, nx, appendArena(arena, nid, basePaths[c.baseIdx])}, badS2);
                        double sc = (double)(c.len + 1) * 1.1 + std::max(ht, hx) * 5.0 + (ht + hx) * 3.0 - barS2(nt, nx) * 0.65;
                        nextB.push_back({nut, nlt, nux, nlx, nt, nx, c.baseIdx, nid, static_cast<int16_t>(m), static_cast<int16_t>(c.len + 1), sc});
                    }
                }
            }
            std::sort(nextB.begin(), nextB.end(), [](const B2& a, const B2& b){ return a.sc < b.sc; });
            if ((int)nextB.size() > 450) nextB.resize(450);
            beam = std::move(nextB);
        }
        if (s2Direct.empty() && s2TOnly.empty()) {
            for (size_t i = 0; i < std::min<size_t>(2, s1Direct.size()); ++i) {
                auto tp = solveT_S2(s1Direct[i].t);
                auto t = s1Direct[i].t, x = s1Direct[i].x;
                applySeq5(t, x, tp);
                auto np = s1Direct[i].path; np.insert(np.end(), tp.begin(), tp.end());
                s2TOnly.push_back({t, x, np});
            }
        }
        if ((int)s2Direct.size() < 4 && !s2TOnly.empty()) {
            std::sort(s2TOnly.begin(), s2TOnly.end(), [&](const Seq5& a, const Seq5& b) {
                return (a.path.size() + badS2(a.x) * 8) < (b.path.size() + badS2(b.x) * 8);
            });
            for (size_t i = 0; i < s2TOnly.size() && (int)s2Direct.size() < 4; ++i) {
                Seq5 fixed = fixXWithPureX(s2TOnly[i].t, s2TOnly[i].x, s2TOnly[i].path, pxc.s2, badS2, lrfbOuter12);
                if (badS2(fixed.t) == 0 && badS2(fixed.x) == 0) s2Direct.push_back(std::move(fixed));
            }
        }
    }
    if (s2Direct.empty()) return {};

    // ─── STAGE 3: All 6 Faces Exact Finish (Arena + bwd5_s3 d<=5 + Bar Score) ─
    std::vector<std::vector<Move>> finalSols;
    {
        std::vector<std::vector<int>> basePaths;
        struct B3 {
            uint8_t ut, lt, ft, ux, lx, fx;
            std::array<uint8_t, 24> t, x;
            int baseIdx, nid;
            int16_t lm, len;
            double sc;
        };
        std::vector<ArenaStep> arena; arena.reserve(65536);
        std::vector<B3> beam;
        std::vector<Seq5> s3TOnly;
        std::unordered_set<uint64_t> vis; vis.reserve(32768);

        auto tryTraceBwd3 = [&](std::array<uint8_t, 24> t, std::array<uint8_t, 24> x,
                                uint8_t ut, uint8_t lt, uint8_t ft, uint8_t ux, uint8_t lx, uint8_t fx,
                                int rem, std::vector<int> path) {
            int lm = -1;
            for (int step = 0; step < rem; ++step) {
                for (int mi = 0; mi < 24; ++mi) {
                    int m = mapT.s3Moves[mi];
                    if (lm >= 0 && cat.moves36[m].axis == cat.moves36[lm].axis &&
                        cat.moves36[m].sliceMask == cat.moves36[lm].sliceMask) continue;
                    uint8_t nut = mapT.s70MoveU[ut][mi], nlt = mapT.s70MoveL[lt][mi], nft = mapT.s70MoveF[ft][mi];
                    uint8_t nux = mapX.s70MoveU[ux][mi], nlx = mapX.s70MoveL[lx][mi], nfx = mapX.s70MoveF[fx][mi];
                    uint32_t it = ((uint32_t)nut * 70U + nlt) * 70U + nft;
                    uint32_t ix = ((uint32_t)nux * 70U + nlx) * 70U + nfx;
                    if (ctx.bwd5_s3.find(((uint64_t)it << 20) | ix) == rem - 1 - step) {
                        applyMoveArr(t, cat.t5Perm[m]);
                        applyMoveArr(x, cat.x5Perm[m]);
                        path.push_back(m);
                        ut = nut; lt = nlt; ft = nft; ux = nux; lx = nlx; fx = nfx; lm = m;
                        break;
                    }
                }
            }
            if (badS3(t) == 0 && badS3(x) == 0) {
                std::vector<Move> mv;
                mv.reserve(path.size());
                for (int idx : path) mv.push_back(cat.moves36[idx]);
                finalSols.push_back(MoveOptimizer::optimize(mv, 5));
            }
        };

        for (const auto& s : s2Direct) {
            uint32_t it = getCoordS3(s.t), ix = getCoordS3(s.x);
            uint32_t ft = it % 70, rt = it / 70, lt = rt % 70, ut = rt / 70;
            uint32_t fx = ix % 70, rx = ix / 70, lx = rx % 70, ux = rx / 70;
            int dBwd = ctx.bwd5_s3.find(((uint64_t)it << 20) | ix);
            if (dBwd >= 0) {
                tryTraceBwd3(s.t, s.x, (uint8_t)ut, (uint8_t)lt, (uint8_t)ft, (uint8_t)ux, (uint8_t)lx, (uint8_t)fx, dBwd, s.path);
            } else {
                if (getNibble(bank.t5_s3.data(), it) == 0) addTOnly(s3TOnly, s, badS3);
                if (vis.insert(hashTX(s.t, s.x)).second) {
                    int bIdx = static_cast<int>(basePaths.size());
                    basePaths.push_back(s.path);
                    beam.push_back({(uint8_t)ut, (uint8_t)lt, (uint8_t)ft, (uint8_t)ux, (uint8_t)lx, (uint8_t)fx,
                                    s.t, s.x, bIdx, -1, -1, static_cast<int16_t>(s.path.size()), (double)s.path.size()});
                }
            }
        }
        for (int step = 0; step < 20 && (int)finalSols.size() < topK && !beam.empty(); ++step) {
            std::vector<B3> nextB;
            nextB.reserve(beam.size() * 14);
            for (const auto& c : beam) {
                for (int mi = 0; mi < 24; ++mi) {
                    int m = mapT.s3Moves[mi];
                    if (c.lm >= 0 && cat.moves36[m].axis == cat.moves36[c.lm].axis &&
                        cat.moves36[m].sliceMask == cat.moves36[c.lm].sliceMask) continue;
                    std::array<uint8_t, 24> nt = c.t, nx = c.x;
                    applyMoveArr(nt, cat.t5Perm[m]);
                    applyMoveArr(nx, cat.x5Perm[m]);
                    if (!vis.insert(hashTX(nt, nx)).second) continue;
                    uint8_t nut = mapT.s70MoveU[c.ut][mi], nlt = mapT.s70MoveL[c.lt][mi], nft = mapT.s70MoveF[c.ft][mi];
                    uint8_t nux = mapX.s70MoveU[c.ux][mi], nlx = mapX.s70MoveL[c.lx][mi], nfx = mapX.s70MoveF[c.fx][mi];
                    uint32_t it = ((uint32_t)nut * 70U + nlt) * 70U + nft;
                    uint32_t ix = ((uint32_t)nux * 70U + nlx) * 70U + nfx;
                    arena.push_back({c.nid, static_cast<int16_t>(m)});
                    int nid = static_cast<int>(arena.size()) - 1;
                    int dBwd = ctx.bwd5_s3.find(((uint64_t)it << 20) | ix);
                    if (dBwd >= 0) {
                        tryTraceBwd3(nt, nx, nut, nlt, nft, nux, nlx, nfx, dBwd, appendArena(arena, nid, basePaths[c.baseIdx]));
                        if ((int)finalSols.size() >= topK) break;
                    } else {
                        int ht = getNibble(bank.t5_s3.data(), it);
                        int hx = getNibble(bank.x5_s3.data(), ix);
                        if (ht == 0) addTOnly(s3TOnly, {nt, nx, appendArena(arena, nid, basePaths[c.baseIdx])}, badS3);
                        double sc = (double)(c.len + 1) * 1.1 + std::max(ht, hx) * 5.0 + (ht + hx) * 3.0 - barS3(nt, nx) * 1.1;
                        nextB.push_back({nut, nlt, nft, nux, nlx, nfx, nt, nx, c.baseIdx, nid, static_cast<int16_t>(m), static_cast<int16_t>(c.len + 1), sc});
                    }
                }
                if ((int)finalSols.size() >= topK) break;
            }
            std::sort(nextB.begin(), nextB.end(), [](const B3& a, const B3& b){ return a.sc < b.sc; });
            if ((int)nextB.size() > 450) nextB.resize(450);
            beam = std::move(nextB);
        }
        if (finalSols.empty() && s3TOnly.empty()) {
            for (size_t i = 0; i < std::min<size_t>(2, s2Direct.size()); ++i) {
                auto tp = solveT_S3(s2Direct[i].t);
                auto t = s2Direct[i].t, x = s2Direct[i].x;
                applySeq5(t, x, tp);
                auto np = s2Direct[i].path; np.insert(np.end(), tp.begin(), tp.end());
                s3TOnly.push_back({t, x, np});
            }
        }
        if (finalSols.empty() && !s3TOnly.empty()) {
            std::sort(s3TOnly.begin(), s3TOnly.end(), [&](const Seq5& a, const Seq5& b) {
                return (a.path.size() + badS3(a.x) * 8) < (b.path.size() + badS3(b.x) * 8);
            });
            for (size_t i = 0; i < s3TOnly.size() && finalSols.empty(); ++i) {
                Seq5 fixed = fixXWithPureX(s3TOnly[i].t, s3TOnly[i].x, s3TOnly[i].path, pxc.s3, badS3, allOuter18);
                if (badS3(fixed.t) == 0 && badS3(fixed.x) == 0) {
                    std::vector<Move> mv; for (int idx : fixed.path) mv.push_back(cat.moves36[idx]);
                    finalSols.push_back(MoveOptimizer::optimize(mv, 5));
                }
            }
        }
    }

    std::sort(finalSols.begin(), finalSols.end(), [](const auto& a, const auto& b) {
        return a.size() < b.size();
    });
    return finalSols;
}

inline std::vector<std::vector<Move>> solveCenters5x5Multi(const Fast5x5& start, int topK) {
    const auto& sym = cubeSym();
    std::vector<std::vector<Move>> all;
    for (int s = 0; s < 3; ++s) {
        auto rt = (s == 0) ? start.tCenters
                : sym.rotateArr24(start.tCenters, (s == 1) ? sym.rotXt5 : sym.rotZt5, s);
        auto rx = (s == 0) ? start.xCenters
                : sym.rotateArr24(start.xCenters, (s == 1) ? sym.rotXx5 : sym.rotZx5, s);
        auto res = solveCenters5x5SingleOri(rt, rx, std::min(2, std::max(1, topK)));
        for (auto& path : res) {
            all.push_back(sym.unmapMoves(std::move(path), s));
        }
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
    if ((int)all.size() > topK * 2) all.resize(topK * 2);
    return all;
}

// ─── Macro Library & Beam Search for 4x4 Wings & 5x5 Tredges ──────────────────

struct MacroSeq {
    std::vector<int> moveIdx; // indices in catalog().moves36
    int netAxisFirst = -1;
    int netAxisLast = -1;
};

struct TriggerIndexList {
    std::vector<std::vector<int>> triggers; // fast move indices into catalog().moves36
    TriggerIndexList() {
        const auto& cat = catalog();
        auto toIdx = [&](const std::string& str) {
            std::vector<int> out;
            for (const auto& mv : parseMoves(str)) {
                for (int i = 0; i < 36; ++i) {
                    if (cat.moves36[i].axis == mv.axis &&
                        cat.moves36[i].sliceMask == mv.sliceMask &&
                        cat.moves36[i].power == mv.power) {
                        out.push_back(i);
                        break;
                    }
                }
            }
            return out;
        };
        const char* sliceMoves[6] = {"Uw'", "Uw", "Uw2", "Dw", "Dw'", "Dw2"};
        const char* sliceInv[6]   = {"Uw", "Uw'", "Uw2", "Dw'", "Dw", "Dw2"};
        // 48 verified 5-move L2E triggers: Slice + 3-move commutator/insertion + Slice' (cost 5!)
        const char* flips3[8] = {
            "R U R'", "R U' R'", "R U2 R'",
            "L' U L", "L' U' L", "L' U2 L",
            "F' U F", "B U B'"
        };
        for (int f = 0; f < 8; ++f) {
            for (int s = 0; s < 6; ++s) {
                triggers.push_back(toIdx(std::string(sliceMoves[s]) + " " + flips3[f] + " " + sliceInv[s]));
            }
        }
        // All 4 corner-slot 7-move edge flippers: slice + flip + slice' = 7 moves preserving ALL centers & U/D edges!
        const char* flips5[4] = {
            "R F' U R' F",
            "L' F U' L F'",
            "R' B U' R B'",
            "L B' U L' B"
        };
        for (int f = 0; f < 4; ++f) {
            for (int s = 0; s < 6; ++s) {
                triggers.push_back(toIdx(std::string(sliceMoves[s]) + " " + flips5[f] + " " + sliceInv[s]));
            }
        }
        // Plus 7-move pure slice-flip triggers & classic 9-move triggers
        triggers.push_back(toIdx("Uw' R U R' F R' F' R Uw"));
        triggers.push_back(toIdx("Uw R U R' F R' F' R Uw'"));
        triggers.push_back(toIdx("Dw R U R' F R' F' R Dw'"));
        triggers.push_back(toIdx("Dw' R U R' F R' F' R Dw"));
    }
};

inline const std::vector<std::vector<int>>& safeL2ETriggers() {
    static const TriggerIndexList inst;
    return inst.triggers;
}

// Smart per-node macro set for Freeslice (uses single L/R/F/B outer moves whenever eqAligned!)
inline void gatherFreesliceMoves(bool eqAligned,
                                 const uint8_t unpairedMask[4], // axes L=0(ax2), R=1(ax3), F=2(ax4), B=3(ax5)
                                 std::vector<MacroSeq>& out) {
    out.clear();
    // 1. Always allow single U/D/Uw/Dw (12 moves, cost 1)
    for (int m : {0, 1, 2, 6, 7, 8, 3, 4, 5, 9, 10, 11}) {
        int ax = m / 6;
        out.push_back({{m}, ax, ax});
    }
    if (eqAligned) {
        // Equator centers solid -> ALL 12 outer L/R/F/B single moves preserve centers directly (cost 1!)
        for (int ax = 2; ax < 6; ++ax) {
            for (int p = 0; p < 3; ++p) out.push_back({{ax * 6 + p}, ax, ax});
        }
    } else {
        // Equator rotated off -> safe 3-move X U/D^k X' ONLY for axes touching an unpaired equator edge
        for (int ax = 2; ax < 6; ++ax) {
            if (!unpairedMask[ax - 2]) continue;
            int base = ax * 6;
            for (int ud : {0, 6}) {
                for (int up = 0; up < 3; ++up) {
                    out.push_back({{base + 0, ud + up, base + 2}, ax, ax});
                    out.push_back({{base + 2, ud + up, base + 0}, ax, ax});
                }
            }
        }
    }
    // 2. 6-move in-place equator flips when any equator edge is unpaired
    if (unpairedMask[0] || unpairedMask[1] || unpairedMask[2] || unpairedMask[3]) {
        out.push_back({{18, 2, 20, 26, 0, 24}, 3, 4});
        out.push_back({{26, 0, 24, 18, 2, 20}, 4, 3});
        out.push_back({{14, 0, 12, 24, 2, 26}, 2, 4});
        out.push_back({{24, 2, 26, 14, 0, 12}, 4, 2});
    }
}

// Align equator centers after freeslice (ONLY U/D centers intact, equator bars solid but shifted by Uw/Dw)
inline std::vector<Move> alignEquatorCenters4x4(Fast4x4& st) {
    const auto& cat = catalog();
    std::vector<Move> out;
    // Try all (uw in 0..3, dw in 0..3) combinations (16 total!) preserving U/D centers
    for (int uw = 0; uw < 4; ++uw) {
        for (int dw = 0; dw < 4; ++dw) {
            Fast4x4 tmp = st;
            std::vector<Move> mv;
            if (uw > 0) {
                int m = 3 + (uw - 1);
                tmp.applyMoveIdx(m);
                mv.push_back(cat.moves36[m]);
            }
            if (dw > 0) {
                int m = 9 + (dw - 1);
                tmp.applyMoveIdx(m);
                mv.push_back(cat.moves36[m]);
            }
            bool ok = true;
            for (int f = 0; f < 6 && ok; ++f)
                for (int k = 0; k < 4; ++k)
                    if (tmp.centers[f * 4 + k] != f) ok = false;
            if (ok) {
                st = tmp;
                return mv;
            }
        }
    }
    return out;
}

inline std::vector<Move> alignEquatorCenters5x5(Fast5x5& st) {
    const auto& cat = catalog();
    std::vector<Move> out;
    for (int uw = 0; uw < 4; ++uw) {
        for (int dw = 0; dw < 4; ++dw) {
            Fast5x5 tmp = st;
            std::vector<Move> mv;
            if (uw > 0) {
                int m = 3 + (uw - 1);
                tmp.applyMoveIdx(m);
                mv.push_back(cat.moves36[m]);
            }
            if (dw > 0) {
                int m = 9 + (dw - 1);
                tmp.applyMoveIdx(m);
                mv.push_back(cat.moves36[m]);
            }
            bool ok = true;
            for (int f = 0; f < 6 && ok; ++f)
                for (int k = 0; k < 4; ++k)
                    if (tmp.tCenters[f * 4 + k] != f || tmp.xCenters[f * 4 + k] != f) ok = false;
            if (ok) {
                st = tmp;
                return mv;
            }
        }
    }
    return out;
}

// Fast score for 4x4 Freeslice Beam Search
inline double scoreWings4x4(const Fast4x4& st, int moveCount, const HeuristicWeights& w) {
    int totalPaired = 0;
    int udParked = 0;
    for (int e = 0; e < 12; ++e) {
        if (st.wings[2 * e] == st.wings[2 * e + 1]) {
            totalPaired++;
            if (e < 8) udParked++;
        }
    }
    // Chain-slice opportunity: how many unpaired equator edges align simultaneously under a SINGLE Uw/Uw'/Uw2 or Dw/Dw'/Dw2 shift!
    int bestChain = 0;
    int setup1Away = 0;
    int badFlip = 0;
    static const int eqEdges[4] = {8, 9, 10, 11};
    bool unp[4];
    for (int a = 0; a < 4; ++a) unp[a] = (st.wings[2 * eqEdges[a]] != st.wings[2 * eqEdges[a] + 1]);
    for (int sh = 1; sh <= 3; ++sh) {
        int mTop = 0;
        for (int a = 0; a < 4; ++a) {
            if (!unp[a]) continue;
            int b = (a + sh) & 3;
            if (!unp[b]) continue;
            if (st.wings[2 * eqEdges[b]] == st.wings[2 * eqEdges[a] + 1]) mTop++;
            if (st.wings[2 * eqEdges[b]] == st.wings[2 * eqEdges[a]]) badFlip++;
        }
        if (mTop > bestChain) bestChain = mTop;
        setup1Away += mTop;
    }
    // Check monochromatic 4-shift equator alignment
    bool eqOk = (st.centers[8] == st.centers[9] && st.centers[8] == st.centers[10] &&
                 st.centers[16] == st.centers[17] && st.centers[16] == st.centers[18]);
    double eqPenalty = eqOk ? 0.0 : w.wEqMisaligned;

    return totalPaired * w.wPairedEdge + udParked * w.wUDParked
           + bestChain * (w.wSetup1Away * 2.8) + setup1Away * (w.wSetup1Away * 0.4)
           - badFlip * w.wBadFlip - eqPenalty - moveCount * w.wMoveCost;
}

// Fast score for 5x5 Freeslice Beam Search
inline double scoreTredges5x5(const Fast5x5& st, int moveCount, const HeuristicWeights& w) {
    int fullPaired = 0;
    int udParked = 0;
    int halfPaired = 0;
    for (int e = 0; e < 12; ++e) {
        bool w1 = (st.wings[2 * e] == st.midges[e]);
        bool w2 = (st.wings[2 * e + 1] == st.midges[e]);
        if (w1) halfPaired++;
        if (w2) halfPaired++;
        if (w1 && w2) {
            fullPaired++;
            if (e < 8) udParked++;
        }
    }
    int bestChain = 0;
    int setup1Away = 0;
    int badFlip = 0;
    static const int eqEdges[4] = {8, 9, 10, 11};
    for (int sh = 1; sh <= 3; ++sh) {
        int mTop = 0, mBot = 0;
        for (int a = 0; a < 4; ++a) {
            int e1 = eqEdges[a];
            int e2 = eqEdges[(a + sh) & 3];
            if (st.wings[2 * e2] == st.midges[e1] && st.wings[2 * e2] != st.wings[2 * e2 + 1]) mTop++;
            if (st.wings[2 * e2 + 1] == st.midges[e1] && st.wings[2 * e2 + 1] != st.wings[2 * e2]) mBot++;
            if (st.wings[2 * e1] == st.wings[2 * e2] && st.wings[2 * e1] != st.midges[e1] && st.wings[2 * e2] != st.midges[e2]) badFlip++;
        }
        if (mTop > bestChain) bestChain = mTop;
        if (mBot > bestChain) bestChain = mBot;
        setup1Away += mTop + mBot;
    }
    double eqPenalty = (st.tCenters[17] != 4 || st.tCenters[19] != 4) ? w.wEqMisaligned : 0.0;

    return fullPaired * w.wPairedEdge + halfPaired * w.wHalfPair5x5 + udParked * w.wUDParked
           + bestChain * (w.wSetup1Away * 2.5) + setup1Away * (w.wSetup1Away * 0.4)
           - badFlip * w.wBadFlip - eqPenalty - moveCount * w.wMoveCost;
}

// ─── 4x4 Beam Freeslice + Safe Edge Completion ────────────────────────────────

inline bool solveWings4x4Axis(State4x4& state, const HeuristicWeights& w, std::vector<Move>& outMoves) {
    const auto& cat = catalog();
    const auto& l2e = safeL2ETriggers();
    Fast4x4 startFast = Fast4x4::fromState(state);

    if (startFast.pairedEdgeCount() == 12) return true;

    struct Node4 {
        Fast4x4 st;
        int nodeId;
        int16_t lastMove;
        int16_t pathLen;
        double score;
        int paired;
        int udParked;
        int eqAligned;
    };

    std::vector<ArenaStep> arena;
    arena.reserve(49152);
    auto extractPath = [&](int nodeId) {
        std::vector<int> rev;
        for (int p = nodeId; p >= 0; p = arena[p].parent) rev.push_back(arena[p].m);
        std::reverse(rev.begin(), rev.end());
        return rev;
    };

    auto evalNode = [&](const Fast4x4& f, int nodeId, int16_t lm, int16_t plen) -> Node4 {
        int pc = 0, udp = 0;
        for (int e = 0; e < 12; ++e) {
            if (f.wings[2 * e] == f.wings[2 * e + 1]) { pc++; if (e < 8) udp++; }
        }
        bool eqOk = (f.centers[8] == f.centers[9] && f.centers[8] == f.centers[10] &&
                     f.centers[16] == f.centers[17] && f.centers[16] == f.centers[18]);
        return Node4{f, nodeId, lm, plen, scoreWings4x4(f, plen, w), pc, udp, eqOk ? 1 : 0};
    };

    auto finishEst4 = [](const Node4& n) -> double {
        double alignCost = n.eqAligned ? 0.0 : 1.2;
        if (n.paired == 12) return (double)n.pathLen + alignCost;
        return (double)n.pathLen + alignCost + (12 - n.paired) * 5.0;
    };
    auto betterBest4 = [&](const Node4& cand, const Node4& curBest) -> bool {
        if (cand.paired >= 10 && curBest.paired >= 10) {
            return finishEst4(cand) < finishEst4(curBest);
        }
        if (cand.paired != curBest.paired) return cand.paired > curBest.paired;
        return cand.score > curBest.score;
    };

    Node4 bestNode = evalNode(startFast, -1, -1, 0);

    auto hashWings = [](const Fast4x4& f) -> uint64_t {
        uint64_t h = 14695981039346656037ULL;
        for (int i = 0; i < 24; ++i) { h ^= f.wings[i]; h *= 1099511628211ULL; }
        h ^= (static_cast<uint64_t>(f.centers[8]) << 12) | (static_cast<uint64_t>(f.centers[10]) << 8)
           | (static_cast<uint64_t>(f.centers[16]) << 4) | f.centers[18];
        return h;
    };

    std::unordered_set<uint64_t> visited;
    visited.reserve(32768);
    visited.insert(hashWings(startFast));

    const int MAX_G4 = 34;
    int bw = std::max(32, std::min(256, w.beamWidthFreeslice));
    std::vector<std::vector<Node4>> buckets(MAX_G4 + 8);
    buckets[0].push_back(bestNode);
    std::vector<MacroSeq> dynMacros;

    for (int g = 0; g <= MAX_G4; ++g) {
        auto& beam = buckets[g];
        if (beam.empty()) continue;

        if (bestNode.paired == 12 && bestNode.eqAligned && g >= bestNode.pathLen) break;
        if (bestNode.paired >= 10 && g >= (int)std::ceil(finishEst4(bestNode)) + 1) break;

        if ((int)beam.size() > bw) {
            std::nth_element(beam.begin(), beam.begin() + bw, beam.end(), [](const Node4& a, const Node4& b) {
                return a.score > b.score;
            });
            beam.resize(bw);
        }

        for (const auto& cur : beam) {
            uint8_t ep[12];
            for (int e = 0; e < 12; ++e) ep[e] = (cur.st.wings[2 * e] == cur.st.wings[2 * e + 1]) ? 1 : 0;
            uint8_t uMask[4] = {
                uint8_t(!ep[2] || !ep[6] || !ep[9]  || !ep[10]), // L
                uint8_t(!ep[0] || !ep[4] || !ep[8]  || !ep[11]), // R
                uint8_t(!ep[1] || !ep[5] || !ep[8]  || !ep[9]),  // F
                uint8_t(!ep[3] || !ep[7] || !ep[10] || !ep[11])  // B
            };
            gatherFreesliceMoves(cur.eqAligned != 0, uMask, dynMacros);

            for (const auto& mac : dynMacros) {
                int ng = g + (int)mac.moveIdx.size();
                if (ng > MAX_G4) continue;
                if (cur.lastMove >= 0 && mac.moveIdx.size() == 1) {
                    const Move& pm = cat.moves36[cur.lastMove];
                    const Move& cm = cat.moves36[mac.moveIdx[0]];
                    if (pm.axis == cm.axis && pm.sliceMask == cm.sliceMask) continue;
                }
                Fast4x4 nx = cur.st;
                for (int m : mac.moveIdx) nx.applyMoveIdx(m);
                uint64_t h = hashWings(nx);
                if (!visited.insert(h).second) continue;

                int nid = cur.nodeId;
                for (int m : mac.moveIdx) {
                    arena.push_back({nid, static_cast<int16_t>(m)});
                    nid = static_cast<int>(arena.size()) - 1;
                }
                Node4 cand = evalNode(nx, nid, static_cast<int16_t>(mac.moveIdx.back()), static_cast<int16_t>(ng));
                if (betterBest4(cand, bestNode)) {
                    bestNode = cand;
                }
                buckets[ng].push_back(cand);
            }
        }
        std::vector<Node4>().swap(buckets[g]);
    }

    // Apply best W1 trajectory & align equator centers in Fast4x4
    Fast4x4 fState = startFast;
    std::vector<int> wIdx = extractPath(bestNode.nodeId);
    for (int m : wIdx) fState.applyMoveIdx(m);
    for (const auto& mv : alignEquatorCenters4x4(fState)) {
        for (int i = 0; i < 36; ++i) {
            if (cat.moves36[i].axis == mv.axis && cat.moves36[i].sliceMask == mv.sliceMask && cat.moves36[i].power == mv.power) {
                wIdx.push_back(i);
                break;
            }
        }
    }

    // Phase W2: Blazing-fast L4E/L2E Safe Finishing on Fast4x4 (prefers 12/12 in 1 trigger!)
    for (int guard = 0; guard < 18 && fState.pairedEdgeCount() < 12; ++guard) {
        int curP = fState.pairedEdgeCount();

        auto tryTrigs4 = [&](const Fast4x4& st, std::vector<int>& bestT, int& outBestP, size_t maxT) -> bool {
            int bestP = curP;
            int bestLen = 999;
            size_t lim = std::min(maxT, l2e.size());
            for (size_t ti = 0; ti < lim; ++ti) {
                const auto& tr = l2e[ti];
                Fast4x4 test = st;
                for (int m : tr) test.applyMoveIdx(m);
                int p = test.pairedEdgeCount();
                if (p > bestP || (p == bestP && p > curP && (int)tr.size() < bestLen)) {
                    bestP = p;
                    bestLen = (int)tr.size();
                    bestT = tr;
                    if (bestP == 12 && bestLen == 5) break;
                }
            }
            outBestP = bestP;
            return bestP > curP;
        };

        std::vector<int> chosenT;
        int bestDirectP = curP;
        if (tryTrigs4(fState, chosenT, bestDirectP, l2e.size()) && bestDirectP == 12) {
            for (int m : chosenT) { fState.applyMoveIdx(m); wIdx.push_back(m); }
            continue;
        }

        // Fast4x4 outer BFS (depth <= 3, capped for < 5ms): prefer FULL 12/12 finish over partial!
        struct FNode4 { Fast4x4 s; std::vector<int> p; };
        std::queue<FNode4> q;
        q.push({fState, {}});
        std::unordered_set<uint64_t> vis;
        auto hashW4 = [](const Fast4x4& f) -> uint64_t {
            uint64_t h = 14695981039346656037ULL;
            for (int i = 0; i < 24; ++i) { h ^= f.wings[i]; h *= 1099511628211ULL; }
            return h;
        };
        vis.insert(hashW4(fState));

        std::vector<int> bestPartSetup, bestPartTrig;
        int bestPartCost = 999;
        if (bestDirectP > curP) {
            bestPartTrig = chosenT;
            bestPartCost = (int)chosenT.size();
        }
        std::vector<int> bestFullSetup, bestFullTrig;
        int bestFullCost = 999;

        bool stopBFS = false;
        for (int iter = 0; !q.empty() && iter < 1200 && !stopBFS; ++iter) {
            FNode4 cur = std::move(q.front()); q.pop();
            if (cur.p.size() >= 3) continue;
            if (bestFullCost < 999 && (int)cur.p.size() >= bestFullCost - 5) break;
            if (bestPartCost < 999 && cur.p.size() >= 2 && bestFullCost == 999) break;
            if (bestFullCost < 999 && cur.p.size() >= 2) break;

            size_t maxT = (cur.p.size() <= 1) ? l2e.size() : size_t(32);
            for (int ax = 0; ax < 6; ++ax) {
                if (!cur.p.empty() && (cur.p.back() / 6) == ax) continue;
                for (int pw = 0; pw < 3; ++pw) {
                    int m = ax * 6 + pw;
                    Fast4x4 ns = cur.s;
                    ns.applyMoveIdx(m);
                    if (!vis.insert(hashW4(ns)).second) continue;

                    auto np = cur.p; np.push_back(m);
                    std::vector<int> tr;
                    int bp = curP;
                    if (tryTrigs4(ns, tr, bp, maxT)) {
                        int totC = (int)(np.size() + tr.size());
                        if (bp == 12 && totC < bestFullCost) {
                            bestFullCost = totC;
                            bestFullSetup = np;
                            bestFullTrig = tr;
                            if (np.size() <= 1 && tr.size() <= 7) { stopBFS = true; break; }
                        } else if (bp > curP && totC < bestPartCost) {
                            bestPartCost = totC;
                            bestPartSetup = np;
                            bestPartTrig = tr;
                        }
                    }
                    if (np.size() < 3) q.push({ns, std::move(np)});
                }
                if (stopBFS) break;
            }
        }

        if (bestFullCost < 999) {
            for (int m : bestFullSetup) { fState.applyMoveIdx(m); wIdx.push_back(m); }
            for (int m : bestFullTrig) { fState.applyMoveIdx(m); wIdx.push_back(m); }
        } else if (bestPartCost < 999) {
            for (int m : bestPartSetup) { fState.applyMoveIdx(m); wIdx.push_back(m); }
            for (int m : bestPartTrig) { fState.applyMoveIdx(m); wIdx.push_back(m); }
        } else {
            return false;
        }
    }

    std::vector<Move> wMoves;
    wMoves.reserve(wIdx.size());
    for (int m : wIdx) wMoves.push_back(cat.moves36[m]);
    auto opt = MoveOptimizer::optimize(wMoves, 4);
    for (const auto& mv : opt) state.applyMove(mv);
    outMoves.insert(outMoves.end(), opt.begin(), opt.end());
    return state.areCentersSolved() && state.areWingsPaired();
}

inline bool solveWings4x4V2(State4x4& state, const HeuristicWeights& w, std::vector<Move>& outMoves, bool allAxes = true) {
    if (state.areWingsPaired()) return true;
    const auto& sym = cubeSym();
    bool found = false;
    std::vector<Move> bestOpt;
    int maxSym = allAxes ? 3 : 1;
    for (int s = 0; s < maxSym; ++s) {
        State4x4 rs = sym.rotateState4(state, s);
        std::vector<Move> cand;
        if (!solveWings4x4Axis(rs, w, cand)) continue;
        std::vector<Move> origCand = sym.unmapMoves(std::move(cand), s);
        if (!found || origCand.size() < bestOpt.size()) {
            State4x4 chk = state;
            for (const auto& mv : origCand) chk.applyMove(mv);
            if (chk.areCentersSolved() && chk.areWingsPaired()) {
                found = true;
                bestOpt = std::move(origCand);
            }
        }
    }
    if (!found) return false;
    for (const auto& mv : bestOpt) state.applyMove(mv);
    outMoves.insert(outMoves.end(), bestOpt.begin(), bestOpt.end());
    return true;
}

// ─── 5x5 Beam Freeslice + Safe Tredge Completion ──────────────────────────────

inline bool solveTredges5x5Axis(State5x5& state, const HeuristicWeights& w, std::vector<Move>& outMoves) {
    const auto& cat = catalog();
    const auto& l2e = safeL2ETriggers();
    Fast5x5 startFast = Fast5x5::fromState(state);

    if (startFast.pairedTredgeCount() == 12) return true;

    struct Node5 {
        Fast5x5 st;
        int nodeId;
        int16_t lastMove;
        int16_t pathLen;
        double score;
        int fullPaired;
        int halfPaired;
        int eqAligned;
    };

    std::vector<ArenaStep> arena;
    arena.reserve(65536);
    auto extractPath = [&](int nodeId) {
        std::vector<int> rev;
        for (int p = nodeId; p >= 0; p = arena[p].parent) rev.push_back(arena[p].m);
        std::reverse(rev.begin(), rev.end());
        return rev;
    };

    auto evalNode = [&](const Fast5x5& f, int nodeId, int16_t lm, int16_t plen) -> Node5 {
        bool eqOk = (f.tCenters[9] == 2 && f.tCenters[11] == 2 && f.tCenters[17] == 4 && f.tCenters[19] == 4 &&
                     f.xCenters[9] == 2 && f.xCenters[11] == 2 && f.xCenters[17] == 4 && f.xCenters[19] == 4);
        return Node5{f, nodeId, lm, plen, scoreTredges5x5(f, plen, w), f.pairedTredgeCount(), f.pairedWingCount(), eqOk ? 1 : 0};
    };

    auto finishEst5 = [](const Node5& n) -> double {
        double alignCost = n.eqAligned ? 0.0 : 1.2;
        if (n.fullPaired == 12) return (double)n.pathLen + alignCost;
        double p11 = (n.fullPaired == 11) ? 14.0 : 0.0;
        return (double)n.pathLen + alignCost + (24 - n.halfPaired) * 2.8 + (12 - n.fullPaired) * 4.5 + p11;
    };
    auto betterBest5 = [&](const Node5& cand, const Node5& curBest) -> bool {
        if (cand.fullPaired >= 9 && curBest.fullPaired >= 9) {
            return finishEst5(cand) < finishEst5(curBest);
        }
        if (cand.fullPaired != curBest.fullPaired) return cand.fullPaired > curBest.fullPaired;
        if (cand.halfPaired != curBest.halfPaired) return cand.halfPaired > curBest.halfPaired;
        return cand.score > curBest.score;
    };

    Node5 bestNode = evalNode(startFast, -1, -1, 0);

    auto hash5 = [](const Fast5x5& f) -> uint64_t {
        uint64_t h = 14695981039346656037ULL;
        for (int i = 0; i < 24; ++i) { h ^= f.wings[i]; h *= 1099511628211ULL; }
        for (int i = 0; i < 12; ++i) { h ^= f.midges[i]; h *= 1099511628211ULL; }
        h ^= (static_cast<uint64_t>(f.tCenters[9]) << 12) ^ (static_cast<uint64_t>(f.tCenters[17]) << 8) ^ (static_cast<uint64_t>(f.tCenters[19]) << 4);
        return h;
    };

    std::unordered_set<uint64_t> visited;
    visited.reserve(65536);
    visited.insert(hash5(startFast));

    const int MAX_G5 = 64;
    int bw = std::max(32, std::min(256, w.beamWidthFreeslice));
    std::vector<std::vector<Node5>> buckets(MAX_G5 + 8);
    buckets[0].push_back(bestNode);
    std::vector<MacroSeq> dynMacros;

    for (int g = 0; g <= MAX_G5; ++g) {
        auto& beam = buckets[g];
        if (beam.empty()) continue;

        if (bestNode.fullPaired == 12 && bestNode.eqAligned && g >= bestNode.pathLen) break;
        if (bestNode.fullPaired >= 10 && g >= (int)std::ceil(finishEst5(bestNode)) + 1) break;

        if ((int)beam.size() > bw) {
            std::nth_element(beam.begin(), beam.begin() + bw, beam.end(), [](const Node5& a, const Node5& b) {
                return a.score > b.score;
            });
            beam.resize(bw);
        }

        for (const auto& cur : beam) {
            uint8_t ep[12];
            for (int e = 0; e < 12; ++e) {
                ep[e] = (cur.st.wings[2 * e] == cur.st.midges[e] && cur.st.wings[2 * e + 1] == cur.st.midges[e]) ? 1 : 0;
            }
            uint8_t uMask[4] = {
                uint8_t(!ep[2] || !ep[6] || !ep[9]  || !ep[10]),
                uint8_t(!ep[0] || !ep[4] || !ep[8]  || !ep[11]),
                uint8_t(!ep[1] || !ep[5] || !ep[8]  || !ep[9]),
                uint8_t(!ep[3] || !ep[7] || !ep[10] || !ep[11])
            };
            gatherFreesliceMoves(cur.eqAligned != 0, uMask, dynMacros);

            for (const auto& mac : dynMacros) {
                int ng = g + (int)mac.moveIdx.size();
                if (ng > MAX_G5) continue;
                if (cur.lastMove >= 0 && mac.moveIdx.size() == 1) {
                    const Move& pm = cat.moves36[cur.lastMove];
                    const Move& cm = cat.moves36[mac.moveIdx[0]];
                    if (pm.axis == cm.axis && pm.sliceMask == cm.sliceMask) continue;
                }
                Fast5x5 nx = cur.st;
                for (int m : mac.moveIdx) nx.applyMoveIdx(m);
                uint64_t h = hash5(nx);
                if (!visited.insert(h).second) continue;

                int nid = cur.nodeId;
                for (int m : mac.moveIdx) {
                    arena.push_back({nid, static_cast<int16_t>(m)});
                    nid = static_cast<int>(arena.size()) - 1;
                }
                Node5 cand = evalNode(nx, nid, static_cast<int16_t>(mac.moveIdx.back()), static_cast<int16_t>(ng));
                if (betterBest5(cand, bestNode)) {
                    bestNode = cand;
                }
                buckets[ng].push_back(cand);
            }
        }
        std::vector<Node5>().swap(buckets[g]);
    }

    Fast5x5 fState = startFast;
    std::vector<int> eIdx = extractPath(bestNode.nodeId);
    for (int m : eIdx) fState.applyMoveIdx(m);
    for (const auto& mv : alignEquatorCenters5x5(fState)) {
        for (int i = 0; i < 36; ++i) {
            if (cat.moves36[i].axis == mv.axis && cat.moves36[i].sliceMask == mv.sliceMask && cat.moves36[i].power == mv.power) {
                eIdx.push_back(i);
                break;
            }
        }
    }

    // Build 5x5 single-tredge parity alg index vector once
    std::vector<int> parityIdx;
    for (const auto& mv : parseMoves("Rw2 B2 U2 Lw U2 Rw' U2 Rw U2 F2 Rw F2 Lw' B2 Rw2")) {
        for (int i = 0; i < 36; ++i) {
            if (cat.moves36[i].axis == mv.axis && cat.moves36[i].sliceMask == mv.sliceMask && cat.moves36[i].power == mv.power) {
                parityIdx.push_back(i);
                break;
            }
        }
    }

    auto scoreFast5 = [](const Fast5x5& f) -> int {
        return f.pairedTredgeCount() * 10 + f.pairedWingCount();
    };

    // Phase W2 on Fast5x5: blazing fast + full 12/12 finish preference!
    for (int guard = 0; guard < 28 && fState.pairedTredgeCount() < 12; ++guard) {
        int curScore = scoreFast5(fState);

        // Single-tredge flipped parity on Fast5x5 (when 11/12 paired)
        bool didParity = false;
        if (fState.pairedTredgeCount() == 11) {
            struct PNode { Fast5x5 s; std::vector<int> p; };
            std::queue<PNode> pq;
            pq.push({fState, {}});
            std::unordered_set<uint64_t> pvis;
            auto h5f = [](const Fast5x5& ff) -> uint64_t {
                uint64_t h = 14695981039346656037ULL;
                for (int i = 0; i < 24; ++i) { h ^= ff.wings[i]; h *= 1099511628211ULL; }
                for (int i = 0; i < 12; ++i) { h ^= ff.midges[i]; h *= 1099511628211ULL; }
                return h;
            };
            pvis.insert(h5f(fState));
            for (int iter = 0; !pq.empty() && iter < 8000 && !didParity; ++iter) {
                PNode cur = std::move(pq.front()); pq.pop();
                if (cur.s.wings[2] == (cur.s.midges[1] ^ 1) && cur.s.wings[3] == (cur.s.midges[1] ^ 1)) {
                    Fast5x5 test = cur.s;
                    for (int m : parityIdx) test.applyMoveIdx(m);
                    if (test.pairedTredgeCount() == 12) {
                        for (int m : cur.p) { fState.applyMoveIdx(m); eIdx.push_back(m); }
                        for (int m : parityIdx) { fState.applyMoveIdx(m); eIdx.push_back(m); }
                        didParity = true;
                        break;
                    }
                }
                if (cur.p.size() >= 4) continue;
                for (int ax = 0; ax < 6; ++ax) {
                    if (!cur.p.empty() && (cur.p.back() / 6) == ax) continue;
                    for (int pw = 0; pw < 3; ++pw) {
                        int m = ax * 6 + pw;
                        Fast5x5 ns = cur.s; ns.applyMoveIdx(m);
                        if (pvis.insert(h5f(ns)).second) {
                            auto np = cur.p; np.push_back(m);
                            pq.push({ns, std::move(np)});
                        }
                    }
                }
            }
        }
        if (didParity) break;

        auto tryTrigs5 = [&](const Fast5x5& st, std::vector<int>& bestT, int& outBestS, size_t maxT) -> bool {
            int bestS = curScore;
            int bestLen = 999;
            size_t lim = std::min(maxT, l2e.size());
            for (size_t ti = 0; ti < lim; ++ti) {
                const auto& tr = l2e[ti];
                Fast5x5 test = st;
                for (int m : tr) test.applyMoveIdx(m);
                int sc = scoreFast5(test);
                if (sc > bestS || (sc == bestS && sc > curScore && (int)tr.size() < bestLen)) {
                    bestS = sc;
                    bestLen = (int)tr.size();
                    bestT = tr;
                    if (bestS == 144 && bestLen == 5) break;
                }
            }
            outBestS = bestS;
            return bestS > curScore;
        };

        std::vector<int> chosenT;
        int bestDirectS = curScore;
        if (tryTrigs5(fState, chosenT, bestDirectS, l2e.size()) && bestDirectS == 144) {
            for (int m : chosenT) { fState.applyMoveIdx(m); eIdx.push_back(m); }
            continue;
        }

        struct FNode5 { Fast5x5 s; std::vector<int> p; };
        std::queue<FNode5> q;
        q.push({fState, {}});
        std::unordered_set<uint64_t> vis;
        auto h5f = [](const Fast5x5& ff) -> uint64_t {
            uint64_t h = 14695981039346656037ULL;
            for (int i = 0; i < 24; ++i) { h ^= ff.wings[i]; h *= 1099511628211ULL; }
            for (int i = 0; i < 12; ++i) { h ^= ff.midges[i]; h *= 1099511628211ULL; }
            return h;
        };
        vis.insert(h5f(fState));

        std::vector<int> bestPartSetup, bestPartTrig;
        int bestPartCost = 999;
        int bestPartGain = 0;
        if (bestDirectS > curScore) {
            bestPartTrig = chosenT;
            bestPartCost = (int)chosenT.size();
            bestPartGain = bestDirectS - curScore;
        }
        std::vector<int> bestFullSetup, bestFullTrig;
        int bestFullCost = 999;

        bool stopBFS = false;
        for (int iter = 0; !q.empty() && iter < 1200 && !stopBFS; ++iter) {
            FNode5 cur = std::move(q.front()); q.pop();
            if (cur.p.size() >= 3) continue;
            if (bestFullCost < 999 && (int)cur.p.size() >= bestFullCost - 5) break;
            if (bestPartCost < 999 && cur.p.size() >= 2 && bestFullCost == 999) break;
            if (bestFullCost < 999 && cur.p.size() >= 2) break;

            size_t maxT = (cur.p.size() <= 1) ? l2e.size() : size_t(32);
            for (int ax = 0; ax < 6; ++ax) {
                if (!cur.p.empty() && (cur.p.back() / 6) == ax) continue;
                for (int pw = 0; pw < 3; ++pw) {
                    int m = ax * 6 + pw;
                    Fast5x5 ns = cur.s; ns.applyMoveIdx(m);
                    if (!vis.insert(h5f(ns)).second) continue;

                    auto np = cur.p; np.push_back(m);
                    std::vector<int> tr;
                    int bs = curScore;
                    if (tryTrigs5(ns, tr, bs, maxT)) {
                        int totC = (int)(np.size() + tr.size());
                        int gain = bs - curScore;
                        if (bs == 144 && totC < bestFullCost) {
                            bestFullCost = totC;
                            bestFullSetup = np;
                            bestFullTrig = tr;
                            if (np.size() <= 1 && tr.size() <= 7) { stopBFS = true; break; }
                        } else if (gain * 1.0 / totC > (bestPartCost < 999 ? (double)bestPartGain / bestPartCost : 0.0) ||
                                   (bestPartCost == 999)) {
                            bestPartCost = totC;
                            bestPartGain = gain;
                            bestPartSetup = np;
                            bestPartTrig = tr;
                        }
                    }
                    if (np.size() < 3) q.push({ns, std::move(np)});
                }
                if (stopBFS) break;
            }
        }

        if (bestFullCost < 999) {
            for (int m : bestFullSetup) { fState.applyMoveIdx(m); eIdx.push_back(m); }
            for (int m : bestFullTrig) { fState.applyMoveIdx(m); eIdx.push_back(m); }
        } else if (bestPartCost < 999) {
            for (int m : bestPartSetup) { fState.applyMoveIdx(m); eIdx.push_back(m); }
            for (int m : bestPartTrig) { fState.applyMoveIdx(m); eIdx.push_back(m); }
        } else {
            return false;
        }
    }

    std::vector<Move> eMoves;
    eMoves.reserve(eIdx.size());
    for (int m : eIdx) eMoves.push_back(cat.moves36[m]);
    auto opt = MoveOptimizer::optimize(eMoves, 5);
    for (const auto& mv : opt) state.applyMove(mv);
    outMoves.insert(outMoves.end(), opt.begin(), opt.end());
    return state.areCentersSolved() && state.areTredgesPaired();
}

inline bool solveTredges5x5V2(State5x5& state, const HeuristicWeights& w, std::vector<Move>& outMoves, bool allAxes = true) {
    if (state.areTredgesPaired()) return true;
    const auto& sym = cubeSym();
    bool found = false;
    std::vector<Move> bestOpt;
    int maxSym = allAxes ? 3 : 1;
    for (int s = 0; s < maxSym; ++s) {
        State5x5 rs = sym.rotateState5(state, s);
        std::vector<Move> cand;
        if (!solveTredges5x5Axis(rs, w, cand)) continue;
        std::vector<Move> origCand = sym.unmapMoves(std::move(cand), s);
        if (!found || origCand.size() < bestOpt.size()) {
            State5x5 chk = state;
            for (const auto& mv : origCand) chk.applyMove(mv);
            if (chk.areCentersSolved() && chk.areTredgesPaired()) {
                found = true;
                bestOpt = std::move(origCand);
            }
        }
    }
    if (!found) return false;
    for (const auto& mv : bestOpt) state.applyMove(mv);
    outMoves.insert(outMoves.end(), bestOpt.begin(), bestOpt.end());
    return true;
}

// ─── Full 4x4 & 5x5 Solve Pipelines Using Trained Tables & Weights ────────────

inline SolveResult solve4x4WithWeights(const State4x4& state, const HeuristicWeights& w, bool allowShallow = true) {
    auto t0 = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;

    if (!result.isValid) {
        result.errorMessage = "Invalid 4x4 cube colors.";
        return result;
    }
    if (state.isSolved()) {
        result.solveTimeMs = 0.0;
        return result;
    }

    // Shallow optimal check (depth <= 4) for near-solved cubes
    if (allowShallow) {
        static const uint8_t masks[2] = {0x01, 0x03};
        std::vector<Move> sPath;
        std::function<bool(const State4x4&, int, int, int)> sdfs = [&](const State4x4& s, int d, int la, int lm) -> bool {
            if (s.isSolved()) return true;
            if (d == 0) return false;
            for (uint8_t ax = 0; ax < 6; ++ax) {
                for (uint8_t sl : masks) {
                    if (ax == la && sl == lm) continue;
                    for (uint8_t pw = 1; pw <= 3; ++pw) {
                        State4x4 ns = s;
                        Move m{ax, sl, pw};
                        ns.applyMove(m);
                        sPath.push_back(m);
                        if (sdfs(ns, d - 1, ax, sl)) return true;
                        sPath.pop_back();
                    }
                }
            }
            return false;
        };
        for (int d = 1; d <= 4; ++d) {
            sPath.clear();
            if (sdfs(state, d, -1, -1)) {
                PhaseStep ps;
                ps.phaseName = "4x4 Direct Optimal";
                ps.moves = sPath;
                for (size_t i = 0; i < sPath.size(); ++i) {
                    if (i > 0) ps.notationString += " ";
                    ps.notationString += moveToString(sPath[i], 4);
                }
                result.phases.push_back(ps);
                result.totalMoves = static_cast<int>(sPath.size());
                auto t1 = std::chrono::high_resolution_clock::now();
                result.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
                return result;
            }
        }
    }

    // Generate top-K shortest center reduction paths and pick best full solve
    Fast4x4 fStart = Fast4x4::fromState(state);
    auto centerPaths = solveCenters4x4Multi(fStart, std::max(1, std::min(8, w.centerTopK)));
    if (centerPaths.empty()) {
        result.isValid = false;
        result.errorMessage = "4x4 center reduction failed.";
        return result;
    }

    // Evaluate each center path (keep shortest overall totalMoves!)
    bool hasAny = false;
    int bestTot = 999999;
    std::vector<PhaseStep> bestPhases;

    int evalLimit = std::min<int>((int)centerPaths.size(), 3);
    for (int ci = 0; ci < evalLimit; ++ci) {
        State4x4 s = state;
        std::vector<Move> cMoves = centerPaths[ci];
        for (const auto& m : cMoves) s.applyMove(m);

        std::vector<Move> wMoves;
        if (!solveWings4x4V2(s, w, wMoves, ci == 0)) continue;

        // OLL + PLL Parity + 3x3 Stage (test verified pure parity variants & pick shortest!)
        bool hasOLL = s.hasOLLParity();
        bool hasPLL = s.hasPLLParity();
        static const char* const kOllAlgs[2] = {
            "2R2 B2 U2 2L U2 2R' U2 2R U2 F2 2R F2 2L' B2 2R2",
            "2R' U2 2L F2 2L' F2 2R2 U2 2R U2 2R' U2 F2 2R2 F2"
        };
        static const char* const kPllAlgs[2] = {
            "2R2 U2 2R2 Uw2 2R2 2U2",
            "Uw2 Rw2 U2 2R2 U2 Rw2 Uw2"
        };
        std::vector<std::vector<Move>> ollOpts, pllOpts;
        if (hasOLL) {
            ollOpts.push_back(parseMoves(kOllAlgs[0]));
            ollOpts.push_back(parseMoves(kOllAlgs[1]));
            if (!hasPLL) {
                ollOpts.push_back(parseMoves(std::string("U ") + kOllAlgs[0]));
                ollOpts.push_back(parseMoves(std::string("U2 ") + kOllAlgs[0]));
                ollOpts.push_back(parseMoves(std::string("U' ") + kOllAlgs[0]));
            }
        } else {
            ollOpts.push_back({});
        }
        if (hasPLL) {
            pllOpts.push_back(parseMoves(kPllAlgs[0]));
            if (!hasOLL) pllOpts.push_back(parseMoves(kPllAlgs[1]));
            if (!hasOLL) pllOpts.push_back(parseMoves(std::string("U ") + kPllAlgs[0]));
        } else {
            pllOpts.push_back({});
        }

        std::vector<Move> parityMoves, m3x3;
        int bestTailLen = 999999;
        for (const auto& om : ollOpts) {
            State4x4 so = s;
            for (const auto& m : om) so.applyMove(m);
            for (const auto& pm : pllOpts) {
                State4x4 sp = so;
                for (const auto& m : pm) sp.applyMove(m);
                State3x3 s3 = sp.toReduced3x3();
                std::vector<Move> r3Moves;
                if (!s3.isSolved()) {
                    SolveResult r3 = Solver3x3_Min2Phase::solveState(s3);
                    if (!r3.isValid) continue;
                    for (const auto& ph : r3.phases) {
                        r3Moves.insert(r3Moves.end(), ph.moves.begin(), ph.moves.end());
                    }
                }
                std::vector<Move> pCand = om;
                pCand.insert(pCand.end(), pm.begin(), pm.end());
                std::vector<Move> tail = cMoves;
                tail.insert(tail.end(), wMoves.begin(), wMoves.end());
                tail.insert(tail.end(), pCand.begin(), pCand.end());
                tail.insert(tail.end(), r3Moves.begin(), r3Moves.end());
                tail = MoveOptimizer::optimize(tail, 4);
                if ((int)tail.size() < bestTailLen) {
                    bestTailLen = (int)tail.size();
                    parityMoves = std::move(pCand);
                    m3x3 = MoveOptimizer::optimize(r3Moves, 4);
                }
            }
        }
        if (bestTailLen >= 999999) continue;

        // Merge & optimize cross-boundary adjacent moves while keeping phase breakdown clean!
        // Combine parity + 3x3 boundary & global count estimate
        std::vector<Move> allCombined;
        allCombined.insert(allCombined.end(), cMoves.begin(), cMoves.end());
        allCombined.insert(allCombined.end(), wMoves.begin(), wMoves.end());
        allCombined.insert(allCombined.end(), parityMoves.begin(), parityMoves.end());
        allCombined.insert(allCombined.end(), m3x3.begin(), m3x3.end());
        allCombined = MoveOptimizer::optimize(allCombined, 4);

        // Double check algebraic equivalence!
        State4x4 chk = state;
        for (const auto& m : allCombined) chk.applyMove(m);
        if (!chk.isSolved()) continue;

        int tot = static_cast<int>(allCombined.size());
        if (tot < bestTot) {
            bestTot = tot;
            hasAny = true;
            bestPhases.clear();
            auto pushPhase = [&](const std::string& name, const std::vector<Move>& mvs) {
                if (mvs.empty()) return;
                PhaseStep ps;
                ps.phaseName = name;
                ps.moves = mvs;
                for (size_t i = 0; i < mvs.size(); ++i) {
                    if (i > 0) ps.notationString += " ";
                    ps.notationString += moveToString(mvs[i], 4);
                }
                bestPhases.push_back(std::move(ps));
            };
            // Keep unified optimized sequence or cleanly cancel boundary pairs!
            // If cross-boundary optimization saved moves, emit phase-aligned optimized moves!
            int unoptSum = (int)(cMoves.size() + wMoves.size() + parityMoves.size() + m3x3.size());
            if (tot < unoptSum) {
                // Cancel boundary pairs cleanly between c->w->parity->3x3
                auto trimBoundary = [](std::vector<Move>& a, std::vector<Move>& b) {
                    while (!a.empty() && !b.empty() && a.back().axis == b.front().axis &&
                           a.back().sliceMask == b.front().sliceMask) {
                        uint8_t p = (a.back().power + b.front().power) & 3;
                        a.pop_back();
                        b.erase(b.begin());
                        if (p != 0) {
                            a.push_back(Move{b.empty() ? a.back().axis : b.front().axis, 0, 0}); // fallback below
                        }
                    }
                };
                (void)trimBoundary;
                // Emit 2 clear phases (Reduction + 3x3 Finish) or Full Optimized Sequence so move count strictly equals `tot`!
                pushPhase("4x4 Optimized Reduction + 3x3 Finish", allCombined);
            } else {
                pushPhase("4x4 Centers", cMoves);
                pushPhase("4x4 Wing Pairing", wMoves);
                pushPhase("4x4 Parity", parityMoves);
                pushPhase("3x3 Stage", m3x3);
            }
        }
    }

    if (!hasAny) {
        result.isValid = false;
        result.errorMessage = "4x4 V2 solve failed.";
        return result;
    }

    result.phases = std::move(bestPhases);
    result.totalMoves = bestTot;
    auto t1 = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

inline SolveResult solve5x5WithWeights(const State5x5& state, const HeuristicWeights& w, bool allowShallow = true) {
    auto t0 = std::chrono::high_resolution_clock::now();
    SolveResult result;
    result.isValid = state.isValid();
    result.totalMoves = 0;

    if (!result.isValid) {
        result.errorMessage = "Invalid 5x5 cube colors.";
        return result;
    }
    if (state.isSolved()) {
        result.solveTimeMs = 0.0;
        return result;
    }

    if (allowShallow) {
        static const uint8_t masks[2] = {0x01, 0x03};
        std::vector<Move> sPath;
        std::function<bool(const State5x5&, int, int, int)> sdfs = [&](const State5x5& s, int d, int la, int lm) -> bool {
            if (s.isSolved()) return true;
            if (d == 0) return false;
            for (uint8_t ax = 0; ax < 6; ++ax) {
                for (uint8_t sl : masks) {
                    if (ax == la && sl == lm) continue;
                    for (uint8_t pw = 1; pw <= 3; ++pw) {
                        State5x5 ns = s;
                        Move m{ax, sl, pw};
                        ns.applyMove(m);
                        sPath.push_back(m);
                        if (sdfs(ns, d - 1, ax, sl)) return true;
                        sPath.pop_back();
                    }
                }
            }
            return false;
        };
        for (int d = 1; d <= 3; ++d) {
            sPath.clear();
            if (sdfs(state, d, -1, -1)) {
                PhaseStep ps;
                ps.phaseName = "5x5 Direct Optimal";
                ps.moves = sPath;
                for (size_t i = 0; i < sPath.size(); ++i) {
                    if (i > 0) ps.notationString += " ";
                    ps.notationString += moveToString(sPath[i], 5);
                }
                result.phases.push_back(ps);
                result.totalMoves = static_cast<int>(sPath.size());
                auto t1 = std::chrono::high_resolution_clock::now();
                result.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
                return result;
            }
        }
    }

    Fast5x5 fStart = Fast5x5::fromState(state);
    auto centerPaths = solveCenters5x5Multi(fStart, std::max(1, std::min(6, w.centerTopK)));
    if (centerPaths.empty()) {
        result.isValid = false;
        result.errorMessage = "5x5 center reduction failed.";
        return result;
    }

    bool hasAny = false;
    int bestTot = 999999;
    std::vector<PhaseStep> bestPhases;

    int evalLimit = std::min<int>((int)centerPaths.size(), 2);
    for (int ci = 0; ci < evalLimit; ++ci) {
        State5x5 s = state;
        std::vector<Move> cMoves = centerPaths[ci];
        for (const auto& m : cMoves) s.applyMove(m);

        std::vector<Move> eMoves;
        if (!solveTredges5x5V2(s, w, eMoves, ci == 0)) continue;

        std::vector<Move> m3x3;
        State3x3 s3 = s.toReduced3x3();
        if (!s3.isSolved()) {
            SolveResult r3 = Solver3x3_Min2Phase::solveState(s3);
            if (!r3.isValid) continue;
            for (const auto& ph : r3.phases) {
                m3x3.insert(m3x3.end(), ph.moves.begin(), ph.moves.end());
            }
        }
        m3x3 = MoveOptimizer::optimize(m3x3, 5);

        std::vector<Move> allCombined;
        allCombined.insert(allCombined.end(), cMoves.begin(), cMoves.end());
        allCombined.insert(allCombined.end(), eMoves.begin(), eMoves.end());
        allCombined.insert(allCombined.end(), m3x3.begin(), m3x3.end());
        allCombined = MoveOptimizer::optimize(allCombined, 5);

        State5x5 chk = state;
        for (const auto& m : allCombined) chk.applyMove(m);
        if (!chk.isSolved()) continue;

        int tot = static_cast<int>(allCombined.size());
        if (tot < bestTot) {
            bestTot = tot;
            hasAny = true;
            bestPhases.clear();
            auto pushPhase = [&](const std::string& name, const std::vector<Move>& mvs) {
                if (mvs.empty()) return;
                PhaseStep ps;
                ps.phaseName = name;
                ps.moves = mvs;
                for (size_t i = 0; i < mvs.size(); ++i) {
                    if (i > 0) ps.notationString += " ";
                    ps.notationString += moveToString(mvs[i], 5);
                }
                bestPhases.push_back(std::move(ps));
            };
            int unoptSum = (int)(cMoves.size() + eMoves.size() + m3x3.size());
            if (tot < unoptSum) {
                pushPhase("5x5 Optimized Reduction + 3x3 Finish", allCombined);
            } else {
                pushPhase("5x5 Joint T+X Centers", cMoves);
                pushPhase("5x5 Freeslice Tredge Pairing", eMoves);
                pushPhase("3x3 Stage", m3x3);
            }
        }
    }

    if (!hasAny) {
        result.isValid = false;
        result.errorMessage = "5x5 V2 solve failed.";
        return result;
    }

    result.phases = std::move(bestPhases);
    result.totalMoves = bestTot;
    auto t1 = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

} // namespace TrainedSolver
