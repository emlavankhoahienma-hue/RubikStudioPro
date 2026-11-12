#pragma once
#include <string>

class CubeEngineBridge {
public:
    // cubeSize: 2, 3, 4, 5
    // methodId: 0 = Optimal (Máy chọn), 1 = Speedcubing 1 (Ortega/CFOP/Yau/Freeslice), 2 = Speedcubing 2 (CLL/Roux)
    // faceletsCsv: chuỗi 6*N*N số từ 0..5 cách nhau bởi dấu phẩy
    // Trả về chuỗi JSON chứa { isValid, errorMessage, totalMoves, solveTimeMs, phases: [...] }
    static std::string solveCubeJson(int cubeSize, int methodId, const std::string& faceletsCsv);

    // C-string compatible version for seamless Swift bridging
    static const char* solveCubeJsonC(int cubeSize, int methodId, const char* faceletsCsv);
};
