import SwiftUI
import SceneKit
import AVFoundation
import UIKit

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
    @State private var selectedPaletteColorId: Int = 0

    @State private var playbackSpeed: Double = 1.0 // 0.5x to 3.0x
    @State private var isPlaying: Bool = false
    @State private var currentStepIndex: Int = 0 // 0...flatMoves.count (moves applied)

    @State private var initialFacelets: [Int] = ParametricRubikScene.solvedFacelets(size: 3)
    @State private var solveResponse: SolveResponse?
    @State private var flatMoves: [FlatMoveItem] = []
    @State private var isSolving: Bool = false
    @State private var solveRequestId: Int = 0
    @State private var errorMessage: String? = nil

    private let faceGuideNames: [String] = [
        "MẶT 1/6: TRẮNG (U - TRÊN)",
        "MẶT 2/6: VÀNG (D - DƯỚI)",
        "MẶT 3/6: CAM (L - TRÁI)",
        "MẶT 4/6: ĐỎ (R - PHẢI)",
        "MẶT 5/6: XANH LÁ (F - TRƯỚC)",
        "MẶT 6/6: XANH DƯƠNG (B - SAU)"
    ]

    private let colorTagLetters = ["U", "D", "L", "R", "F", "B"]

    private var activeAlgorithmLabel: String {
        switch selectedCubeSize {
        case 2: return "IDA* TỐI ƯU"
        case 3: return "KOCIEMBA 2-PHA"
        case 4: return "REDUCTION 4x4"
        default: return "REDUCTION 5x5"
        }
    }

    public init() {}

    public var body: some View {
        ZStack {
            TactileTheme.bgCarbon.ignoresSafeArea()

            VStack(spacing: 0) {
                topNavigationHeader

                Group {
                    switch currentMode {
                    case .studio3D:
                        studio3DContentView
                    case .cameraScanner:
                        cameraScannerContentView
                    case .manualNetEditor:
                        CubeNetEditorView(
                            scanner: scanner,
                            selectedColorId: $selectedPaletteColorId,
                            onResetCube: {
                                resetCubeToSolved()
                            },
                            onSolveTriggered: {
                                applyEditorStateAndSolve()
                            }
                        )
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)

                if currentMode == .studio3D {
                    algorithmTapeSection
                    bottomTransportDeck
                }
            }
        }
        .onAppear {
            scanner.setCubeSize(selectedCubeSize)
            resetCubeToSolved()
        }
        .onDisappear {
            isPlaying = false
            scanner.stopCamera()
        }
        .onChange(of: scanner.isScanningComplete) { completed in
            if completed && currentMode == .cameraScanner {
                finishCameraScanAndSolve()
            }
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
                    HStack(spacing: 6) {
                        ProgressView()
                            .progressViewStyle(CircularProgressViewStyle(tint: TactileTheme.accentOrange))
                            .scaleEffect(0.7)
                        Text("ĐANG GIẢI...")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundColor(TactileTheme.accentOrange)
                    }
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

            // Mode Selector Tabs
            HStack(spacing: 6) {
                ForEach(AppWorkspaceMode.allCases) { mode in
                    Button(action: {
                        switchMode(to: mode)
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
                        .background(currentMode == mode ? TactileTheme.accentOrange : TactileTheme.bgCarbon.opacity(0.6))
                        .foregroundColor(currentMode == mode ? TactileTheme.bgCarbon : TactileTheme.textSecondary)
                        .cornerRadius(6)
                    }
                }
            }
            .padding(.horizontal, 12)

            // Cube Size & Active Algorithm Indicator
            HStack(spacing: 8) {
                Picker("Size", selection: $selectedCubeSize) {
                    Text("2x2").tag(2)
                    Text("3x3").tag(3)
                    Text("4x4").tag(4)
                    Text("5x5").tag(5)
                }
                .pickerStyle(SegmentedPickerStyle())
                .onChange(of: selectedCubeSize) { newSize in
                    handleCubeSizeChanged(to: newSize)
                }

                Text(activeAlgorithmLabel)
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundColor(TactileTheme.textPrimary)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 6)
                    .background(TactileTheme.bgCarbon.opacity(0.8))
                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(TactileTheme.borderHairline, lineWidth: 1))
                    .cornerRadius(6)
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

            VStack {
                HStack {
                    if let err = errorMessage {
                        Text(err)
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(Color.red)
                            .padding(8)
                            .background(TactileTheme.panelGraphite.opacity(0.92))
                            .overlay(RoundedRectangle(cornerRadius: 6).stroke(Color.red.opacity(0.5), lineWidth: 1))
                            .cornerRadius(6)
                    }
                    Spacer()
                    Button(action: {
                        sceneModel.resetCameraView()
                    }) {
                        Image(systemName: "viewfinder")
                            .font(.system(size: 14, weight: .semibold))
                            .foregroundColor(TactileTheme.textSecondary)
                            .padding(8)
                            .background(TactileTheme.panelGraphite.opacity(0.85))
                            .clipShape(Circle())
                    }
                }
                .padding(.horizontal, 12)
                .padding(.top, 8)

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
                        .padding(.vertical, 9)
                        .background(TactileTheme.panelGraphite.opacity(0.92))
                        .foregroundColor(TactileTheme.textPrimary)
                        .overlay(RoundedRectangle(cornerRadius: 6).stroke(TactileTheme.borderHairline, lineWidth: 1))
                        .cornerRadius(6)
                    }
                    .disabled(isSolving)

                    Button(action: {
                        let currentFacelets = ParametricRubikScene.stateAfterMoves(
                            initialFacelets: initialFacelets,
                            size: selectedCubeSize,
                            moves: flatMoves.map { $0.move },
                            count: currentStepIndex
                        )
                        initialFacelets = currentFacelets
                        scanner.loadFaceletsFlat(currentFacelets, size: selectedCubeSize)
                        sceneModel.rebuildCube(size: selectedCubeSize, facelets: currentFacelets)
                        runSolve(facelets: currentFacelets)
                    }) {
                        HStack(spacing: 6) {
                            Image(systemName: "bolt.fill")
                            Text("GIẢI TỰ ĐỘNG")
                        }
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 9)
                        .background(TactileTheme.accentOrange)
                        .foregroundColor(TactileTheme.bgCarbon)
                        .cornerRadius(6)
                        .shadow(color: TactileTheme.accentOrange.opacity(0.4), radius: 6, x: 0, y: 3)
                    }
                    .disabled(isSolving)
                }
                .padding(.bottom, 10)
            }
        }
    }

    // MARK: - 2B. Camera Scanner Content View
    private var cameraScannerContentView: some View {
        ZStack {
            if scanner.isCameraAuthorized && scanner.cameraError == nil {
                CameraVideoPreview(session: scanner.captureSession)
                    .ignoresSafeArea()
            } else {
                VStack(spacing: 14) {
                    Image(systemName: "camera.viewfinder")
                        .font(.system(size: 48))
                        .foregroundColor(TactileTheme.accentOrange)
                    Text(scanner.isCameraAuthorized ? "CAMERA KHÔNG KHẢ DỤNG" : "CẦN QUYỀN TRUY CẬP CAMERA")
                        .font(.system(.headline, design: .monospaced))
                        .foregroundColor(TactileTheme.textPrimary)
                    Text(scanner.cameraError ?? "Vui lòng cấp quyền Camera trong Cài đặt để quét trực tiếp khối Rubik.")
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(TactileTheme.textSecondary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 32)

                    HStack(spacing: 12) {
                        Button(scanner.isCameraAuthorized ? "THỬ LẠI" : "CẤP QUYỀN / CÀI ĐẶT") {
                            let status = AVCaptureDevice.authorizationStatus(for: .video)
                            if status == .denied || status == .restricted {
                                if let url = URL(string: UIApplication.openSettingsURLString) {
                                    UIApplication.shared.open(url)
                                }
                            } else {
                                scanner.startCamera()
                            }
                        }
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(TactileTheme.accentOrange)
                        .foregroundColor(TactileTheme.bgCarbon)
                        .cornerRadius(8)

                        Button("NHẬP BẢN ĐỒ MÀU") {
                            switchMode(to: .manualNetEditor)
                        }
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(TactileTheme.panelGraphite)
                        .foregroundColor(TactileTheme.textPrimary)
                        .overlay(RoundedRectangle(cornerRadius: 8).stroke(TactileTheme.borderHairline, lineWidth: 1))
                        .cornerRadius(8)
                    }
                }
                .padding()
            }

            if scanner.isCameraAuthorized && scanner.cameraError == nil {
                scannerOverlayControls
            }
        }
    }

    private var scannerOverlayControls: some View {
        let n = scanner.cubeSize
        let reticleSize: CGFloat = 260

        return VStack(spacing: 12) {
            // Top Scanner Banner + Torch + Reset
            HStack(spacing: 8) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(faceGuideNames[min(5, max(0, scanner.currentFaceIndex))])
                        .font(.system(.subheadline, design: .monospaced))
                        .fontWeight(.heavy)
                        .foregroundColor(TactileTheme.accentOrange)
                    Text("Căn chỉnh \(n * n) ô vuông (\(n)x\(n)) khớp khung cam")
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundColor(Color.white.opacity(0.85))
                }
                Spacer()

                Button(action: {
                    scanner.isAutoCaptureEnabled.toggle()
                }) {
                    Text(scanner.isAutoCaptureEnabled ? "AUTO: ON" : "AUTO: OFF")
                        .font(.system(size: 9, weight: .bold, design: .monospaced))
                        .foregroundColor(scanner.isAutoCaptureEnabled ? TactileTheme.bgCarbon : Color.white)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 6)
                        .background(scanner.isAutoCaptureEnabled ? TactileTheme.statusGreen : TactileTheme.panelGraphite.opacity(0.85))
                        .cornerRadius(6)
                }

                Button(action: {
                    scanner.resetAllFaces()
                }) {
                    Image(systemName: "arrow.counterclockwise")
                        .font(.subheadline)
                        .foregroundColor(Color.white)
                        .padding(8)
                        .background(TactileTheme.panelGraphite.opacity(0.85))
                        .clipShape(Circle())
                }

                Button(action: {
                    scanner.toggleTorch()
                }) {
                    Image(systemName: scanner.isTorchOn ? "flashlight.on.fill" : "flashlight.off.fill")
                        .font(.subheadline)
                        .foregroundColor(scanner.isTorchOn ? TactileTheme.accentOrange : Color.white)
                        .padding(8)
                        .background(TactileTheme.panelGraphite.opacity(0.85))
                        .clipShape(Circle())
                }
            }
            .padding(.horizontal, 14)
            .padding(.top, 10)

            Spacer()

            // Center Reticle Grid showing both raw sample & classified color tag
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
                                let clsId = (scanner.liveClassifiedColors.count > idx) ? scanner.liveClassifiedColors[idx] : nil
                                let tileSide = (reticleSize - CGFloat(4 * n)) / CGFloat(n)

                                ZStack {
                                    RoundedRectangle(cornerRadius: 6)
                                        .stroke(Color.white.opacity(0.75), lineWidth: 1.5)
                                        .background(
                                            (sample?.uiColor.opacity(0.35) ?? Color.black.opacity(0.25))
                                                .cornerRadius(6)
                                        )
                                        .frame(width: tileSide, height: tileSide)

                                    if let cid = clsId, cid >= 0, cid < colorTagLetters.count {
                                        Text(colorTagLetters[cid])
                                            .font(.system(size: n >= 4 ? 10 : 12, weight: .heavy, design: .monospaced))
                                            .foregroundColor(Color.black)
                                            .padding(4)
                                            .background(Color(uiColor: ParametricRubikScene.faceColors[cid]))
                                            .clipShape(Circle())
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Spacer()

            // Tappable face progress indicator dots
            HStack(spacing: 10) {
                ForEach(0..<6, id: \.self) { i in
                    Button(action: {
                        scanner.selectFace(i)
                    }) {
                        VStack(spacing: 3) {
                            Text(colorTagLetters[i])
                                .font(.system(size: 10, design: .monospaced))
                                .fontWeight(.bold)
                                .foregroundColor(i == scanner.currentFaceIndex ? TactileTheme.accentOrange : Color.white)
                            Circle()
                                .fill(scanner.faceStatus[i] ? TactileTheme.statusGreen : (i == scanner.currentFaceIndex ? TactileTheme.accentOrange : Color.gray.opacity(0.55)))
                                .frame(width: 16, height: 16)
                                .overlay(
                                    Circle().stroke(i == scanner.currentFaceIndex ? Color.white : Color.clear, lineWidth: 1.5)
                                )
                        }
                        .padding(.horizontal, 4)
                    }
                }
            }
            .padding(.vertical, 8)
            .padding(.horizontal, 14)
            .background(TactileTheme.panelGraphite.opacity(0.9))
            .cornerRadius(20)

            // Capture Button
            Button(action: {
                scanner.captureCurrentFace()
                if scanner.isScanningComplete {
                    finishCameraScanAndSolve()
                }
            }) {
                let completedCount = scanner.faceStatus.filter { $0 }.count
                HStack(spacing: 8) {
                    Image(systemName: "camera.circle.fill")
                        .font(.title2)
                    Text(completedCount >= 5 ? "HOÀN TẤT & GIẢI" : "CHỤP MẶT (\(colorTagLetters[scanner.currentFaceIndex]) • \(completedCount + 1)/6)")
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

    // MARK: - 3. Algorithm Tape Section
    private var algorithmTapeSection: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    if flatMoves.isEmpty {
                        Text("KHỐI ĐÃ Ở TRẠNG THÁI GIẢI XONG")
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(TactileTheme.statusGreen)
                            .padding(.horizontal, 16)
                    } else {
                        ForEach(flatMoves) { item in
                            let isDone = item.id < currentStepIndex
                            let isCurrent = item.id == currentStepIndex
                            Button(action: {
                                jumpToStep(item.id)
                            }) {
                                VStack(spacing: 2) {
                                    Text(item.move.notation)
                                        .font(.system(.body, design: .monospaced))
                                        .fontWeight(.bold)
                                        .foregroundColor(
                                            isCurrent ? TactileTheme.accentOrange :
                                                (isDone ? TactileTheme.statusGreen : TactileTheme.textPrimary)
                                        )

                                    Text(item.phaseName)
                                        .font(.system(size: 8, design: .monospaced))
                                        .foregroundColor(TactileTheme.textSecondary)
                                        .lineLimit(1)
                                }
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(
                                    isCurrent ? TactileTheme.accentOrange.opacity(0.18) :
                                        (isDone ? TactileTheme.statusGreen.opacity(0.10) : TactileTheme.panelGraphite)
                                )
                                .overlay(
                                    RoundedRectangle(cornerRadius: 6)
                                        .stroke(
                                            isCurrent ? TactileTheme.accentOrange :
                                                (isDone ? TactileTheme.statusGreen.opacity(0.5) : TactileTheme.borderHairline),
                                            lineWidth: isCurrent ? 2 : 1
                                        )
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
                let targetId = min(max(0, idx), max(0, flatMoves.count - 1))
                withAnimation {
                    proxy.scrollTo(targetId, anchor: .center)
                }
            }
        }
        .frame(height: 58)
        .background(TactileTheme.panelGraphite)
        .overlay(
            Rectangle().frame(height: 1).foregroundColor(TactileTheme.borderHairline),
            alignment: .top
        )
    }

    // MARK: - 4. Bottom Transport Deck
    private var bottomTransportDeck: some View {
        VStack(spacing: 6) {
            HStack {
                Text("TỐC ĐỘ: \(String(format: "%.2f", playbackSpeed))x")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(TactileTheme.textSecondary)

                Slider(value: $playbackSpeed, in: 0.5...3.0, step: 0.25)
                    .accentColor(TactileTheme.accentOrange)

                Text("\(currentStepIndex)/\(flatMoves.count)")
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundColor(currentStepIndex == flatMoves.count && !flatMoves.isEmpty ? TactileTheme.statusGreen : TactileTheme.accentOrange)
            }
            .padding(.horizontal, 20)

            HStack(spacing: 26) {
                Button(action: { jumpToStep(0) }) {
                    Image(systemName: "backward.end.fill")
                        .font(.title3)
                        .foregroundColor(currentStepIndex > 0 ? TactileTheme.textPrimary : TactileTheme.textSecondary.opacity(0.4))
                }
                .disabled(flatMoves.isEmpty || currentStepIndex == 0)

                Button(action: stepBackward) {
                    Image(systemName: "backward.fill")
                        .font(.title3)
                        .foregroundColor(currentStepIndex > 0 ? TactileTheme.textPrimary : TactileTheme.textSecondary.opacity(0.4))
                }
                .disabled(flatMoves.isEmpty || currentStepIndex == 0 || isPlaying || sceneModel.isAnimating)

                Button(action: togglePlayPause) {
                    Image(systemName: isPlaying ? "pause.circle.fill" : "play.circle.fill")
                        .font(.system(size: 40))
                        .foregroundColor(flatMoves.isEmpty ? TactileTheme.textSecondary.opacity(0.4) : TactileTheme.accentOrange)
                }
                .disabled(flatMoves.isEmpty)

                Button(action: stepForward) {
                    Image(systemName: "forward.fill")
                        .font(.title3)
                        .foregroundColor(currentStepIndex < flatMoves.count ? TactileTheme.textPrimary : TactileTheme.textSecondary.opacity(0.4))
                }
                .disabled(flatMoves.isEmpty || currentStepIndex >= flatMoves.count || isPlaying || sceneModel.isAnimating)

                Button(action: { jumpToStep(flatMoves.count) }) {
                    Image(systemName: "forward.end.fill")
                        .font(.title3)
                        .foregroundColor(currentStepIndex < flatMoves.count ? TactileTheme.textPrimary : TactileTheme.textSecondary.opacity(0.4))
                }
                .disabled(flatMoves.isEmpty || currentStepIndex >= flatMoves.count)
            }
            .padding(.bottom, 10)
        }
        .background(TactileTheme.panelGraphite)
    }

    // MARK: - Actions & State Transitions
    private func switchMode(to mode: AppWorkspaceMode) {
        isPlaying = false
        sceneModel.cancelAnimations()
        if currentMode == .cameraScanner && mode != .cameraScanner {
            scanner.stopCamera()
        } else if mode == .cameraScanner && currentMode != .cameraScanner {
            scanner.startCamera()
        }
        withAnimation(.easeInOut(duration: 0.2)) {
            currentMode = mode
        }
    }

    private func handleCubeSizeChanged(to newSize: Int) {
        isPlaying = false
        solveRequestId += 1
        isSolving = false
        scanner.setCubeSize(newSize)
        resetCubeToSolved()
    }

    private func resetCubeToSolved() {
        isPlaying = false
        let solved = ParametricRubikScene.solvedFacelets(size: selectedCubeSize)
        initialFacelets = solved
        scanner.loadFaceletsFlat(solved, size: selectedCubeSize)
        sceneModel.rebuildCube(size: selectedCubeSize, facelets: solved)
        errorMessage = nil
        solveResponse = nil
        flatMoves = []
        currentStepIndex = 0
    }

    private func applyEditorStateAndSolve() {
        let edited = scanner.getFlatFacelets()
        initialFacelets = edited
        sceneModel.rebuildCube(size: selectedCubeSize, facelets: edited)
        switchMode(to: .studio3D)
        runSolve(facelets: edited)
    }

    private func finishCameraScanAndSolve() {
        guard currentMode == .cameraScanner else { return }
        let scanned = scanner.getFlatFacelets()
        scanner.stopCamera()
        initialFacelets = scanned
        sceneModel.rebuildCube(size: selectedCubeSize, facelets: scanned)
        withAnimation(.easeInOut(duration: 0.2)) {
            currentMode = .studio3D
        }
        runSolve(facelets: scanned)
    }

    private func scrambleCube() {
        isPlaying = false
        sceneModel.cancelAnimations()

        let n = selectedCubeSize
        let allowedAxes: [Int] = (n == 2) ? [0, 3, 4] : [0, 1, 2, 3, 4, 5]
        let powers: [Int] = [1, 2, 3]
        let count = (n == 2) ? 11 : ((n == 3) ? 20 : 28)

        var scrambled = ParametricRubikScene.solvedFacelets(size: n)
        var lastAxis = -1
        for _ in 0..<count {
            var axis = allowedAxes.randomElement() ?? 0
            while axis == lastAxis {
                axis = allowedAxes.randomElement() ?? 0
            }
            lastAxis = axis
            let power = powers.randomElement() ?? 1
            let sliceMask: Int = (n >= 4 && Bool.random()) ? 0x03 : 0x01
            ParametricRubikScene.applyMove(to: &scrambled, size: n, axis: axis, sliceMask: sliceMask, power: power)
        }

        initialFacelets = scrambled
        currentStepIndex = 0
        scanner.loadFaceletsFlat(scrambled, size: n)
        sceneModel.rebuildCube(size: n, facelets: scrambled)
        runSolve(facelets: scrambled)
    }

    private func runSolve(facelets: [Int]) {
        isPlaying = false
        solveRequestId += 1
        let activeReq = solveRequestId
        let sizeSnapshot = selectedCubeSize
        let csv = facelets.map(String.init).joined(separator: ",")

        isSolving = true
        errorMessage = nil

        Task.detached(priority: .userInitiated) {
            guard let cStr = CubeEngineBridge.solveCubeJsonC(Int32(sizeSnapshot), 0, csv) else {
                await MainActor.run {
                    guard self.solveRequestId == activeReq else { return }
                    self.errorMessage = "Không thể kết nối C++ Solver Bridge."
                    self.isSolving = false
                }
                return
            }

            let jsonString = String(cString: cStr)
            await MainActor.run {
                guard self.solveRequestId == activeReq else { return }
                self.isSolving = false
                if let resp = SolveResponse.parse(jsonString: jsonString) {
                    self.solveResponse = resp
                    if !resp.isValid {
                        self.flatMoves = []
                        self.currentStepIndex = 0
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
                    self.sceneModel.rebuildCube(size: sizeSnapshot, facelets: self.initialFacelets)
                } else {
                    self.errorMessage = "Không thể phân tích dữ liệu JSON từ bộ giải."
                }
            }
        }
    }

    private func stepForward() {
        guard !isPlaying, !sceneModel.isAnimating, currentStepIndex < flatMoves.count else { return }
        let item = flatMoves[currentStepIndex]
        let dur = 0.3 / playbackSpeed
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: item.move.power,
                               duration: dur) {
            if self.currentStepIndex < self.flatMoves.count {
                self.currentStepIndex += 1
            }
        }
    }

    private func stepBackward() {
        guard !isPlaying, !sceneModel.isAnimating, currentStepIndex > 0 else { return }
        let prevIdx = currentStepIndex - 1
        let item = flatMoves[prevIdx]
        let inversePower = (item.move.power == 1) ? 3 : ((item.move.power == 3) ? 1 : 2)
        let dur = 0.3 / playbackSpeed
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: inversePower,
                               duration: dur) {
            if self.currentStepIndex > 0 {
                self.currentStepIndex = prevIdx
            }
        }
    }

    private func jumpToStep(_ targetStep: Int) {
        isPlaying = false
        let nextStep = max(0, min(targetStep, flatMoves.count))
        let state = ParametricRubikScene.stateAfterMoves(
            initialFacelets: initialFacelets,
            size: selectedCubeSize,
            moves: flatMoves.map { $0.move },
            count: nextStep
        )
        sceneModel.rebuildCube(size: selectedCubeSize, facelets: state)
        currentStepIndex = nextStep
    }

    private func togglePlayPause() {
        if flatMoves.isEmpty { return }
        if isPlaying {
            isPlaying = false
            return
        }
        if currentStepIndex >= flatMoves.count {
            jumpToStep(0)
        }
        isPlaying = true
        schedulePlaybackLoop()
    }

    private func schedulePlaybackLoop() {
        guard isPlaying, currentStepIndex < flatMoves.count else {
            isPlaying = false
            return
        }
        guard !sceneModel.isAnimating else { return }

        let item = flatMoves[currentStepIndex]
        let dur = 0.3 / playbackSpeed
        sceneModel.animateMove(axis: item.move.axis,
                               sliceMask: item.move.sliceMask,
                               power: item.move.power,
                               duration: dur) {
            if self.currentStepIndex < self.flatMoves.count {
                self.currentStepIndex += 1
            }
            if self.isPlaying && self.currentStepIndex < self.flatMoves.count {
                self.schedulePlaybackLoop()
            } else {
                self.isPlaying = false
            }
        }
    }
}
