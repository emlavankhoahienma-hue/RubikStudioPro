#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>

enum class Axis : uint8_t {
    U = 0,
    D = 1,
    L = 2,
    R = 3,
    F = 4,
    B = 5
};

struct Move {
    uint8_t axis;       // 0..5 (Axis enum)
    uint8_t sliceMask;  // Bitmask: bit 0 = outer layer (layer 1), bit 1 = layer 2, bit 2 = layer 3, etc.
    uint8_t power;      // 1 = 90 deg clockwise, 2 = 180 deg, 3 = 90 deg counter-clockwise (')

    bool operator==(const Move& other) const {
        return axis == other.axis && sliceMask == other.sliceMask && power == other.power;
    }

    bool operator!=(const Move& other) const {
        return !(*this == other);
    }
};

inline std::string moveToString(Move m, int cubeSize) {
    std::string result;
    static const char axisNames[] = {'U', 'D', 'L', 'R', 'F', 'B'};
    if (m.axis > 5) return "";
    char baseAxis = axisNames[m.axis];

    // Check layer configuration
    if (m.sliceMask == 0x01) {
        // Outer layer only
        result += baseAxis;
    } else if (m.sliceMask == 0x03) {
        // Wide 2 layers (e.g. Rw, Uw)
        result += baseAxis;
        result += 'w';
    } else if (m.sliceMask == 0x07 && cubeSize >= 5) {
        // Wide 3 layers (e.g. 3Uw, 3Rw)
        result = "3" + std::string(1, baseAxis) + "w";
    } else if (m.sliceMask == 0x02) {
        // Inner second layer only (e.g. 2R, 2U)
        result = "2" + std::string(1, baseAxis);
    } else if (m.sliceMask == 0x04) {
        // Inner third layer only (e.g. 3R, 3U)
        result = "3" + std::string(1, baseAxis);
    } else {
        // Generic representation for custom slice masks
        result = std::to_string(m.sliceMask) + std::string(1, baseAxis);
    }

    // Power modifier
    if (m.power == 2) {
        result += '2';
    } else if (m.power == 3) {
        result += '\'';
    }

    return result;
}

inline std::vector<Move> parseMoves(const std::string& str) {
    std::vector<Move> moves;
    std::istringstream iss(str);
    std::string token;

    while (iss >> token) {
        if (token.empty()) continue;

        size_t idx = 0;
        uint8_t prefixDigit = 0;
        if (std::isdigit(static_cast<unsigned char>(token[idx]))) {
            prefixDigit = static_cast<uint8_t>(token[idx] - '0');
            idx++;
        }

        if (idx >= token.size()) continue;

        char c = token[idx++];
        uint8_t axis = 0;
        uint8_t sliceMask = 0x01;
        bool isLower = false;

        switch (c) {
            case 'U': axis = 0; break;
            case 'D': axis = 1; break;
            case 'L': axis = 2; break;
            case 'R': axis = 3; break;
            case 'F': axis = 4; break;
            case 'B': axis = 5; break;
            case 'u': axis = 0; isLower = true; break;
            case 'd': axis = 1; isLower = true; break;
            case 'l': axis = 2; isLower = true; break;
            case 'r': axis = 3; isLower = true; break;
            case 'f': axis = 4; isLower = true; break;
            case 'b': axis = 5; isLower = true; break;
            case 'M': // M slice follows L direction
                axis = 2;
                sliceMask = 0x02;
                break;
            case 'E': // E slice follows D direction
                axis = 1;
                sliceMask = 0x02;
                break;
            case 'S': // S slice follows F direction
                axis = 4;
                sliceMask = 0x02;
                break;
            default:
                continue; // unrecognized token
        }

        bool isWide = isLower;
        if (idx < token.size() && (token[idx] == 'w' || token[idx] == 'W')) {
            isWide = true;
            idx++;
        }

        if (isWide) {
            if (prefixDigit >= 3) {
                sliceMask = 0x07; // 3 layers
            } else {
                sliceMask = 0x03; // 2 layers
            }
        } else if (prefixDigit > 0) {
            // e.g. 2R, 3R
            sliceMask = static_cast<uint8_t>(1 << (prefixDigit - 1));
        }

        uint8_t power = 1;
        if (idx < token.size()) {
            if (token[idx] == '2') {
                power = 2;
                idx++;
                if (idx < token.size() && token[idx] == '\'') {
                    idx++; // 2' is treated as 2
                }
            } else if (token[idx] == '\'' || token[idx] == '3') {
                power = 3;
                idx++;
            }
        }

        moves.push_back(Move{axis, sliceMask, power});
    }

    return moves;
}
