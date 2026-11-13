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
