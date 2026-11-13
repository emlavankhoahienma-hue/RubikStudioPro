#include "Common/CubeBridge.hpp"
#include "Common/SolverTypes.hpp"
#include "Cube2x2/Solver2x2_Optimal.hpp"
#include "Cube2x2/State2x2.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include "Cube3x3/State3x3.hpp"
#include "Cube4x4/Solver4x4_Optimal.hpp"
#include "Cube4x4/State4x4.hpp"
#include "Cube5x5/Solver5x5_Optimal.hpp"
#include "Cube5x5/State5x5.hpp"

#include <array>
#include <cctype>
#include <vector>
#include <sstream>
#include <iomanip>

static std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static std::string solveResultToJson(const SolveResult& res) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"isValid\": " << (res.isValid ? "true" : "false") << ",\n";
    oss << "  \"errorMessage\": \"" << escapeJson(res.errorMessage) << "\",\n";
    oss << "  \"totalMoves\": " << res.totalMoves << ",\n";
    oss << "  \"solveTimeMs\": " << std::fixed << std::setprecision(2) << res.solveTimeMs << ",\n";
    oss << "  \"phases\": [\n";

    for (size_t i = 0; i < res.phases.size(); ++i) {
        const auto& ph = res.phases[i];
        oss << "    {\n";
        oss << "      \"phaseName\": \"" << escapeJson(ph.phaseName) << "\",\n";
        oss << "      \"notationString\": \"" << escapeJson(ph.notationString) << "\",\n";
        oss << "      \"moves\": [";
        for (size_t m = 0; m < ph.moves.size(); ++m) {
            const auto& mv = ph.moves[m];
            if (m > 0) oss << ", ";
            oss << "{\"axis\":" << static_cast<int>(mv.axis)
                << ",\"sliceMask\":" << static_cast<int>(mv.sliceMask)
                << ",\"power\":" << static_cast<int>(mv.power) << "}";
        }
        oss << "]\n";
        oss << "    }" << (i + 1 < res.phases.size() ? "," : "") << "\n";
    }

    oss << "  ]\n";
    oss << "}";
    return oss.str();
}

static bool normalize2x2Facelets(const std::vector<uint8_t>& raw, uint8_t outColors[24]) {
    // Standard outward normals: 0:U(+Y), 1:D(-Y), 2:L(-X), 3:R(+X), 4:F(+Z), 5:B(-Z)
    static const int normals[6][3] = {
        {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, -1}
    };
    uint8_t cD = raw[6], cB = raw[23], cL = raw[10];
    if (cD > 5 || cB > 5 || cL > 5) return false;
    const int* nB = normals[cB];
    const int* nD = normals[cD];
    const int* nL = normals[cL];
    // Cross product n(B) x n(D) must equal n(L) for a valid right-handed corner
    int cx = nB[1] * nD[2] - nB[2] * nD[1];
    int cy = nB[2] * nD[0] - nB[0] * nD[2];
    int cz = nB[0] * nD[1] - nB[1] * nD[0];
    if (cx != nL[0] || cy != nL[1] || cz != nL[2]) {
        return false;
    }

    uint8_t remap[6] = {0, 1, 2, 3, 4, 5};
    remap[cD ^ 1] = 0; // U
    remap[cD]     = 1; // D
    remap[cL]     = 2; // L
    remap[cL ^ 1] = 3; // R
    remap[cB ^ 1] = 4; // F
    remap[cB]     = 5; // B

    for (int i = 0; i < 24; ++i) {
        outColors[i] = remap[raw[i]];
    }
    return true;
}

std::string CubeEngineBridge::solveCubeJson(int cubeSize, int /*methodId*/, const std::string& faceletsCsv) {
    try {
        if (cubeSize < 2 || cubeSize > 5) {
            SolveResult res;
            res.isValid = false;
            res.errorMessage = "Kích thước Rubik không được hỗ trợ: " + std::to_string(cubeSize);
            res.totalMoves = 0;
            res.solveTimeMs = 0;
            return solveResultToJson(res);
        }

        std::vector<uint8_t> colors;
        std::istringstream iss(faceletsCsv);
        std::string token;
        while (std::getline(iss, token, ',')) {
            size_t first = token.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            size_t last = token.find_last_not_of(" \t\r\n");
            std::string trimmed = token.substr(first, (last - first + 1));
            for (unsigned char ch : trimmed) {
                if (!std::isdigit(ch)) {
                    SolveResult res;
                    res.isValid = false;
                    res.errorMessage = "Dữ liệu màu chứa ký tự không hợp lệ.";
                    res.totalMoves = 0;
                    res.solveTimeMs = 0;
                    return solveResultToJson(res);
                }
            }
            int val = std::stoi(trimmed);
            if (val < 0 || val > 5) {
                SolveResult res;
                res.isValid = false;
                res.errorMessage = "Mã màu vượt quá phạm vi 0..5.";
                res.totalMoves = 0;
                res.solveTimeMs = 0;
                return solveResultToJson(res);
            }
            colors.push_back(static_cast<uint8_t>(val));
        }

        SolveResult res;
        size_t expectedSize = static_cast<size_t>(6 * cubeSize * cubeSize);
        if (colors.size() != expectedSize) {
            res.isValid = false;
            res.errorMessage = "Số lượng ô màu không khớp với kích thước Rubik " + std::to_string(cubeSize) + "x" + std::to_string(cubeSize);
            res.totalMoves = 0;
            res.solveTimeMs = 0;
            return solveResultToJson(res);
        }

        int counts[6] = {0};
        for (uint8_t c : colors) counts[c]++;
        int expectedPerColor = cubeSize * cubeSize;
        for (int c = 0; c < 6; ++c) {
            if (counts[c] != expectedPerColor) {
                res.isValid = false;
                res.errorMessage = "Số lượng 6 màu chưa đều (mỗi màu phải đủ " + std::to_string(expectedPerColor) + " ô).";
                res.totalMoves = 0;
                res.solveTimeMs = 0;
                return solveResultToJson(res);
            }
        }

        if (cubeSize == 2) {
            uint8_t normalized[24];
            if (!normalize2x2Facelets(colors, normalized)) {
                res.isValid = false;
                res.errorMessage = "Góc tham chiếu DBL của khối 2x2 bị sai màu vật lý.";
                res.totalMoves = 0;
                res.solveTimeMs = 0;
                return solveResultToJson(res);
            }
            // Validate every corner facelet triple uniquely forms corners 0..7
            static const int cornerFacelets2[8][3] = {
                {3, 12, 17}, {2, 16, 9}, {0, 8, 21}, {1, 20, 13},
                {5, 19, 14}, {4, 11, 18}, {7, 15, 22}, {6, 23, 10}
            };
            static const uint8_t cornerColors2[8][3] = {
                {0, 3, 4}, {0, 4, 2}, {0, 2, 5}, {0, 5, 3},
                {1, 4, 3}, {1, 2, 4}, {1, 3, 5}, {1, 5, 2}
            };
            bool seen[8] = {false};
            for (int i = 0; i < 8; ++i) {
                uint8_t c0 = normalized[cornerFacelets2[i][0]];
                uint8_t c1 = normalized[cornerFacelets2[i][1]];
                uint8_t c2 = normalized[cornerFacelets2[i][2]];
                int piece = -1;
                for (int p = 0; p < 8; ++p) {
                    const uint8_t* ref = cornerColors2[p];
                    if ((c0 == ref[0] && c1 == ref[1] && c2 == ref[2]) ||
                        (c1 == ref[0] && c2 == ref[1] && c0 == ref[2]) ||
                        (c2 == ref[0] && c0 == ref[1] && c1 == ref[2])) {
                        piece = p;
                        break;
                    }
                }
                if (piece < 0 || seen[piece]) {
                    res.isValid = false;
                    res.errorMessage = "Màu sắc các góc 2x2 không hợp lệ hoặc bị trùng góc.";
                    res.totalMoves = 0;
                    res.solveTimeMs = 0;
                    return solveResultToJson(res);
                }
                seen[piece] = true;
            }
            res = Solver2x2_Optimal::solve(normalized);
        } else if (cubeSize == 3) {
            res = Solver3x3_Min2Phase::solve(colors.data());
        } else if (cubeSize == 4) {
            res = Solver4x4_Optimal::solve(colors.data());
        } else {
            res = Solver5x5_Optimal::solve(colors.data());
        }

        return solveResultToJson(res);
    } catch (const std::exception& e) {
        SolveResult errRes;
        errRes.isValid = false;
        errRes.errorMessage = std::string("Lỗi xử lý thuật toán: ") + e.what();
        errRes.totalMoves = 0;
        errRes.solveTimeMs = 0;
        return solveResultToJson(errRes);
    } catch (...) {
        SolveResult errRes;
        errRes.isValid = false;
        errRes.errorMessage = "Lỗi ngoại lệ không xác định khi giải khối.";
        errRes.totalMoves = 0;
        errRes.solveTimeMs = 0;
        return solveResultToJson(errRes);
    }
}

static thread_local std::string tlsJsonBuffer;

const char* CubeEngineBridge::solveCubeJsonC(int cubeSize, int methodId, const char* faceletsCsv) {
    if (!faceletsCsv) return "";
    tlsJsonBuffer = solveCubeJson(cubeSize, methodId, std::string(faceletsCsv));
    return tlsJsonBuffer.c_str();
}
