import SwiftUI

public struct CubeNetEditorView: View {
    @ObservedObject var scanner: CIELABScanner
    @Binding var selectedColorId: Int
    var onSolveTriggered: () -> Void

    let colorPalette: [(id: Int, name: String, color: Color)] = [
        (0, "Trắng (U)", Color.white),
        (1, "Vàng (D)", Color(red: 1.0, green: 0.85, blue: 0.0)),
        (2, "Cam (L)", Color(red: 1.0, green: 0.45, blue: 0.0)),
        (3, "Đỏ (R)", Color(red: 0.9, green: 0.15, blue: 0.15)),
        (4, "Xanh lá (F)", Color(red: 0.1, green: 0.75, blue: 0.25)),
        (5, "Xanh dương (B)", Color(red: 0.15, green: 0.45, blue: 0.95))
    ]

    public init(scanner: CIELABScanner, selectedColorId: Binding<Int>, onSolveTriggered: @escaping () -> Void) {
        self.scanner = scanner
        self._selectedColorId = selectedColorId
        self.onSolveTriggered = onSolveTriggered
    }

    public var body: some View {
        ScrollView(.vertical, showsIndicators: false) {
            VStack(spacing: 16) {
                // Header instruction
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("BẢN ĐỒ MẶT KHỐI RUBIK (NET)")
                            .font(.system(.subheadline, design: .monospaced))
                            .fontWeight(.bold)
                            .foregroundColor(TactileTheme.textPrimary)
                        Text("Chạm vào từng ô để tô màu hoặc chỉnh sửa kết quả quét")
                            .font(.system(.caption2, design: .monospaced))
                            .foregroundColor(TactileTheme.textSecondary)
                    }
                    Spacer()
                    Button(action: {
                        scanner.resetAllFaces()
                    }) {
                        HStack(spacing: 4) {
                            Image(systemName: "arrow.counterclockwise")
                            Text("ĐẶT LẠI")
                        }
                        .font(.system(.caption2, design: .monospaced))
                        .foregroundColor(TactileTheme.accentOrange)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(TactileTheme.panelGraphite)
                        .cornerRadius(4)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, 8)

                // Unfolded 6-face cross net:
                //       [ U ]
                // [ L ] [ F ] [ R ] [ B ]
                //       [ D ]
                VStack(spacing: 6) {
                    // Top: U (Face 0)
                    HStack {
                        Spacer()
                        faceGridView(face: 0, title: "U (TRÊN)")
                        Spacer()
                    }

                    // Middle: L (2), F (4), R (3), B (5)
                    HStack(spacing: 6) {
                        faceGridView(face: 2, title: "L (TRÁI)")
                        faceGridView(face: 4, title: "F (TRƯỚC)")
                        faceGridView(face: 3, title: "R (PHẢI)")
                        faceGridView(face: 5, title: "B (SAU)")
                    }

                    // Bottom: D (Face 1)
                    HStack {
                        Spacer()
                        faceGridView(face: 1, title: "D (DƯỚI)")
                        Spacer()
                    }
                }
                .padding(.horizontal, 8)

                // Color Count Validation Checklist
                colorCountValidationBar

                // Color Palette Selector
                colorPaletteBar

                // Big Action Solve Button
                Button(action: onSolveTriggered) {
                    HStack(spacing: 10) {
                        Image(systemName: "cpu.fill")
                            .font(.title3)
                        Text("GIẢI KHỐI NGAY")
                            .font(.system(.headline, design: .monospaced))
                            .fontWeight(.heavy)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
                    .background(TactileTheme.accentOrange)
                    .foregroundColor(TactileTheme.bgCarbon)
                    .cornerRadius(8)
                    .shadow(color: TactileTheme.accentOrange.opacity(0.35), radius: 8, x: 0, y: 4)
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 24)
            }
        }
        .background(TactileTheme.bgCarbon)
    }

    private func faceGridView(face: Int, title: String) -> some View {
        let n = scanner.cubeSize
        let tileSize: CGFloat = (n == 2) ? 36 : ((n == 3) ? 26 : ((n == 4) ? 20 : 16))

        return VStack(spacing: 2) {
            Text(title)
                .font(.system(size: 8, design: .monospaced))
                .fontWeight(.bold)
                .foregroundColor(TactileTheme.textSecondary)

            VStack(spacing: 2) {
                ForEach(0..<n, id: \.self) { r in
                    HStack(spacing: 2) {
                        ForEach(0..<n, id: \.self) { c in
                            let idx = r * n + c
                            let colorId = (scanner.savedFacelets[face].count > idx) ? scanner.savedFacelets[face][idx] : face
                            Rectangle()
                                .fill(colorForId(colorId))
                                .frame(width: tileSize, height: tileSize)
                                .border(TactileTheme.borderHairline, width: 1)
                                .onTapGesture {
                                    scanner.setFaceletColor(face: face, index: idx, colorId: selectedColorId)
                                }
                        }
                    }
                }
            }
            .padding(2)
            .background(TactileTheme.panelGraphite)
            .cornerRadius(4)
        }
    }

    private var colorCountValidationBar: some View {
        let expectedPerColor = scanner.cubeSize * scanner.cubeSize
        var counts = Array(repeating: 0, count: 6)
        for face in 0..<6 {
            for col in scanner.savedFacelets[face] {
                if col >= 0 && col < 6 {
                    counts[col] += 1
                }
            }
        }

        return HStack(spacing: 8) {
            ForEach(0..<6, id: \.self) { i in
                HStack(spacing: 4) {
                    Circle()
                        .fill(colorPalette[i].color)
                        .frame(width: 8, height: 8)
                    Text("\(counts[i])/\(expectedPerColor)")
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundColor(counts[i] == expectedPerColor ? TactileTheme.statusGreen : TactileTheme.accentOrange)
                }
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(TactileTheme.panelGraphite)
        .cornerRadius(6)
    }

    private var colorPaletteBar: some View {
        VStack(spacing: 6) {
            Text("BẢNG MÀU ĐANG CHỌN")
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(TactileTheme.textSecondary)

            HStack(spacing: 12) {
                ForEach(colorPalette, id: \.id) { item in
                    Button(action: {
                        selectedColorId = item.id
                    }) {
                        VStack(spacing: 4) {
                            RoundedRectangle(cornerRadius: 6)
                                .fill(item.color)
                                .frame(width: 44, height: 44)
                                .overlay(
                                    RoundedRectangle(cornerRadius: 6)
                                        .stroke(selectedColorId == item.id ? TactileTheme.accentOrange : Color.clear, lineWidth: 3)
                                )
                                .shadow(radius: selectedColorId == item.id ? 4 : 0)

                            Text(item.name.components(separatedBy: " ").first ?? "")
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
