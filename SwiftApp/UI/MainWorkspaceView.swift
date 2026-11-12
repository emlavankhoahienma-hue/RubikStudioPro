import SwiftUI
import SceneKit
import AVFoundation

public enum ViewportMode {
    case scene3D
    case cameraScanner
}

public struct FlatMoveItem: Identifiable, Sendable {
    public let id: Int
    public let phaseName: String
    public let move: MoveModel
}

public struct MainWorkspaceView: View {
    @StateObject private var sceneModel = ParametricRubikScene(cubeSize: 3)
    @StateObject private var scanner = CIELABScanner()

    @State private var selectedCubeSize: Int = 3
    @State private var selectedMethodId: Int = 0
    @State private var viewportMode: ViewportMode = .scene3D
    @State private var isCalibratingLUT: Bool = false

    @State private var playbackSpeed: Double = 1.0 // 0.25x to 2.0x
    @State private var isPlaying: Bool = false
    @State private var currentStepIndex: Int = 0

    @State private var solveResponse: SolveResponse?
    @State private var flatMoves: [FlatMoveItem] = []
    @State private var timerSubscription: Timer?

    public init() {}

    public var body: some View {
        ZStack {
            TactileTheme.bgCarbon.ignoresSafeArea()

            VStack(spacing: 0) {
                // Top Control Bar
                topControlBar
                    .background(TactileTheme.panelGraphite)
                    .overlay(
                        Rectangle()
                            .frame(height: 1)
                            .foregroundColor(TactileTheme.borderHairline),
                        alignment: .bottom
                    )

                // Calibration Banner
                if isCalibratingLUT {
                    HStack(spacing: 8) {
                        ProgressView()
                            .progressViewStyle(CircularProgressViewStyle(tint: TactileTheme.accentOrange))
                            .scaleEffect(0.8)
                        Text("CALIBRATING LUT TABLES...")
                            .font(.system(.caption, design: .monospaced))
                            .fontWeight(.bold)
                            .foregroundColor(TactileTheme.accentOrange)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 4)
                    .background(TactileTheme.panelGraphite)
                }

                // Center Viewport (60% height)
                GeometryReader { geo in
                    ZStack {
                        if viewportMode == .scene3D {
                            SceneView(
                                scene: sceneModel.scene,
                                pointOfView: sceneModel.cameraNode,
                                options: [.allowsCameraControl, .autoenablesDefaultLighting]
                            )
                        } else {
                            cameraScannerOverlay
                        }

                        // Top-right mini overlay toggle
                        VStack {
                            HStack {
                                Spacer()
                                Button(action: toggleViewportMode) {
                                    HStack(spacing: 6) {
                                        Image(systemName: viewportMode == .scene3D ? "camera.viewfinder" : "cube.transparent")
                                        Text(viewportMode == .scene3D ? "SCAN" : "3D CUBE")
                                            .font(.system(.caption, design: .monospaced))
                                            .fontWeight(.bold)
                                    }
                                    .padding(.horizontal, 12)
                                    .padding(.vertical, 6)
                                    .background(TactileTheme.panelGraphite.opacity(0.85))
                                    .foregroundColor(TactileTheme.accentOrange)
                                    .overlay(
                                        RoundedRectangle(cornerRadius: 6)
                                            .stroke(TactileTheme.accentOrange, lineWidth: 1)
                                    )
                                    .cornerRadius(6)
                                }
                                .padding(12)
                            }
                            Spacer()
                        }
                    }
                    .frame(width: geo.size.width, height: geo.size.height)
                }
                .frame(maxHeight: UIScreen.main.bounds.height * 0.58)

                // Algorithm Tape
                algorithmTapeView
                    .frame(height: 72)
                    .background(TactileTheme.panelGraphite)
                    .overlay(
                        Rectangle()
                            .frame(height: 1)
                            .foregroundColor(TactileTheme.borderHairline),
                        alignment: .top
                    )
                    .overlay(
                        Rectangle()
                            .frame(height: 1)
                            .foregroundColor(TactileTheme.borderHairline),
                        alignment: .bottom
                    )

                // Bottom Transport Deck
                bottomTransportDeck
                    .background(TactileTheme.panelGraphite)
            }
        }
        .onAppear {
            calibrateTablesInBackground()
            triggerSampleSolve()
        }
    }

    // Top Control Bar
    private var topControlBar: some View {
        VStack(spacing: 8) {
            HStack {
                Text("RUBIK STUDIO PRO")
                    .font(.system(.headline, design: .monospaced))
                    .fontWeight(.heavy)
                    .foregroundColor(TactileTheme.textPrimary)

                Spacer()

                if let res = solveResponse {
                    Text("\(res.totalMoves) MOVES • \(String(format: "%.1f", res.solveTimeMs))ms")
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(TactileTheme.textSecondary)
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 8)

            HStack(spacing: 12) {
                // Cube Size Selector
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
                    triggerSampleSolve()
                }

                // Method Selector
                Picker("Method", selection: $selectedMethodId) {
                    Text("OPTIMAL").tag(0)
                    Text(selectedCubeSize == 2 ? "ORTEGA" : (selectedCubeSize == 4 ? "YAU" : (selectedCubeSize == 5 ? "FREESLICE" : "CFOP"))).tag(1)
                    Text(selectedCubeSize == 2 ? "CLL" : (selectedCubeSize == 3 ? "ROUX" : "SPEED 2")).tag(2)
                }
                .pickerStyle(SegmentedPickerStyle())
                .onChange(of: selectedMethodId) { _ in
                    triggerSampleSolve()
                }
            }
            .padding(.horizontal, 16)
            .padding(.bottom, 8)
        }
    }

    // Camera Scanner View with Mini 3D hint widget
    private var cameraScannerOverlay: some View {
        ZStack {
            TactileTheme.bgCarbon

            VStack(spacing: 16) {
                // Scanner Grid Box
                ZStack {
                    RoundedRectangle(cornerRadius: 12)
                        .stroke(TactileTheme.borderHairline, lineWidth: 2)
                        .frame(width: 260, height: 260)

                    let n = selectedCubeSize
                    VStack(spacing: 2) {
                        ForEach(0..<n, id: \.self) { r in
                            HStack(spacing: 2) {
                                ForEach(0..<n, id: \.self) { c in
                                    Rectangle()
                                        .fill(TactileTheme.panelGraphite.opacity(0.85))
                                        .frame(width: CGFloat(240 / n), height: CGFloat(240 / n))
                                        .border(TactileTheme.borderHairline, width: 1)
                                }
                            }
                        }
                    }
                }

                // Face scanning progress
                HStack(spacing: 8) {
                    let faceNames = ["U", "D", "L", "R", "F", "B"]
                    ForEach(0..<6, id: \.self) { i in
                        VStack(spacing: 2) {
                            Text(faceNames[i])
                                .font(.system(.caption2, design: .monospaced))
                                .foregroundColor(i == scanner.currentFaceIndex ? TactileTheme.accentOrange : TactileTheme.textSecondary)
                            Circle()
                                .fill(scanner.faceStatus[i] ? TactileTheme.statusGreen : (i == scanner.currentFaceIndex ? TactileTheme.accentOrange : TactileTheme.borderHairline))
                                .frame(width: 12, height: 12)
                        }
                    }
                }

                Button(action: {
                    scanner.captureCurrentFace()
                    if scanner.isScanningComplete {
                        if let csv = scanner.computeClassifiedColorsCsv() {
                            runSolve(csv: csv)
                            viewportMode = .scene3D
                        }
                    }
                }) {
                    Text("CAPTURE FACE (\(scanner.currentFaceIndex + 1)/6)")
                        .font(.system(.body, design: .monospaced))
                        .fontWeight(.bold)
                        .foregroundColor(TactileTheme.bgCarbon)
                        .padding(.horizontal, 24)
                        .padding(.vertical, 10)
                        .background(TactileTheme.accentOrange)
                        .cornerRadius(8)
                }
            }
        }
    }

    // Algorithm Tape View
    private var algorithmTapeView: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    if flatMoves.isEmpty {
                        Text("NO MOVES - CUBE IS SOLVED")
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(TactileTheme.textSecondary)
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
                                .padding(.vertical, 8)
                                .background(currentStepIndex == item.id ? TactileTheme.accentOrange.opacity(0.15) : TactileTheme.bgCarbon)
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
            }
            .onChange(of: currentStepIndex) { idx in
                withAnimation {
                    proxy.scrollTo(idx, anchor: .center)
                }
            }
        }
    }

    // Bottom Transport Deck
    private var bottomTransportDeck: some View {
        VStack(spacing: 12) {
            // Speed slider
            HStack {
                Text("SPEED: \(String(format: "%.2f", playbackSpeed))x")
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(TactileTheme.textSecondary)

                Slider(value: $playbackSpeed, in: 0.25...2.0, step: 0.25)
                    .accentColor(TactileTheme.accentOrange)
            }
            .padding(.horizontal, 24)
            .padding(.top, 8)

            // Transport Control Buttons
            HStack(spacing: 20) {
                // First step [|<]
                Button(action: jumpToBeginning) {
                    Image(systemName: "backward.end.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }

                // Previous step [<]
                Button(action: stepBackward) {
                    Image(systemName: "backward.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }

                // PLAY / PAUSE
                Button(action: togglePlayPause) {
                    Image(systemName: isPlaying ? "pause.circle.fill" : "play.circle.fill")
                        .font(.system(size: 44))
                        .foregroundColor(TactileTheme.accentOrange)
                }

                // Next step [>]
                Button(action: stepForward) {
                    Image(systemName: "forward.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }

                // Last step [>|]
                Button(action: jumpToEnd) {
                    Image(systemName: "forward.end.fill")
                        .font(.title3)
                        .foregroundColor(TactileTheme.textPrimary)
                }
            }
            .padding(.bottom, 16)
        }
    }

    // Actions & Logic
    private func toggleViewportMode() {
        viewportMode = (viewportMode == .scene3D) ? .cameraScanner : .scene3D
    }

    private func calibrateTablesInBackground() {
        isCalibratingLUT = true
        Task.detached(priority: .userInitiated) {
            let emptyCsv = Array(repeating: "0", count: 54).joined(separator: ",")
            _ = CubeEngineBridge.solveCubeJsonC(3, 0, emptyCsv)
            await MainActor.run {
                self.isCalibratingLUT = false
            }
        }
    }

    private func triggerSampleSolve() {
        // Generate a standard test scrambled CSV for current cube size
        let total = 6 * selectedCubeSize * selectedCubeSize
        var colors: [String] = []
        for face in 0..<6 {
            for _ in 0..<(selectedCubeSize * selectedCubeSize) {
                colors.append(String(face))
            }
        }
        // Deterministic scramble
        colors.swapAt(1, 4)
        colors.swapAt(5, 12)
        if total > 50 {
            colors.swapAt(18, 30)
            colors.swapAt(22, 40)
        }

        runSolve(csv: colors.joined(separator: ","))
    }

    private func runSolve(csv: String) {
        if let cStr = CubeEngineBridge.solveCubeJsonC(Int32(selectedCubeSize),
                                                      Int32(selectedMethodId),
                                                      csv) {
            let swiftJson = String(cString: cStr)
            if let resp = SolveResponse.parse(jsonString: swiftJson) {
                self.solveResponse = resp
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
        let dur = 0.35 / playbackSpeed
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
        let dur = 0.35 / playbackSpeed
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
                              duration: 0.2)
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
        let dur = 0.35 / playbackSpeed
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
