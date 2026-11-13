import Foundation

public struct MoveModel: Codable, Equatable, Sendable {
    public let axis: Int
    public let sliceMask: Int
    public let power: Int

    public var notation: String {
        let axisNames = ["U", "D", "L", "R", "F", "B"]
        guard axis >= 0 && axis < 6 else { return "" }
        var res = axisNames[axis]
        if sliceMask == 3 {
            res += "w"
        } else if sliceMask == 7 {
            res = "3" + res + "w"
        } else if sliceMask == 2 {
            res = "2" + res
        }
        if power == 2 {
            res += "2"
        } else if power == 3 {
            res += "'"
        }
        return res
    }

    public var vietnameseHint: String {
        let layerPrefix: String
        switch sliceMask {
        case 3: layerPrefix = "2 lớp "
        case 7: layerPrefix = "3 lớp "
        case 2: layerPrefix = "Lớp trong "
        default: layerPrefix = ""
        }

        let facePart: String
        switch axis {
        case 0: facePart = "Trên (Trắng)"
        case 1: facePart = "Dưới (Vàng)"
        case 2: facePart = "Trái (Cam)"
        case 3: facePart = "Phải (Đỏ)"
        case 4: facePart = "Trước (Xanh lá)"
        case 5: facePart = "Sau (Xanh dương)"
        default: facePart = ""
        }

        if power == 2 {
            return "Xoay \(layerPrefix)mặt \(facePart) 180°"
        }

        switch (axis, power) {
        case (0, 1): return "Xoay \(layerPrefix)tầng \(facePart) sang trái 90°"
        case (0, 3): return "Xoay \(layerPrefix)tầng \(facePart) sang phải 90°"
        case (1, 1): return "Xoay \(layerPrefix)tầng \(facePart) sang phải 90°"
        case (1, 3): return "Xoay \(layerPrefix)tầng \(facePart) sang trái 90°"
        case (2, 1): return "Kéo \(layerPrefix)cột \(facePart) xuống 90°"
        case (2, 3): return "Đẩy \(layerPrefix)cột \(facePart) lên 90°"
        case (3, 1): return "Đẩy \(layerPrefix)cột \(facePart) lên 90°"
        case (3, 3): return "Kéo \(layerPrefix)cột \(facePart) xuống 90°"
        case (4, 1): return "Xoay \(layerPrefix)mặt \(facePart) theo kim đồng hồ 90°"
        case (4, 3): return "Xoay \(layerPrefix)mặt \(facePart) ngược kim đồng hồ 90°"
        case (5, 1): return "Xoay \(layerPrefix)mặt \(facePart) cùng chiều kim (nhìn từ sau) 90°"
        case (5, 3): return "Xoay \(layerPrefix)mặt \(facePart) ngược chiều kim (nhìn từ sau) 90°"
        default: return notation
        }
    }
}

public struct PhaseStepModel: Codable, Identifiable, Sendable {
    public let id: UUID
    public let phaseName: String
    public let notationString: String
    public let moves: [MoveModel]

    private enum CodingKeys: String, CodingKey {
        case phaseName
        case notationString
        case moves
    }

    public init(id: UUID = UUID(), phaseName: String, notationString: String, moves: [MoveModel]) {
        self.id = id
        self.phaseName = phaseName
        self.notationString = notationString
        self.moves = moves
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.id = UUID()
        self.phaseName = try container.decode(String.self, forKey: .phaseName)
        self.notationString = try container.decode(String.self, forKey: .notationString)
        self.moves = try container.decode([MoveModel].self, forKey: .moves)
    }
}

public struct SolveResponse: Codable, Sendable {
    public let isValid: Bool
    public let errorMessage: String
    public let totalMoves: Int
    public let solveTimeMs: Double
    public let phases: [PhaseStepModel]

    public static func parse(jsonString: String) -> SolveResponse? {
        guard let data = jsonString.data(using: .utf8) else { return nil }
        return try? JSONDecoder().decode(SolveResponse.self, from: data)
    }
}
