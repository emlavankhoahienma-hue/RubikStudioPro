#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <cstring>

namespace TrainedData {

struct HeuristicWeights {
    double wPairedEdge = 100.0;      // Weight per fully paired edge/tredge
    double wHalfPair5x5 = 24.0;      // Weight per wing matching its adjacent midge (5x5)
    double wUDParked = 14.0;         // Bonus for keeping paired edge parked on U/D during freeslice
    double wSetup1Away = 9.0;        // Bonus when two matching wings sit on aligned slice slots (1 Uw/Dw away)
    double wMoveCost = 6.5;          // Penalty per move spent
    double wEqMisaligned = 4.0;      // Small penalty if equator center bars are off-home
    double wBadFlip = 3.5;           // Penalty for wrong-orientation wing in E-slice
    int beamWidthFreeslice = 136;     // Freeslice beam width
    int beamWidthL4E = 128;          // Last-4-edges beam width
    int centerTopK = 6;              // Number of diverse center paths to explore for wing lookahead
};

// Byte-level PackBits-style RLE codec (instant < 5 ms decompression across Windows/macOS/iOS)
inline std::vector<uint8_t> compressRLE(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len / 2);
    size_t i = 0;
    while (i < len) {
        size_t run = 1;
        while (i + run < len && run < 128 && data[i + run] == data[i]) {
            run++;
        }
        if (run >= 3) {
            out.push_back(static_cast<uint8_t>(0x80 | (run - 1)));
            out.push_back(data[i]);
            i += run;
        } else {
            size_t litStart = i;
            size_t litLen = 0;
            while (i < len && litLen < 128) {
                size_t ahead = 1;
                while (i + ahead < len && ahead < 3 && data[i + ahead] == data[i]) ahead++;
                if (ahead >= 3 && litLen > 0) break;
                litLen++;
                i++;
            }
            out.push_back(static_cast<uint8_t>(litLen - 1));
            for (size_t k = 0; k < litLen; ++k) out.push_back(data[litStart + k]);
        }
    }
    return out;
}

inline bool decompressRLE(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t expectedLen) {
    size_t si = 0, di = 0;
    while (si < srcLen && di < expectedLen) {
        uint8_t hdr = src[si++];
        if (hdr & 0x80) {
            size_t count = (hdr & 0x7F) + 1;
            if (si >= srcLen || di + count > expectedLen) return false;
            uint8_t val = src[si++];
            std::memset(dst + di, val, count);
            di += count;
        } else {
            size_t count = (hdr & 0x7F) + 1;
            if (si + count > srcLen || di + count > expectedLen) return false;
            std::memcpy(dst + di, src + si, count);
            si += count;
            di += count;
        }
    }
    return (di == expectedLen);
}

// Runtime container holding all 9 exact 4-bit nibble-packed center pattern databases (~2.97 MB total)
struct TableBank {
    static constexpr uint32_t kSizeUD = 735471;
    static constexpr uint32_t kSizeS2 = 900900;
    static constexpr uint32_t kSizeS3 = 343000;

    std::vector<uint8_t> c4_ud;
    std::vector<uint8_t> c4_s2;
    std::vector<uint8_t> c4_s3;

    std::vector<uint8_t> t5_ud;
    std::vector<uint8_t> t5_s2;
    std::vector<uint8_t> t5_s3;

    std::vector<uint8_t> x5_ud;
    std::vector<uint8_t> x5_s2;
    std::vector<uint8_t> x5_s3;

    HeuristicWeights w4x4;
    HeuristicWeights w5x5;
    bool ready = false;
    uint32_t checksum = 0;
};

} // namespace TrainedData

// Include generated compressed binary tables + evolved heuristic weights
#include "Common/TrainedPayload.hpp"
