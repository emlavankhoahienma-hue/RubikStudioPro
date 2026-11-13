import SwiftUI

public struct CubeNetEditorView: View {
    @ObservedObject var scanner: CIELABScanner
    @Binding var selectedColorId: Int
    var onResetCube: () -> Void
    var onSolveTriggered: () -> Void

    let colorPalette: [(id: Int, shortTag: String, name: String, color: Color)] = [
        (0, "TR", "Trắng", Color.white),
        (1, "VÀ", "Vàng", Color(red: 1.0, green: 0.85, blue: 0.0)),
        (2, "CA", "Cam", Color(red: 1.0, green: 0.45, blue: 0.0)),
        (3, "ĐỎ", "Đỏ", Color(red: 0.9, green: 0.15, blue: 0.15)),
        (4, "LÁ", "X.Lá", Color(red: 0.1, green: 0.75, blue: 0.25)),
        (5, "DU", "X.Dương", Color(red: 0.15, green: 0.45, blue: 0.95))
    ]

    public init(scanner: CIELABScanner,
                selectedColorId: Binding<Int>,
                onResetCube: @escaping () -> Void = {},
                onSolveTriggered: @escaping () -> Void) {
        self.scanner = scanner
        self._selectedColorId = selectedColorId
        self.onResetCube = onResetCube
        self.onSolveTriggered = onSolveTriggered
    }

    private var colorCounts: [Int] {
        var counts = Array(repeating: 0, count: 6)
        let n = scanner.cubeSize
        let nn = n * n
        for face in 0..<min(6, scanner.savedFacelets.count) {
            let slice = scanner.savedFacelets[face].prefix(nn)
            for col in slice where col >= 0 && col < 6 {
                counts[col] += 1
            }
        }
        return counts
    }

    private var isColorCountBalanced: Bool {
        let expected = scanner.cubeSize * scanner.cubeSize
        return colorCounts.allSatisfy { $0 == expected }
    }

    public var body: some View {
        ScrollView(.vertical, showsIndicators: false) {
            VStack(spacing: 14) {
                // Header instruction
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("BẢN ĐỒ MẶT RUBIK \(scanner.cubeSize)x\(scanner.cubeSize)")
                            .font(.system(.subheadline, design: .monospaced))
                            .fontWeight(.bold)
                            .foregroundColor(TactileTheme.textPrimary)
                        Text("Chọn màu bên dưới rồi chạm từng ô để chỉnh sửa")
                            .font(.system(.caption2, design: .monospaced))
                            .foregroundColor(TactileTheme.textSecondary)
                    }
                    Spacer()
                    Button(action: {
                        scanner.resetAllFaces()
                        onResetCube()
                    }) {
                        HStack(spacing: 4) {
                            Image(systemName: "arrow.counterclockwise")
                            Text("ĐẶT LẠI")
                        }
                        .font(.system(.caption2, design: .monospaced))
                        .foregroundColor(TactileTheme.accentOrange)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 6)
                        .background(TactileTheme.panelGraphite)
                        .overlay(RoundedRectangle(cornerRadius: 6).stroke(TactileTheme.borderHairline, lineWidth: 1))
                        .cornerRadius(6)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, 10)

                // Unfolded 6-face cross net:
                //       [ U ]
                // [ L ] [ F ] [ R ] [ B ]
                //       [ D ]
                VStack(spacing: 6) {
                    HStack {
                        Spacer()
                        faceGridView(face: 0, title: "TRÊN (TRẮNG)")
                        Spacer()
                    }

                    HStack(spacing: 6) {
                        faceGridView(face: 2, title: "TRÁI (CAM)")
                        faceGridView(face: 4, title: "TRƯỚC (X.LÁ)")
                        faceGridView(face: 3, title: "PHẢI (ĐỎ)")
                        faceGridView(face: 5, title: "SAU (X.DƯƠNG)")
                    }

                    HStack {
                        Spacer()
                        faceGridView(face: 1, title: "DƯỚI (VÀNG)")
                        Spacer()
                    }
                }
                .padding(.horizontal, 8)

                // Color Count Validation Checklist
                colorCountValidationBar

                // Color Palette Selector
                colorPaletteBar

                // Solve Button
                Button(action: onSolveTriggered) {
                    HStack(spacing: 10) {
                        Image(systemName: "bolt.fill")
                            .font(.headline)
                        Text(isColorCountBalanced ? "ÁP DỤNG & GIẢI NGAY" : "CẦN CHỈNH ĐỦ MÀU ĐỂ GIẢI")
                            .font(.system(.subheadline, design: .monospaced))
                            .fontWeight(.heavy)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
                    .background(isColorCountBalanced ? TactileTheme.accentOrange : TactileTheme.panelGraphite)
                    .foregroundColor(isColorCountBalanced ? TactileTheme.bgCarbon : TactileTheme.textSecondary)
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(isColorCountBalanced ? Color.clear : TactileTheme.accentOrange.opacity(0.6), lineWidth: 1)
                    )
                    .cornerRadius(8)
                    .shadow(color: isColorCountBalanced ? TactileTheme.accentOrange.opacity(0.35) : .clear, radius: 8, x: 0, y: 4)
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 24)
            }
        }
        .background(TactileTheme.bgCarbon)
    }

    private func faceGridView(face: Int, title: String) -> some View {
        let n = scanner.cubeSize
        let tileSize: CGFloat = (n == 2) ? 34 : ((n == 3) ? 24 : ((n == 4) ? 19 : 15))
        let isOddCube = (n % 2 == 1)
        let centerCoord = n / 2

        return VStack(spacing: 3) {
            Text(title)
                .font(.system(size: 8, design: .monospaced))
                .fontWeight(.bold)
                .foregroundColor(TactileTheme.textSecondary)

            VStack(spacing: 2) {
                ForEach(0..<n, id: \.self) { r in
                    HStack(spacing: 2) {
                        ForEach(0..<n, id: \.self) { c in
                            let idx = r * n + c
                            let isFixedCenter = isOddCube && r == centerCoord && c == centerCoord
                            let colorId: Int = {
                                if isFixedCenter { return face }
                                guard face < scanner.savedFacelets.count, idx < scanner.savedFacelets[face].count else {
                                    return face
                                }
                                return scanner.savedFacelets[face][idx]
                            }()

                            ZStack {
                                RoundedRectangle(cornerRadius: 2)
                                    .fill(colorForId(colorId))
                                    .frame(width: tileSize, height: tileSize)
                                    .overlay(
                                        RoundedRectangle(cornerRadius: 2)
                                            .stroke(Color.black.opacity(0.45), lineWidth: 0.75)
                                    )

                                if isFixedCenter {
                                    Text(colorPalette[face].shortTag)
                                        .font(.system(size: max(7, tileSize * 0.42), weight: .heavy, design: .monospaced))
                                        .foregroundColor(Color.black.opacity(0.65))
                                }
                            }
                            .onTapGesture {
                                if !isFixedCenter {
                                    scanner.setFaceletColor(face: face, index: idx, colorId: selectedColorId)
                                }
                            }
                        }
                    }
                }
            }
            .padding(3)
            .background(TactileTheme.panelGraphite)
            .cornerRadius(4)
        }
    }

    private var colorCountValidationBar: some View {
        let expectedPerColor = scanner.cubeSize * scanner.cubeSize
        let counts = colorCounts

        return VStack(spacing: 4) {
            HStack(spacing: 8) {
                ForEach(0..<6, id: \.self) { i in
                    HStack(spacing: 4) {
                        Circle()
                            .fill(colorPalette[i].color)
                            .frame(width: 8, height: 8)
                        Text("\(counts[i])/\(expectedPerColor)")
                            .font(.system(size: 10, design: .monospaced))
                            .fontWeight(.semibold)
                            .foregroundColor(counts[i] == expectedPerColor ? TactileTheme.statusGreen : TactileTheme.accentOrange)
                    }
                }
            }
            Text(isColorCountBalanced ? "Đủ \(expectedPerColor * 6) ô màu hợp lệ" : "Mỗi màu phải có đúng \(expectedPerColor) ô")
                .font(.system(size: 9, design: .monospaced))
                .foregroundColor(isColorCountBalanced ? TactileTheme.statusGreen : TactileTheme.accentOrange)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(TactileTheme.panelGraphite)
        .cornerRadius(6)
    }

    private var colorPaletteBar: some View {
        VStack(spacing: 8) {
            Text("BẢNG MÀU TÔ Ô")
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(TactileTheme.textSecondary)

            HStack(spacing: 8) {
                ForEach(colorPalette, id: \.id) { item in
                    Button(action: {
                        selectedColorId = item.id
                    }) {
                        VStack(spacing: 4) {
                            RoundedRectangle(cornerRadius: 6)
                                .fill(item.color)
                                .frame(maxWidth: .infinity)
                                .frame(height: 38)
                                .overlay(
                                    RoundedRectangle(cornerRadius: 6)
                                        .stroke(selectedColorId == item.id ? TactileTheme.accentOrange : Color.white.opacity(0.15),
                                                lineWidth: selectedColorId == item.id ? 3 : 1)
                                )
                                .overlay(
                                    Text(item.shortTag)
                                        .font(.system(size: 11, weight: .heavy, design: .monospaced))
                                        .foregroundColor(Color.black.opacity(0.65))
                                )

                            Text(item.name)
                                .font(.system(size: 9, design: .monospaced))
                                .foregroundColor(selectedColorId == item.id ? TactileTheme.accentOrange : TactileTheme.textSecondary)
                        }
                    }
                }
            }
        }
        .padding(12)
        .background(TactileTheme.panelGraphite)
        .cornerRadius(8)
        .padding(.horizontal, 16)
    }

    private func colorForId(_ id: Int) -> Color {
        if id >= 0 && id < colorPalette.count {
            return colorPalette[id].color
        }
        return Color.gray
    }
}
