#include "Common/CubeBridge.hpp"
#include "Common/SolverTypes.hpp"
#include "Cube2x2/Solver2x2_Optimal.hpp"
#include "Cube2x2/Solver2x2_OrtegaCLL.hpp"
#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include "Cube3x3/Solver3x3_CFOP.hpp"
#include "Cube3x3/Solver3x3_Roux.hpp"
#include "Cube4x4/Solver4x4_Optimal.hpp"
#include "Cube4x4/Solver4x4_Yau.hpp"
#include "Cube5x5/Solver5x5_Optimal.hpp"
#include "Cube5x5/Solver5x5_Freeslice.hpp"

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

std::string CubeEngineBridge::solveCubeJson(int cubeSize, int methodId, const std::string& faceletsCsv) {
    try {
        std::vector<uint8_t> colors;
        std::istringstream iss(faceletsCsv);
        std::string token;
        while (std::getline(iss, token, ',')) {
            // Trim whitespace
            size_t first = token.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            size_t last = token.find_last_not_of(" \t\r\n");
            std::string trimmed = token.substr(first, (last - first + 1));
            if (!trimmed.empty()) {
                colors.push_back(static_cast<uint8_t>(std::stoi(trimmed)));
            }
        }

        SolveResult res;
        size_t expectedSize = 6 * cubeSize * cubeSize;
        if (colors.size() != expectedSize) {
            res.isValid = false;
            res.errorMessage = "Số lượng ô màu không khớp với kích thước Rubik " + std::to_string(cubeSize) + "x" + std::to_string(cubeSize);
            res.totalMoves = 0;
            res.solveTimeMs = 0;
            return solveResultToJson(res);
        }

        if (cubeSize == 2) {
            if (methodId == 0) {
                res = Solver2x2_Optimal::solve(colors.data());
            } else {
                res = Solver2x2_OrtegaCLL::solve(colors.data());
            }
        } else if (cubeSize == 3) {
            if (methodId == 0) {
                res = Solver3x3_Min2Phase::solve(colors.data());
            } else if (methodId == 1) {
                res = Solver3x3_CFOP::solve(colors.data());
            } else {
                res = Solver3x3_Roux::solve(colors.data());
            }
        } else if (cubeSize == 4) {
            if (methodId == 0) {
                res = Solver4x4_Optimal::solve(colors.data());
            } else {
                res = Solver4x4_Yau::solve(colors.data());
            }
        } else if (cubeSize == 5) {
            if (methodId == 0) {
                res = Solver5x5_Optimal::solve(colors.data());
            } else {
                res = Solver5x5_Freeslice::solve(colors.data());
            }
        } else {
            res.isValid = false;
            res.errorMessage = "Kích thước Rubik không được hỗ trợ: " + std::to_string(cubeSize);
            res.totalMoves = 0;
            res.solveTimeMs = 0;
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
