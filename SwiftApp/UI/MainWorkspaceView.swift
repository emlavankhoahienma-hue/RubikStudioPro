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

    private struct FaceScanGuide {
        let colorId: Int
        let colorName: String
        let shortName: String
        let holdTip: String
        let topNeighborId: Int
        let bottomNeighborId: Int
    }

    // Indexed by canonical face index: 0:U(White), 1:D(Yellow), 2:L(Orange), 3:R(Red), 4:F(Green), 5:B(Blue)
    private let faceGuides: [FaceScanGuide] = [
        FaceScanGuide(
            colorId: 0,
            colorName: "TRẮNG",
            shortName: "TR",
            holdTip: "Từ mặt Xanh lá, ngửa mặt TRẮNG từ trên xuống thẳng camera",
            topNeighborId: 5,
            bottomNeighborId: 4
        ),
        FaceScanGuide(
            colorId: 1,
            colorName: "VÀNG",
            shortName: "VÀ",
            holdTip: "Từ mặt Xanh lá, gập mặt VÀNG từ dưới lên thẳng camera",
            topNeighborId: 4,
            bottomNeighborId: 5
        ),
        FaceScanGuide(
            colorId: 2,
            colorName: "CAM",
            shortName: "CA",
            holdTip: "Xoay tiếp sang trái 90° từ mặt Xanh dương, giữ mặt Trắng ở trên",
            topNeighborId: 0,
            bottomNeighborId: 1
        ),
        FaceScanGuide(
            colorId: 3,
            colorName: "ĐỎ",
            shortName: "ĐỎ",
            holdTip: "Từ mặt Xanh lá, xoay khối sang trái 90° (giữ mặt Trắng ở trên)",
            topNeighborId: 0,
            bottomNeighborId: 1
        ),
        FaceScanGuide(
            colorId: 4,
            colorName: "XANH LÁ",
            shortName: "LÁ",
            holdTip: "Hướng mặt Xanh lá thẳng camera, để mặt TRẮNG ở phía trên",
            topNeighborId: 0,
            bottomNeighborId: 1
        ),
        FaceScanGuide(
            colorId: 5,
            colorName: "XANH DƯƠNG",
            shortName: "DU",
            holdTip: "Xoay tiếp sang trái 90° từ mặt Đỏ, giữ mặt Trắng ở trên",
            topNeighborId: 0,
            bottomNeighborId: 1
        )
    ]

    private let colorTagLetters = ["TR", "VÀ", "CA", "ĐỎ", "LÁ", "DU"]

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
                HStack(alignment: .top, spacing: 8) {
                    if let err = errorMessage {
                        VStack(alignment: .leading, spacing: 6) {
                            Text(err)
                                .font(.system(.caption, design: .monospaced))
                                .foregroundColor(Color.red)
                            Button("Sửa ở Bản Đồ Màu") {
                                switchMode(to: .manualNetEditor)
                            }
                            .font(.system(size: 11, weight: .bold, design: .monospaced))
                            .foregroundColor(TactileTheme.bgCarbon)
                            .padding(.horizontal, 10)
                            .padding(.vertical, 5)
                            .background(TactileTheme.accentOrange)
                            .cornerRadius(5)
                        }
                        .padding(10)
                        .background(TactileTheme.panelGraphite.opacity(0.94))
                        .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color.red.opacity(0.55), lineWidth: 1))
                        .cornerRadius(8)
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
        .onChange(of: scanner.isScanningComplete) { _, complete in
            if complete {
                finishCameraScanAndSolve()
            }
        }
    }

    private var scannerOverlayControls: some View {
        let n = scanner.cubeSize
        let reticleSize: CGFloat = 246
        let activeFace = min(5, max(0, scanner.currentFaceIndex))
        let guide = faceGuides[activeFace]
        let topGuide = faceGuides[guide.topNeighborId]
        let bottomGuide = faceGuides[guide.bottomNeighborId]
        let completedCount = scanner.faceStatus.filter { $0 }.count
        let stepPos = (CIELABScanner.naturalScanOrder.firstIndex(of: activeFace) ?? 0) + 1

        let isOddCube = (n % 2 == 1)
        let centerIdx = (n * n) / 2
        let detectedCenterId: Int? = (isOddCube && scanner.liveClassifiedColors.count == n * n)
            ? scanner.liveClassifiedColors[centerIdx]
            : nil
        let isCenterMatched = (detectedCenterId == activeFace)

        return VStack(spacing: 8) {
            // Top Beginner-Friendly Center Color Card + Actions
            VStack(spacing: 6) {
                HStack(spacing: 10) {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color(uiColor: ParametricRubikScene.faceColors[guide.colorId]))
                        .frame(width: 40, height: 40)
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(Color.white, lineWidth: 2)
                        )

                    VStack(alignment: .leading, spacing: 2) {
                        Text(isOddCube
                             ? "CHỤP MẶT TÂM \(guide.colorName) (\(stepPos)/6)"
                             : "CHỤP MẶT \(guide.colorName) (\(stepPos)/6)")
                            .font(.system(.subheadline, design: .monospaced))
                            .fontWeight(.heavy)
                            .foregroundColor(TactileTheme.accentOrange)
                        Text(guide.holdTip)
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundColor(Color.white.opacity(0.92))
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    Spacer(minLength: 4)

                    VStack(spacing: 6) {
                        HStack(spacing: 6) {
                            Button(action: {
                                scanner.resetAllFaces()
                            }) {
                                Image(systemName: "arrow.counterclockwise")
                                    .font(.caption)
                                    .foregroundColor(Color.white)
                                    .padding(7)
                                    .background(Color.black.opacity(0.5))
                                    .clipShape(Circle())
                            }

                            Button(action: {
                                scanner.toggleTorch()
                            }) {
                                Image(systemName: scanner.isTorchOn ? "flashlight.on.fill" : "flashlight.off.fill")
                                    .font(.caption)
                                    .foregroundColor(scanner.isTorchOn ? TactileTheme.accentOrange : Color.white)
                                    .padding(7)
                                    .background(Color.black.opacity(0.5))
                                    .clipShape(Circle())
                            }
                        }

                        Button(action: {
                            scanner.isAutoCaptureEnabled.toggle()
                        }) {
                            Text(scanner.isAutoCaptureEnabled ? "AUTO: ON" : "AUTO: OFF")
                                .font(.system(size: 9, weight: .bold, design: .monospaced))
                                .foregroundColor(scanner.isAutoCaptureEnabled ? TactileTheme.bgCarbon : Color.white)
                                .padding(.horizontal, 7)
                                .padding(.vertical, 4)
                                .background(scanner.isAutoCaptureEnabled ? TactileTheme.statusGreen : Color.black.opacity(0.5))
                                .cornerRadius(5)
                        }
                    }
                }

                // Live center verification banner for 3x3 & 5x5
                if isOddCube {
                    if let det = detectedCenterId, det >= 0, det < faceGuides.count {
                        HStack(spacing: 6) {
                            Image(systemName: isCenterMatched ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                                .font(.caption2)
                            Text(isCenterMatched
                                 ? "Đúng mặt tâm \(guide.colorName)! Căn \(n * n) ô khít khung rồi bấm CHỤP"
                                 : "Ô giữa đang thấy màu \(faceGuides[det].colorName) — xoay tìm tâm \(guide.colorName)")
                                .font(.system(size: 10, weight: .bold, design: .monospaced))
                        }
                        .foregroundColor(isCenterMatched ? TactileTheme.statusGreen : Color.yellow)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                } else {
                    Text("Cỡ \(n)x\(n) không có tâm cố định — giữ đúng chiều Trên/Dưới bên dưới")
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundColor(Color.white.opacity(0.8))
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .padding(10)
            .background(TactileTheme.panelGraphite.opacity(0.92))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(TactileTheme.borderHairline, lineWidth: 1))
            .cornerRadius(10)
            .padding(.horizontal, 12)
            .padding(.top, 8)

            Spacer(minLength: 4)

            // Top Orientation Neighbor Badge
            HStack(spacing: 6) {
                Image(systemName: "arrow.up")
                    .font(.caption2)
                Circle()
                    .fill(Color(uiColor: ParametricRubikScene.faceColors[topGuide.colorId]))
                    .frame(width: 10, height: 10)
                    .overlay(Circle().stroke(Color.white, lineWidth: 1))
                Text("Cạnh TRÊN hướng về mặt \(topGuide.colorName)")
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
            }
            .foregroundColor(Color.white)
            .padding(.horizontal, 12)
            .padding(.vertical, 5)
            .background(Color.black.opacity(0.65))
            .cornerRadius(12)

            // Center Reticle Grid showing raw sample + color badge
            ZStack {
                RoundedRectangle(cornerRadius: 16)
                    .stroke(isOddCube && isCenterMatched ? TactileTheme.statusGreen : TactileTheme.accentOrange, lineWidth: 3)
                    .frame(width: reticleSize, height: reticleSize)
                    .shadow(color: TactileTheme.accentOrange.opacity(0.45), radius: 10)

                VStack(spacing: 3) {
                    ForEach(0..<n, id: \.self) { r in
                        HStack(spacing: 3) {
                            ForEach(0..<n, id: \.self) { c in
                                let idx = r * n + c
                                let isCenterCell = isOddCube && r == n / 2 && c == n / 2
                                let sample = (scanner.liveSampledColors.count > idx) ? scanner.liveSampledColors[idx] : nil
                                let clsId = (scanner.liveClassifiedColors.count > idx) ? scanner.liveClassifiedColors[idx] : nil
                                let tileSide = (reticleSize - CGFloat(4 * n)) / CGFloat(n)

                                ZStack {
                                    RoundedRectangle(cornerRadius: 6)
                                        .stroke(isCenterCell ? TactileTheme.accentOrange : Color.white.opacity(0.75),
                                                lineWidth: isCenterCell ? 2.5 : 1.5)
                                        .background(
                                            (sample?.uiColor.opacity(0.32) ?? Color.black.opacity(0.25))
                                                .cornerRadius(6)
                                        )
                                        .frame(width: tileSide, height: tileSide)

                                    if let cid = clsId, cid >= 0, cid < colorTagLetters.count {
                                        Text(colorTagLetters[cid])
                                            .font(.system(size: n >= 4 ? 9 : 11, weight: .heavy, design: .monospaced))
                                            .foregroundColor(Color.black)
                                            .padding(.horizontal, 5)
                                            .padding(.vertical, 3)
                                            .background(Color(uiColor: ParametricRubikScene.faceColors[cid]))
                                            .clipShape(Capsule())
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Bottom Orientation Neighbor Badge
            HStack(spacing: 6) {
                Image(systemName: "arrow.down")
                    .font(.caption2)
                Circle()
                    .fill(Color(uiColor: ParametricRubikScene.faceColors[bottomGuide.colorId]))
                    .frame(width: 10, height: 10)
                    .overlay(Circle().stroke(Color.white, lineWidth: 1))
                Text("Cạnh DƯỚI hướng về mặt \(bottomGuide.colorName)")
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
            }
            .foregroundColor(Color.white)
            .padding(.horizontal, 12)
            .padding(.vertical, 5)
            .background(Color.black.opacity(0.65))
            .cornerRadius(12)

            Spacer(minLength: 4)

            // Tappable face progress bar in natural beginner scan order
            HStack(spacing: 8) {
                ForEach(CIELABScanner.naturalScanOrder, id: \.self) { i in
                    let g = faceGuides[i]
                    let isDone = scanner.faceStatus[i]
                    let isCurrent = (i == activeFace)
                    Button(action: {
                        scanner.selectFace(i)
                    }) {
                        VStack(spacing: 3) {
                            Text(g.shortName)
                                .font(.system(size: 9, weight: .heavy, design: .monospaced))
                                .foregroundColor(isCurrent ? TactileTheme.accentOrange : Color.white)
                            ZStack {
                                Circle()
                                    .fill(Color(uiColor: ParametricRubikScene.faceColors[g.colorId]))
                                    .frame(width: 20, height: 20)
                                    .overlay(
                                        Circle().stroke(isCurrent ? TactileTheme.accentOrange : (isDone ? TactileTheme.statusGreen : Color.white.opacity(0.4)),
                                                        lineWidth: isCurrent ? 2.5 : 1.5)
                                    )
                                if isDone {
                                    Image(systemName: "checkmark")
                                        .font(.system(size: 10, weight: .black))
                                        .foregroundColor(Color.black)
                                }
                            }
                        }
                        .padding(.horizontal, 5)
                        .padding(.vertical, 3)
                        .background(isCurrent ? TactileTheme.accentOrange.opacity(0.18) : Color.clear)
                        .cornerRadius(8)
                    }
                }
            }
            .padding(.vertical, 6)
            .padding(.horizontal, 12)
            .background(TactileTheme.panelGraphite.opacity(0.92))
            .cornerRadius(20)

            // Capture Button
            Button(action: {
                scanner.captureCurrentFace()
                if scanner.isScanningComplete {
                    finishCameraScanAndSolve()
                }
            }) {
                let willFinish = completedCount >= 5 && !scanner.faceStatus[activeFace]
                HStack(spacing: 8) {
                    Image(systemName: "camera.circle.fill")
                        .font(.title2)
                    Text(willFinish ? "CHỤP MẶT \(guide.colorName) & GIẢI NGAY" : "CHỤP MẶT \(guide.colorName) (\(completedCount + 1)/6)")
                        .font(.system(.subheadline, design: .monospaced))
                        .fontWeight(.bold)
                }
                .foregroundColor(TactileTheme.bgCarbon)
                .padding(.horizontal, 24)
                .padding(.vertical, 12)
                .background(TactileTheme.accentOrange)
                .cornerRadius(8)
                .shadow(color: TactileTheme.accentOrange.opacity(0.4), radius: 8, x: 0, y: 4)
            }
            .padding(.bottom, 12)
        }
    }

    // MARK: - 3. Algorithm Tape Section
    private var algorithmTapeSection: some View {
        VStack(spacing: 0) {
            if !flatMoves.isEmpty {
                HStack(spacing: 6) {
                    if currentStepIndex < flatMoves.count {
                        let current = flatMoves[currentStepIndex]
                        Text("BƯỚC \(currentStepIndex + 1)/\(flatMoves.count):")
                            .font(.system(size: 10, weight: .heavy, design: .monospaced))
                            .foregroundColor(TactileTheme.accentOrange)
                        Text(current.move.vietnameseHint)
                            .font(.system(size: 10, weight: .semibold, design: .monospaced))
                            .foregroundColor(TactileTheme.textPrimary)
                            .lineLimit(1)
                    } else {
                        Text("✓ ĐÃ HOÀN THÀNH TẤT CẢ \(flatMoves.count) BƯỚC GIẢI!")
                            .font(.system(size: 10, weight: .heavy, design: .monospaced))
                            .foregroundColor(TactileTheme.statusGreen)
                    }
                    Spacer()
                }
                .padding(.horizontal, 14)
                .padding(.top, 6)
            }

            ScrollViewReader { proxy in
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        if flatMoves.isEmpty {
                            Text("KHỐI ĐÃ Ở TRẠNG THÁI GIẢI XONG — BẤM XÁO KHỐI HOẶC QUÉT CAMERA")
                                .font(.system(size: 11, design: .monospaced))
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
                                    .padding(.vertical, 5)
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
            .frame(height: 56)
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
