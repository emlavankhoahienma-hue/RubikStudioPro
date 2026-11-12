import SwiftUI

public struct TactileTheme {
    // Professional tactile hardware color palette
    public static let bgCarbon = Color(hex: "#0D0E11")
    public static let panelGraphite = Color(hex: "#16181D")
    public static let borderHairline = Color(hex: "#262930")
    public static let accentOrange = Color(hex: "#FF5722")
    public static let textPrimary = Color(hex: "#F3F4F6")
    public static let textSecondary = Color(hex: "#9CA3AF")
    public static let statusGreen = Color(hex: "#10B981")
    public static let statusRed = Color(hex: "#EF4444")
}

extension Color {
    public init(hex: String) {
        let hex = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        var int: UInt64 = 0
        Scanner(string: hex).scanHexInt64(&int)
        let a, r, g, b: UInt64
        switch hex.count {
        case 3: // RGB (12-bit)
            (a, r, g, b) = (255, (int >> 8) * 17, (int >> 4 & 0xF) * 17, (int & 0xF) * 17)
        case 6: // RGB (24-bit)
            (a, r, g, b) = (255, int >> 16, int >> 8 & 0xFF, int & 0xFF)
        case 8: // ARGB (32-bit)
            (a, r, g, b) = (int >> 24, int >> 16 & 0xFF, int >> 8 & 0xFF, int & 0xFF)
        default:
            (a, r, g, b) = (255, 0, 0, 0)
        }

        self.init(
            .sRGB,
            red: Double(r) / 255,
            green: Double(g) / 255,
            blue: Double(b) / 255,
            opacity: Double(a) / 255
        )
    }
}
