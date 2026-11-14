#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include "Common/MoveNotation.hpp"

namespace MoveOptimizer {

// Algebraically simplifies a move sequence for N-x-N cubes (2 <= N <= 5):
// Combines consecutive moves along the same axis pair (U/D, L/R, F/B) via per-slice mod-4 accumulation.
inline std::vector<Move> optimize(const std::vector<Move>& input, int cubeSize) {
    if (input.empty() || cubeSize < 2 || cubeSize > 5) return input;

    std::vector<Move> out;
    out.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        int pairId = input[i].axis / 2; // 0: U/D, 1: L/R, 2: F/B
        // Accumulate layer powers [0 .. cubeSize-1] from the perspective of baseAxis = 2 * pairId
        std::array<int, 5> slicePow = {0, 0, 0, 0, 0};

        while (i < input.size() && (input[i].axis / 2) == pairId) {
            const Move& m = input[i];
            int sign = (m.axis % 2 == 0) ? 1 : -1;
            int effPow = (sign > 0) ? (m.power % 4) : ((4 - (m.power % 4)) % 4);
            for (int l = 0; l < cubeSize; ++l) {
                if (m.sliceMask & (1 << l)) {
                    int canonLayer = (m.axis % 2 == 0) ? l : (cubeSize - 1 - l);
                    slicePow[canonLayer] = (slicePow[canonLayer] + effPow) & 3;
                }
            }
            ++i;
        }

        // Emit minimal moves representing slicePow[0 .. cubeSize-1]
        // Split into even-axis side [0 .. half-1] and odd-axis side [cubeSize-1 down to cubeSize-half]
        // Note: on odd cubes (3x3, 5x5), middle layer only comes from valid sliceMask if ever used.
        int half = (cubeSize + 1) / 2;

        // Helper to emit grouped slices with equal non-zero power on base or opp axis
        auto emitSide = [&](uint8_t axisId, bool reverseIdx, int maxLayers) {
            // Try merging outer + inner wide combinations cleanly per distinct power {1, 2, 3}
            // First check if multiple layers share the exact same non-zero power -> wide move!
            for (uint8_t pw = 1; pw <= 3; ++pw) {
                uint8_t mask = 0;
                for (int l = 0; l < maxLayers; ++l) {
                    int canon = reverseIdx ? (cubeSize - 1 - l) : l;
                    int val = reverseIdx ? ((4 - slicePow[canon]) & 3) : slicePow[canon];
                    if (val == pw) {
                        mask |= static_cast<uint8_t>(1 << l);
                    }
                }
                if (mask == 0) continue;

                // Only emit single-slice or supported prefix-wide masks (0x01, 0x03, 0x07, 0x02, 0x04)
                // If non-contiguous (e.g. mask == 0x05), split into individual supported layer bits
                if (mask == 0x01 || mask == 0x03 || mask == 0x02 || mask == 0x04 || (mask == 0x07 && cubeSize >= 5)) {
                    out.push_back(Move{axisId, mask, pw});
                } else {
                    for (int l = 0; l < maxLayers; ++l) {
                        if (mask & (1 << l)) {
                            out.push_back(Move{axisId, static_cast<uint8_t>(1 << l), pw});
                        }
                    }
                }
            }
        };

        // Map middle layer of odd cube (if non-zero) arbitrarily to baseAxis side
        int baseCount = (cubeSize % 2 == 1) ? half : (cubeSize / 2);
        int oppCount = cubeSize - baseCount;

        emitSide(static_cast<uint8_t>(pairId * 2), false, baseCount);
        emitSide(static_cast<uint8_t>(pairId * 2 + 1), true, oppCount);
    }

    return out;
}

} // namespace MoveOptimizer
