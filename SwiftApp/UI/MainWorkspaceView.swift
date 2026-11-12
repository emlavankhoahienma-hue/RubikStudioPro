import SwiftUI
import SceneKit
import AVFoundation

public enum AppWorkspaceMode: Int, CaseIterable, Identifiable {
    case studio3D = 0
    case cameraScanner = 1
    case manualNetEditor = 2

    public var id: Int { self.rawValue }

    public var title: String {
        switch self {
        case .studio3D: return "3D STUDIO"
        case .cameraScanner: return "QUÉT CAMERA"
        case .manualNetEditor: return "BẢN ĐỒ MÀU"
        }
    }

    public var icon: String {
        switch self {
        case .studio3D: return "cube.transparent"
        case .cameraScanner: return "camera.viewfinder"
        case .manualNetEditor: return "paintpalette.fill"
        }
    }
}

public struct FlatMoveItem: Identifiable, Sendable {
    public let id: Int
    public let phaseName: String
    public let move: MoveModel
}

public struct MainWorkspaceView: View {
    @StateObject private var sceneModel = ParametricRubikScene(cubeSize: 3)
    @StateObject private var scanner = CIELABScanner()

    @State private var currentMode: AppWorkspaceMode = .studio3D
    @State private var selectedCubeSize: Int = 3
    @State private var selectedMethodId: Int = 0
    @State private var selectedPaletteColorId: Int = 0

    @State private var playbackSpeed: Double = 1.0 // 0.25x to 2.0x
    @State private var isPlaying: Bool = false
    @State private var currentStepIndex: Int = 0

    @State private var solveResponse: SolveResponse?
    @State private var flatMoves: [FlatMoveItem] = []
    @State private var isSolving: Bool = false
    @State private var errorMessage: String? = nil

    let faceGuideNames: [String] = [
        "MẶT 1/6: TRẮNG (U - TRÊN)",
        "MẶT 2/6: VÀNG (D - DƯỚI)",
        "MẶT 3/6: CAM (L - TRÁI)",
        "MẶT 4/6: ĐỎ (R - PHẢI)",
        "MẶT 5/6: XANH LÁ (F - TRƯỚC)",
        "MẶT 6/6: XANH DƯƠNG (B - SAU)"
    ]

    public init() {}

    public var body: some View {
        ZStack {
            TactileTheme.bgCarbon.ignoresSafeArea()

            VStack(spacing: 0) {
                // 1. Top Header Bar & Mode Picker
                topNavigationHeader

                // 2. Main Center Content based on selected mode
                Group {
                    switch currentMode {
                    case .studio3D:
                        studio3DContentView
                    case .cameraScanner:
                        cameraScannerContentView
                    case .manualNetEditor:
                        CubeNetEditorView(scanner: scanner, selectedColorId: $selectedPaletteColorId) {
                            runSolve(csv: scanner.getFaceletsCsv())
                            currentMode = .studio3D
                        }
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)

                // 3. Bottom Algorithm Tape & Transport Deck (Shown only in 3D Studio mode)
                if currentMode == .studio3D {
                    algorithmTapeSection
                    bottomTransportDeck
                }
            }
        }
        .onAppear {
            scanner.setCubeSize(selectedCubeSize)
            triggerDefaultSolvedDisplay()
        }
    }

    // MARK: - 1. Top Navigation Header
    private var topNavigationHeader: some View {
        VStack(spacing: 6) {
            HStack {
                Text("RUBIK STUDIO PRO")
                    .font(.system(.headline, design: .monospaced))
                    .fontWeight(.heavy)
                    .foregroundColor(TactileTheme.textPrimary)

                Spacer()

                if isSolving {
                    ProgressView()
                        .progressViewStyle(CircularProgressViewStyle(tint: TactileTheme.accentOrange))
                        .scaleEffect(0.7)
                } else if let res = solveResponse, res.isValid {
                    Text("\(res.totalMoves) BƯỚC • \(String(format: "%.1f", res.solveTimeMs))ms")
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(TactileTheme.accentOrange)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(TactileTheme.accentOrange.opacity(0.15))
                        .cornerRadius(4)
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 6)

            // Mode Selector Tabs (3D Studio / Scanner / Manual Net)
            HStack(spacing: 6) {
                ForEach(AppWorkspaceMode.allCases) { mode in
                    Button(action: {
                        withAnimation(.easeInOut(duration: 0.2)) {
                            currentMode = mode
                        }
                    }) {
                        HStack(spacing: 6) {
                            Image(systemName: mode.icon)
                                .font(.system(size: 12))
                            Text(mode.title)
                                .font(.system(size: 11, design: .monospaced))
                                .fontWeight(.bold)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 8)
                        .background(currentMode == mode ? TactileTheme.accentOrange : TactileTheme.panelGraphite)
                        .foregroundColor(currentMode == mode ? TactileTheme.bgCarbon : TactileTheme.textSecondary)
                        .cornerRadius(6)
                    }
                }
            }
            .padding(.horizontal, 12)

            // Cube Size & Method Controls
            HStack(spacing: 8) {
                Picker("Size", selection: $selectedCubeSize) {
                    Text("2x2").tag(2)
                    Text("3x3").tag(3)
                    Text("4x4").tag(4)
                    Text("5x5").tag(5)
                }
                .pickerStyle(SegmentedPickerStyle())
                .onChange(of: selectedCubeSize) { newSize in
                    sceneModel.rebuildCube(size: newSize)
                    scanner.setCubeSize(newSize)
                    triggerDefaultSolvedDisplay()
                }

                Picker("Method", selection: $selectedMethodId) {
                    Text("OPTIMAL").tag(0)
                    Text(selectedCubeSize == 2 ? "ORTEGA" : (selectedCubeSize == 4 ? "YAU" : (selectedCubeSize == 5 ? "FREESLICE" : "CFOP"))).tag(1)
                    Text(selectedCubeSize == 2 ? "CLL" : (selectedCubeSize == 3 ? "ROUX" : "SPEED 2")).tag(2)
                }
                .pickerStyle(SegmentedPickerStyle())
                .onChange(of: selectedMethodId) { _ in
                    runSolve(csv: scanner.getFaceletsCsv())
                }
            }
            .padding(.horizontal, 12)
            .padding(.bottom, 6)
        }
        .background(TactileTheme.panelGraphite)
        .overlay(
            Rectangle().frame(height: 1).foregroundColor(TactileTheme.borderHairline),
            alignment: .bottom
        )
    }

    // MARK: - 2A. 3D Studio Content View
    private var studio3DContentView: some View {
        ZStack {
            SceneView(
                scene: sceneModel.scene,
                pointOfView: sceneModel.cameraNode,
                options: [.allowsCameraControl, .autoenablesDefaultLighting]
            )

            // Floating Scramble & Solve Action Buttons
            VStack {
                if let err = errorMessage {
                    Text(err)
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(Color.red)
                        .padding(8)
                        .background(TactileTheme.panelGraphite.opacity(0.9))
                        .cornerRadius(6)
                        .padding(.top, 8)
                }

                Spacer()

                HStack(spacing: 12) {
                    Button(action: scrambleCube) {
                        HStack(spacing: 6) {
                            Image(systemName: "dice.fill")
                            Text("XÁO KHỐI")
                        }
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 8)
                        .background(TactileTheme.panelGraphite.opacity(0.9))
                        .foregroundColor(TactileTheme.textPrimary)
                        .overlay(RoundedRectangle(cornerRadius: 6).stroke(TactileTheme.borderHairline, lineWidth: 1))
                        .cornerRadius(6)
                    }

                    Button(action: {
                        runSolve(csv: scanner.getFaceletsCsv())
                    }) {
                        HStack(spacing: 6) {
                            Image(systemName: "bolt.fill")
                            Text("GIẢI TỰ ĐỘNG")
                        }
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 8)
                        .background(TactileTheme.accentOrange)
                        .foregroundColor(TactileTheme.bgCarbon)
                        .cornerRadius(6)
                        .shadow(color: TactileTheme.accentOrange.opacity(0.4), radius: 6, x: 0, y: 3)
                    }
                }
                .padding(.bottom, 8)
            }
        }
    }

    // MARK: - 2B. Camera Scanner Content View
    private var cameraScannerContentView: some View {
        ZStack {
            if scanner.isCameraAuthorized {
                CameraVideoPreview(session: scanner.captureSession)
                    .ignoresSafeArea()
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "camera.fill")
                        .font(.system(size: 48))
                        .foregroundColor(TactileTheme.textSecondary)
                    Text("CẦN QUYỀN TRUY CẬP CAMERA")
                        .font(.system(.headline, design: .monospaced))
                        .foregroundColor(TactileTheme.textPrimary)
                    Text("Vui lòng cấp quyền trong Cài đặt iPhone để quét mặt khối Rubik")
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(TactileTheme.textSecondary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 32)
                    Button("CẤP QUYỀN CAMERA") {
                        scanner.checkCameraAuthorization()
                    }
                    .font(.system(.body, design: .monospaced))
                    .fontWeight(.bold)
                    .padding(.horizontal, 20)
                    .padding(.vertical, 10)
                    .background(TactileTheme.accentOrange)
                    .foregroundColor(TactileTheme.bgCarbon)
                    .cornerRadius(8)
                }
            }

            // Dark semi-transparent overlay around the square reticle
            VStack(spacing: 12) {
                // Top Scanner Banner
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(faceGuideNames[scanner.currentFaceIndex])
                            .font(.system(.subheadline, design: .monospaced))
                            .fontWeight(.heavy)
                            .foregroundColor(TactileTheme.accentOrange)
                        Text("Căn chỉnh 9 ô vuông trùng khớp với khối Rubik")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundColor(Color.white.opacity(0.8))
                    }
                    Spacer()

                    Button(action: {
                        scanner.toggleTorch()
                    }) {
                        Image(systemName: scanner.isTorchOn ? "flashlight.on.fill" : "flashlight.off.fill")
                            .font(.title3)
                            .foregroundColor(scanner.isTorchOn ? TactileTheme.accentOrange : Color.white)
                            .padding(8)
                            .background(TactileTheme.panelGraphite.opacity(0.8))
                            .clipShape(Circle())
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, 10)

                Spacer()

                // Center Reticle Grid with Live Sampled Colors
                let n = scanner.cubeSize
                let reticleSize: CGFloat = 260
                ZStack {
                    RoundedRectangle(cornerRadius: 16)
                        .stroke(TactileTheme.accentOrange, lineWidth: 3)
                        .frame(width: reticleSize, height: reticleSize)
                        .shadow(color: TactileTheme.accentOrange.opacity(0.5), radius: 10)

                    VStack(spacing: 3) {
                        ForEach(0..<n, id: \.self) { r in
                            HStack(spacing: 3) {
                                ForEach(0..<n, id: \.self) { c in
                                    let idx = r * n + c
                                    let sample = (scanner.liveSampledColors.count > idx) ? scanner.liveSampledColors[idx] : nil

                                    ZStack {
                                        RoundedRectangle(cornerRadius: 6)
                                            .stroke(Color.white.opacity(0.7), lineWidth: 1.5)
                                            .background(
                                                sample?.uiColor.opacity(0.65) ?? Color.black.opacity(0.3)
                                            )
                                            .frame(width: (reticleSize - CGFloat(4 * n)) / CGFloat(n),
                                                   height: (reticleSize - CGFloat(4 * n)) / CGFloat(n))

                                        // Central color dot
                                        Circle()
                                            .fill(sample?.uiColor ?? Color.clear)
                                            .frame(width: 12, height: 12)
                                    }
                                }
                            }
                        }
                    }
                }

                Spacer()

                // Face progress indicator dots
                HStack(spacing: 12) {
                    let tags = ["U", "D", "L", "R", "F", "B"]
                    ForEach(0..<6, id: \.self) { i in
                        VStack(spacing: 2) {
                            Text(tags[i])
                                .font(.system(size: 9, design: .monospaced))
                                .fontWeight(.bold)
                                .foregroundColor(i == scanner.currentFaceIndex ? TactileTheme.accentOrange : Color.white)
                            Circle()
                                .fill(scanner.faceStatus[i] ? TactileTheme.statusGreen : (i == scanner.currentFaceIndex ? TactileTheme.accentOrange : Color.gray.opacity(0.5)))
                                .frame(width: 14, height: 14)
                        }
                    }
                }
                .padding(.vertical, 8)
                .padding(.horizontal, 16)
                .background(TactileTheme.panelGraphite.opacity(0.9))
                .cornerRadius(20)

                // Bottom Action Button: Capture Face
                Button(action: {
                    scanner.captureCurrentFace()
                    if scanner.isScanningComplete {
                        runSolve(csv: scanner.getFaceletsCsv())
                        currentMode = .studio3D
                    }
                }) {
                    HStack(spacing: 8) {
                        Image(systemName: "camera.circle.fill")
                            .font(.title2)
                        Text(scanner.currentFaceIndex == 5 ? "HOÀN TẤT & GIẢI" : "CHỤP MẶT (\(scanner.currentFaceIndex + 1)/6)")
                            .font(.system(.body, design: .monospaced))
                            .fontWeight(.bold)
                    }
                    .foregroundColor(TactileTheme.bgCarbon)
                    .padding(.horizontal, 28)
                    .padding(.vertical, 12)
                    .background(TactileTheme.accentOrange)
                    .cornerRadius(8)
                    .shadow(color: TactileTheme.accentOrange.opacity(0.4), radius: 8, x: 0, y: 4)
                }
                .padding(.bottom, 16)
            }
        }
    }

    // MARK: - 3. Algorithm Tape Section
    private var algorithmTapeSection: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    if flatMoves.isEmpty {
                        Text("KHỐI ĐÃ GIẢI HOÀN TOÀN")
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(TactileTheme.statusGreen)
                            .padding(.horizontal, 16)
                    } else {
                        ForEach(flatMoves) { item in
                            Button(action: {
                                jumpToStep(item.id)
                            }) {
                                VStack(spacing: 2) {
                                    Text(item.move.notation)
                                        .font(.system(.body, design: .monospaced))
                                        .fontWeight(.bold)
                                        .foregroundColor(currentStepIndex == item.id ? TactileTheme.accentOrange : TactileTheme.textPrimary)

                                    Text(item.phaseName)
                                        .font(.system(size: 8, design: .monospaced))
                                        .foregroundColor(TactileTheme.textSecondary)
                                        .lineLimit(1)
                                }
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(currentStepIndex == item.id ? TactileTheme.accentOrange.opacity(0.18) : TactileTheme.panelGraphite)
                                .overlay(
                                    RoundedRectangle(cornerRadius: 6)
                                        .stroke(currentStepIndex == item.id ? TactileTheme.accentOrange : TactileTheme.borderHairline, lineWidth: currentStepIndex == item.id ? 2 : 1)
                                )
                                .cornerRadius(6)
                            }
                            .id(item.id)
                        }
                    }
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 6)
            }
            .onChange(of: currentStepIndex) { idx in
                withAnimation {
                    proxy.scrollTo(idx, anchor: .center)
                }
            }
        }
        .background(TactileTheme.panelGraphite)
        .overlay(
            Rectangle().frame(height: 1).foregroundColor(TactileTheme.borderHairline),
            alignment: .top
        )
    }

    // MARK: - 4. Bottom Transport Deck
    private var bottomTransportDeck: some View {
        VStack(spacing: 6) {
            // Speed slider
            HStack {
                Text("TỐC ĐỘ: \(String(format: "%.1f", playbackSpeed))x")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(TactileTheme.textSecondary)

                Slider(value: $playbackSpeed, in: 0.5...3.0, step: 0.25)
                    .accentColor(TactileTheme.accentOrange)
            }
            .padding(.horizontal, 24)

            // Playback buttons
            HStack(spacing: 24) {
                Button(action: jumpToBeginning) {
                    Image(systemName: "backward.end.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }

                Button(action: stepBackward) {
                    Image(systemName: "backward.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }

                Button(action: togglePlayPause) {
                    Image(systemName: isPlaying ? "pause.circle.fill" : "play.circle.fill")
                        .font(.system(size: 40))
                        .foregroundColor(TactileTheme.accentOrange)
                }

                Button(action: stepForward) {
                    Image(systemName: "forward.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }

                Button(action: jumpToEnd) {
                    Image(systemName: "forward.end.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }
            }
            .padding(.bottom, 10)
        }
        .background(TactileTheme.panelGraphite)
    }

    // MARK: - Actions & Logic
    private func triggerDefaultSolvedDisplay() {
        self.errorMessage = nil
        self.solveResponse = nil
        self.flatMoves = []
        self.currentStepIndex = 0
    }

    private func scrambleCube() {
        isPlaying = false
        sceneModel.rebuildCube(size: selectedCubeSize)

        // Generate 12-20 moves random scramble
        let axes: [UInt8] = [0, 1, 2, 3, 4, 5] // U, D, L, R, F, B
        let powers: [UInt8] = [1, 2, 3]

        var moves: [MoveModel] = []
        let count = (selectedCubeSize == 2) ? 10 : ((selectedCubeSize == 3) ? 18 : 24)

        for _ in 0..<count {
            let axis = axes.randomElement()!
            let p = powers.randomElement()!
            let sliceMask: UInt8 = (selectedCubeSize >= 4 && Bool.random()) ? 0x03 : 0x01
            let notation = "\(axis)-\(p)"
            moves.append(MoveModel(axis: axis, sliceMask: sliceMask, power: p, notation: notation))
        }

        for m in moves {
            sceneModel.animateMove(axis: m.axis, sliceMask: m.sliceMask, power: m.power, duration: 0.05)
        }

        // Run solve from bridge
        runSolve(csv: scanner.getFaceletsCsv())
    }

    private func runSolve(csv: String) {
        isSolving = true
        errorMessage = nil

        Task.detached(priority: .userInitiated) {
            let size = Int32(self.selectedCubeSize)
            let method = Int32(self.selectedMethodId)

            guard let cStr = CubeEngineBridge.solveCubeJsonC(size, method, csv) else {
                await MainActor.run {
                    self.errorMessage = "Không thể kết nối C++ Solver Bridge."
                    self.isSolving = false
                }
                return
            }

            let jsonString = String(cString: cStr)
            await MainActor.run {
                self.isSolving = false
                if let resp = SolveResponse.parse(jsonString: jsonString) {
                    self.solveResponse = resp
                    if !resp.isValid {
                        self.errorMessage = resp.errorMessage
                        return
                    }

                    var items: [FlatMoveItem] = []
                    var counter = 0
                    for phase in resp.phases {
                        for m in phase.moves {
                            items.append(FlatMoveItem(id: counter, phaseName: phase.phaseName, move: m))
                            counter += 1
                        }
                    }
                    self.flatMoves = items
                    self.currentStepIndex = 0
                } else {
                    self.errorMessage = "Không thể phân tích dữ liệu JSON từ bộ giải."
                }
            }
        }
    }

    private func jumpToBeginning() {
        isPlaying = false
        currentStepIndex = 0
        sceneModel.rebuildCube(size: selectedCubeSize)
    }

    private func jumpToEnd() {
        isPlaying = false
        currentStepIndex = max(0, flatMoves.count - 1)
    }

    private func stepForward() {
        guard currentStepIndex < flatMoves.count else { return }
        let item = flatMoves[currentStepIndex]
        let dur = 0.3 / playbackSpeed
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: item.move.power,
                               duration: dur) {
            if self.currentStepIndex + 1 < self.flatMoves.count {
                self.currentStepIndex += 1
            } else {
                self.isPlaying = false
            }
        }
    }

    private func stepBackward() {
        guard currentStepIndex > 0 else { return }
        currentStepIndex -= 1
        let item = flatMoves[currentStepIndex]
        let inversePower = (item.move.power == 1) ? 3 : ((item.move.power == 3) ? 1 : 2)
        let dur = 0.3 / playbackSpeed
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: inversePower,
                               duration: dur)
    }

    private func jumpToStep(_ targetIdx: Int) {
        guard targetIdx >= 0 && targetIdx < flatMoves.count else { return }
        currentStepIndex = targetIdx
        let item = flatMoves[targetIdx]
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: item.move.power,
                               duration: 0.15)
    }

    private func togglePlayPause() {
        isPlaying.toggle()
        if isPlaying {
            schedulePlaybackLoop()
        }
    }

    private func schedulePlaybackLoop() {
        guard isPlaying, currentStepIndex < flatMoves.count else {
            isPlaying = false
            return
        }
        let item = flatMoves[currentStepIndex]
        let dur = 0.3 / playbackSpeed
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: item.move.power,
                               duration: dur) {
            if self.isPlaying {
                if self.currentStepIndex + 1 < self.flatMoves.count {
                    self.currentStepIndex += 1
                    self.schedulePlaybackLoop()
                } else {
                    self.isPlaying = false
                }
            }
        }
    }
}
