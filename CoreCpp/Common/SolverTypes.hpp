#pragma once
#include <string>
#include <vector>
#include "MoveNotation.hpp"

struct PhaseStep {
    std::string phaseName;        // VD: "Cross", "F2L Pair 1", "3-2-3 Edge Pairing"
    std::vector<Move> moves;      // Danh sách bước xoay của pha này
    std::string notationString;   // Chuỗi hiển thị: "R U R' U'"
};

struct SolveResult {
    bool isValid;                 // Trạng thái Rubik quét vào có hợp lệ không
    std::string errorMessage;     // Báo lỗi nếu quét sai màu/lật góc vật lý
    int totalMoves;               // Tổng số bước
    double solveTimeMs;           // Thời gian giải (mili-giây)
    std::vector<PhaseStep> phases;
};
