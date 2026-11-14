#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <cstring>
#include "Common/MoveNotation.hpp"
#include "Cube4x4/State4x4.hpp"
#include "Cube5x5/State5x5.hpp"

namespace FastCube {

// ─── Combinatorial Ranking Utilities ──────────────────────────────────────────

struct CombTables {
    uint32_t C[25][25];
    CombTables() {
        std::memset(C, 0, sizeof(C));
        for (int n = 0; n <= 24; ++n) {
            C[n][0] = 1;
            for (int k = 1; k <= n; ++k) {
                C[n][k] = C[n - 1][k - 1] + C[n - 1][k];
            }
        }
    }
};

inline const CombTables& comb() {
    static const CombTables inst;
    return inst;
}

template <int N, int K>
inline uint32_t rankComb(const uint8_t mask[N]) {
    const auto& C = comb().C;
    uint32_t rank = 0;
    int r = K;
    for (int i = N - 1; i >= 0 && r > 0; --i) {
        if (mask[i]) {
            rank += C[i][r];
            --r;
        }
    }
    return rank;
}

template <int N, int K>
inline void unrankComb(uint32_t rank, uint8_t outMask[N]) {
    const auto& C = comb().C;
    std::memset(outMask, 0, N);
    int r = K;
    for (int i = N - 1; i >= 0 && r > 0; --i) {
        if (rank >= C[i][r]) {
            outMask[i] = 1;
            rank -= C[i][r];
            --r;
        }
    }
}

// 4-bit Nibble Packing Helpers (2 entries per byte)
inline uint8_t getNibble(const uint8_t* tbl, uint32_t idx) {
    uint8_t b = tbl[idx >> 1];
    return (idx & 1) ? (b >> 4) : (b & 0x0F);
}

inline void setNibble(uint8_t* tbl, uint32_t idx, uint8_t val) {
    val &= 0x0F;
    if (idx & 1) {
        tbl[idx >> 1] = static_cast<uint8_t>((tbl[idx >> 1] & 0x0F) | (val << 4));
    } else {
        tbl[idx >> 1] = static_cast<uint8_t>((tbl[idx >> 1] & 0xF0) | val);
    }
}

// ─── Canonical Edge Color Pair Encoding (0..23) ───────────────────────────────
// Canonical 12 unordered face color pairs corresponding to:
// 0:UR(0,3) 1:UF(0,4) 2:UL(0,2) 3:UB(0,5)
// 4:DR(1,3) 5:DF(1,4) 6:DL(1,2) 7:DB(1,5)
// 8:FR(4,3) 9:FL(4,2) 10:BL(5,2) 11:BR(5,3)
inline int8_t encodeColorPair(uint8_t c1, uint8_t c2) {
    static const int8_t map[6][6] = {
        // 0:U   1:D   2:L   3:R   4:F   5:B
        { -1,   -1,    4,    0,    2,    6 }, // 0:U -> L(4), R(0), F(2), B(6)
        { -1,   -1,   12,    8,   10,   14 }, // 1:D -> L(12), R(8), F(10), B(14)
        {  5,   13,   -1,   -1,   19,   21 }, // 2:L -> U(5), D(13), F(19), B(21)
        {  1,    9,   -1,   -1,   17,   23 }, // 3:R -> U(1), D(9), F(17), B(23)
        {  3,   11,   18,   16,   -1,   -1 }, // 4:F -> U(3), D(11), L(18), R(16)
        {  7,   15,   20,   22,   -1,   -1 }  // 5:B -> U(7), D(15), L(20), R(22)
    };
    if (c1 > 5 || c2 > 5) return -1;
    return map[c1][c2];
}

// Move catalog: 36 standard cube moves {axis 0..5} x {outer 0x01, wide2 0x03} x {pow 1..3}
// Plus optional 18 inner-slice moves (2U..2B) = index 36..53 for 4x4/5x5 parity/setup if needed.
struct MoveCatalog {
    static constexpr int kNumMoves36 = 36;
    Move moves36[36];

    // 4x4 slot facelet indices
    int c4Facelet[24];
    int w4Facelet[24][2];
    // Permutation: next[i] = cur[perm[m][i]] (^ flip[m][i] for edge pieces)
    uint8_t c4Perm[36][24];
    uint8_t w4Perm[36][24];
    uint8_t w4Flip[36][24];

    // 5x5 slot facelet indices
    int t5Facelet[24];
    int x5Facelet[24];
    int m5Facelet[12][2];
    int w5Facelet[24][2];
    uint8_t t5Perm[36][24];
    uint8_t x5Perm[36][24];
    uint8_t w5Perm[36][24];
    uint8_t w5Flip[36][24];
    uint8_t m5Perm[36][12];
    uint8_t m5Flip[36][12];

    MoveCatalog() {
        int idx = 0;
        for (uint8_t ax = 0; ax < 6; ++ax) {
            for (uint8_t sl : {uint8_t(0x01), uint8_t(0x03)}) {
                for (uint8_t pw = 1; pw <= 3; ++pw) {
                    moves36[idx++] = Move{ax, sl, pw};
                }
            }
        }

        // 4x4 centers & wings (matching State4x4.cpp exactly)
        const int c4Local[4] = {5, 6, 9, 10};
        for (int f = 0; f < 6; ++f) {
            for (int k = 0; k < 4; ++k) c4Facelet[f * 4 + k] = f * 16 + c4Local[k];
        }
        const int w4Init[12][4] = {
            {0*16+7,  3*16+2,  0*16+11, 3*16+1},  // 0: UR (U7-R2, U11-R1)
            {0*16+13, 4*16+1,  0*16+14, 4*16+2},  // 1: UF
            {0*16+4,  2*16+1,  0*16+8,  2*16+2},  // 2: UL (U4-L1, U8-L2)
            {0*16+1,  5*16+2,  0*16+2,  5*16+1},  // 3: UB
            {1*16+7,  3*16+13, 1*16+11, 3*16+14}, // 4: DR (D7-R13, D11-R14)
            {1*16+1,  4*16+13, 1*16+2,  4*16+14}, // 5: DF
            {1*16+4,  2*16+14, 1*16+8,  2*16+13}, // 6: DL (D4-L14, D8-L13)
            {1*16+13, 5*16+14, 1*16+14, 5*16+13}, // 7: DB (D13-B14, D14-B13)
            {4*16+7,  3*16+4,  4*16+11, 3*16+8},  // 8: FR
            {4*16+4,  2*16+7,  4*16+8,  2*16+11}, // 9: FL
            {5*16+7,  2*16+4,  5*16+11, 2*16+8},  // 10: BL
            {5*16+4,  3*16+7,  5*16+8,  3*16+11}  // 11: BR
        };
        for (int e = 0; e < 12; ++e) {
            w4Facelet[2 * e][0] = w4Init[e][0];
            w4Facelet[2 * e][1] = w4Init[e][1];
            w4Facelet[2 * e + 1][0] = w4Init[e][2];
            w4Facelet[2 * e + 1][1] = w4Init[e][3];
        }

        // Derive 4x4 permutation tables directly from State4x4::applyMove
        for (int m = 0; m < 36; ++m) {
            State4x4 base;
            // Unique tag each of 96 facelets cleanly
            for (int f = 0; f < 96; ++f) base.facelets[f] = static_cast<uint8_t>(f);
            base.applyMove(moves36[m]);
            for (int i = 0; i < 24; ++i) {
                uint8_t srcC = base.facelets[c4Facelet[i]];
                for (int j = 0; j < 24; ++j) {
                    if (c4Facelet[j] == srcC) { c4Perm[m][i] = static_cast<uint8_t>(j); break; }
                }
                uint8_t srcW = base.facelets[w4Facelet[i][0]];
                for (int j = 0; j < 24; ++j) {
                    if (w4Facelet[j][0] == srcW) {
                        w4Perm[m][i] = static_cast<uint8_t>(j);
                        w4Flip[m][i] = 0;
                        break;
                    }
                    if (w4Facelet[j][1] == srcW) {
                        w4Perm[m][i] = static_cast<uint8_t>(j);
                        w4Flip[m][i] = 1;
                        break;
                    }
                }
            }
        }

        // 5x5 T-centers, X-centers, midges, wings (matching State5x5.cpp exactly)
        const int t5Local[4] = {7, 11, 13, 17};
        const int x5Local[4] = {6, 8, 16, 18};
        for (int f = 0; f < 6; ++f) {
            for (int k = 0; k < 4; ++k) {
                t5Facelet[f * 4 + k] = f * 25 + t5Local[k];
                x5Facelet[f * 4 + k] = f * 25 + x5Local[k];
            }
        }
        const int t5Init[12][6] = {
            {0*25+14, 3*25+2,  0*25+19, 3*25+1,  0*25+9,  3*25+3},  // UR
            {0*25+22, 4*25+2,  0*25+21, 4*25+1,  0*25+23, 4*25+3},  // UF
            {0*25+10, 2*25+2,  0*25+5,  2*25+1,  0*25+15, 2*25+3},  // UL
            {0*25+2,  5*25+2,  0*25+1,  5*25+3,  0*25+3,  5*25+1},  // UB
            {1*25+14, 3*25+22, 1*25+9,  3*25+21, 1*25+19, 3*25+23}, // DR
            {1*25+2,  4*25+22, 1*25+1,  4*25+21, 1*25+3,  4*25+23}, // DF
            {1*25+10, 2*25+22, 1*25+5,  2*25+23, 1*25+15, 2*25+21}, // DL
            {1*25+22, 5*25+22, 1*25+21, 5*25+23, 1*25+23, 5*25+21}, // DB
            {4*25+14, 3*25+10, 4*25+9,  3*25+5,  4*25+19, 3*25+15}, // FR
            {4*25+10, 2*25+14, 4*25+5,  2*25+9,  4*25+15, 2*25+19}, // FL
            {5*25+14, 2*25+10, 5*25+9,  2*25+5,  5*25+19, 2*25+15}, // BL
            {5*25+10, 3*25+14, 5*25+5,  3*25+9,  5*25+15, 3*25+19}  // BR
        };
        for (int e = 0; e < 12; ++e) {
            m5Facelet[e][0] = t5Init[e][0];
            m5Facelet[e][1] = t5Init[e][1];
            w5Facelet[2 * e][0] = t5Init[e][2];
            w5Facelet[2 * e][1] = t5Init[e][3];
            w5Facelet[2 * e + 1][0] = t5Init[e][4];
            w5Facelet[2 * e + 1][1] = t5Init[e][5];
        }

        for (int m = 0; m < 36; ++m) {
            State5x5 base;
            for (int f = 0; f < 150; ++f) base.facelets[f] = static_cast<uint8_t>(f);
            base.applyMove(moves36[m]);
            for (int i = 0; i < 24; ++i) {
                uint8_t srcT = base.facelets[t5Facelet[i]];
                for (int j = 0; j < 24; ++j) if (t5Facelet[j] == srcT) { t5Perm[m][i] = static_cast<uint8_t>(j); break; }
                uint8_t srcX = base.facelets[x5Facelet[i]];
                for (int j = 0; j < 24; ++j) if (x5Facelet[j] == srcX) { x5Perm[m][i] = static_cast<uint8_t>(j); break; }
                uint8_t srcW = base.facelets[w5Facelet[i][0]];
                for (int j = 0; j < 24; ++j) {
                    if (w5Facelet[j][0] == srcW) {
                        w5Perm[m][i] = static_cast<uint8_t>(j);
                        w5Flip[m][i] = 0;
                        break;
                    }
                    if (w5Facelet[j][1] == srcW) {
                        w5Perm[m][i] = static_cast<uint8_t>(j);
                        w5Flip[m][i] = 1;
                        break;
                    }
                }
            }
            for (int e = 0; e < 12; ++e) {
                uint8_t srcM = base.facelets[m5Facelet[e][0]];
                for (int j = 0; j < 12; ++j) {
                    if (m5Facelet[j][0] == srcM) { m5Perm[m][e] = static_cast<uint8_t>(j); m5Flip[m][e] = 0; break; }
                    if (m5Facelet[j][1] == srcM) { m5Perm[m][e] = static_cast<uint8_t>(j); m5Flip[m][e] = 1; break; }
                }
            }
        }
    }
};

inline const MoveCatalog& catalog() {
    static const MoveCatalog inst;
    return inst;
}

// Helper structs for ultra-fast 4x4 & 5x5 reduction search
struct Fast4x4 {
    std::array<uint8_t, 24> centers; // values 0..5
    std::array<uint8_t, 24> wings;   // encodeColorPair values 0..23

    static Fast4x4 fromState(const State4x4& s) {
        const auto& cat = catalog();
        Fast4x4 f;
        for (int i = 0; i < 24; ++i) {
            f.centers[i] = s.facelets[cat.c4Facelet[i]];
            int8_t code = encodeColorPair(s.facelets[cat.w4Facelet[i][0]], s.facelets[cat.w4Facelet[i][1]]);
            f.wings[i] = (code >= 0) ? static_cast<uint8_t>(code) : 0;
        }
        return f;
    }

    inline void applyMoveIdx(int m) {
        const auto& cat = catalog();
        std::array<uint8_t, 24> nc, nw;
        for (int i = 0; i < 24; ++i) {
            nc[i] = centers[cat.c4Perm[m][i]];
            nw[i] = wings[cat.w4Perm[m][i]] ^ cat.w4Flip[m][i];
        }
        centers = nc;
        wings = nw;
    }

    inline int pairedEdgeCount() const {
        int cnt = 0;
        for (int e = 0; e < 12; ++e) {
            if (wings[2 * e] == wings[2 * e + 1]) cnt++;
        }
        return cnt;
    }
};

struct Fast5x5 {
    std::array<uint8_t, 24> tCenters; // values 0..5
    std::array<uint8_t, 24> xCenters; // values 0..5
    std::array<uint8_t, 12> midges;   // encodeColorPair values 0..23
    std::array<uint8_t, 24> wings;    // encodeColorPair values 0..23

    static Fast5x5 fromState(const State5x5& s) {
        const auto& cat = catalog();
        Fast5x5 f;
        for (int i = 0; i < 24; ++i) {
            f.tCenters[i] = s.facelets[cat.t5Facelet[i]];
            f.xCenters[i] = s.facelets[cat.x5Facelet[i]];
            int8_t wcode = encodeColorPair(s.facelets[cat.w5Facelet[i][0]], s.facelets[cat.w5Facelet[i][1]]);
            f.wings[i] = (wcode >= 0) ? static_cast<uint8_t>(wcode) : 0;
        }
        for (int e = 0; e < 12; ++e) {
            int8_t mcode = encodeColorPair(s.facelets[cat.m5Facelet[e][0]], s.facelets[cat.m5Facelet[e][1]]);
            f.midges[e] = (mcode >= 0) ? static_cast<uint8_t>(mcode) : 0;
        }
        return f;
    }

    inline void applyMoveIdx(int m) {
        const auto& cat = catalog();
        std::array<uint8_t, 24> nt, nx, nw;
        std::array<uint8_t, 12> nm;
        for (int i = 0; i < 24; ++i) {
            nt[i] = tCenters[cat.t5Perm[m][i]];
            nx[i] = xCenters[cat.x5Perm[m][i]];
            nw[i] = wings[cat.w5Perm[m][i]] ^ cat.w5Flip[m][i];
        }
        for (int e = 0; e < 12; ++e) {
            nm[e] = midges[cat.m5Perm[m][e]] ^ cat.m5Flip[m][e];
        }
        tCenters = nt;
        xCenters = nx;
        wings = nw;
        midges = nm;
    }

    inline int pairedWingCount() const {
        int cnt = 0;
        for (int e = 0; e < 12; ++e) {
            if (wings[2 * e] == midges[e]) cnt++;
            if (wings[2 * e + 1] == midges[e]) cnt++;
        }
        return cnt;
    }

    inline int pairedTredgeCount() const {
        int cnt = 0;
        for (int e = 0; e < 12; ++e) {
            if (wings[2 * e] == midges[e] && wings[2 * e + 1] == midges[e]) cnt++;
        }
        return cnt;
    }
};

} // namespace FastCube
