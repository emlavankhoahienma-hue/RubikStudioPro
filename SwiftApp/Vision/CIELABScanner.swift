import Foundation
@preconcurrency import AVFoundation
import CoreMotion
import UIKit
import Combine
import CoreVideo
import QuartzCore
import SwiftUI

public struct LabColor: Equatable, Sendable {
    public var L: Double
    public var a: Double
    public var b: Double

    public init(L: Double, a: Double, b: Double) {
        self.L = L
        self.a = a
        self.b = b
    }

    // Standard Delta E 2000 (CIE dE2000) color difference formula (Sharma et al. 2005)
    public func deltaE2000(to other: LabColor) -> Double {
        let kL = 1.0, kC = 1.0, kH = 1.0
        let avgL = (self.L + other.L) / 2.0
        let c1 = sqrt(self.a * self.a + self.b * self.b)
        let c2 = sqrt(other.a * other.a + other.b * other.b)
        let avgC = (c1 + c2) / 2.0

        let avgC7 = pow(avgC, 7.0)
        let p25_7 = pow(25.0, 7.0)
        let g = 0.5 * (1.0 - sqrt(avgC7 / (avgC7 + p25_7)))
        let a1p = (1.0 + g) * self.a
        let a2p = (1.0 + g) * other.a

        let c1p = sqrt(a1p * a1p + self.b * self.b)
        let c2p = sqrt(a2p * a2p + other.b * other.b)
        let avgCp = (c1p + c2p) / 2.0

        func hueAngle(_ bVal: Double, _ aVal: Double) -> Double {
            if bVal == 0.0 && aVal == 0.0 { return 0.0 }
            let deg = atan2(bVal, aVal) * 180.0 / .pi
            return deg >= 0.0 ? deg : (deg + 360.0)
        }

        let h1p = hueAngle(self.b, a1p)
        let h2p = hueAngle(other.b, a2p)

        let cProd = c1p * c2p
        var dkp = 0.0
        var avgHp = h1p + h2p
        if cProd > 0.0 {
            let diff = h2p - h1p
            if abs(diff) <= 180.0 {
                dkp = diff
                avgHp = (h1p + h2p) / 2.0
            } else if diff > 180.0 {
                dkp = diff - 360.0
                avgHp = (h1p + h2p) < 360.0 ? ((h1p + h2p + 360.0) / 2.0) : ((h1p + h2p - 360.0) / 2.0)
            } else {
                dkp = diff + 360.0
                avgHp = (h1p + h2p) < 360.0 ? ((h1p + h2p + 360.0) / 2.0) : ((h1p + h2p - 360.0) / 2.0)
            }
        }
        let dHp = 2.0 * sqrt(cProd) * sin(dkp * .pi / 360.0)

        let t = 1.0 - 0.17 * cos((avgHp - 30.0) * .pi / 180.0)
                    + 0.24 * cos((2.0 * avgHp) * .pi / 180.0)
                    + 0.32 * cos((3.0 * avgHp + 6.0) * .pi / 180.0)
                    - 0.20 * cos((4.0 * avgHp - 63.0) * .pi / 180.0)

        let dLp = other.L - self.L
        let dCp = c2p - c1p

        let avgL50Sq = pow(avgL - 50.0, 2.0)
        let sL = 1.0 + ((0.015 * avgL50Sq) / sqrt(20.0 + avgL50Sq))
        let sC = 1.0 + 0.045 * avgCp
        let sH = 1.0 + 0.015 * avgCp * t

        let dTheta = 30.0 * exp(-pow((avgHp - 275.0) / 25.0, 2.0))
        let avgCp7 = pow(avgCp, 7.0)
        let rC = 2.0 * sqrt(avgCp7 / (avgCp7 + p25_7))
        let rT = -sin(2.0 * dTheta * .pi / 180.0) * rC

        let vL = dLp / (kL * sL)
        let vC = dCp / (kC * sC)
        let vH = dHp / (kH * sH)

        return sqrt(max(0.0, vL * vL + vC * vC + vH * vH + rT * vC * vH))
    }

    public static func fromRGB(r: Double, g: Double, b: Double) -> LabColor {
        func pivotRgb(_ n: Double) -> Double {
            let clamped = max(0.0, min(1.0, n))
            return (clamped > 0.04045) ? pow((clamped + 0.055) / 1.055, 2.4) : (clamped / 12.92)
        }
        let lr = pivotRgb(r / 255.0) * 100.0
        let lg = pivotRgb(g / 255.0) * 100.0
        let lb = pivotRgb(b / 255.0) * 100.0

        let x = lr * 0.4124 + lg * 0.3576 + lb * 0.1805
        let y = lr * 0.2126 + lg * 0.7152 + lb * 0.0722
        let z = lr * 0.0193 + lg * 0.1192 + lb * 0.9505

        let xn = 95.047, yn = 100.000, zn = 108.883
        func pivotXyz(_ n: Double) -> Double {
            return (n > 0.008856) ? cbrt(n) : (7.787 * n + 16.0 / 116.0)
        }
        let fx = pivotXyz(x / xn)
        let fy = pivotXyz(y / yn)
        let fz = pivotXyz(z / zn)

        let L = max(0.0, 116.0 * fy - 16.0)
        let a = (fx - fy) * 500.0
        let bVal = (fy - fz) * 200.0
        return LabColor(L: L, a: a, b: bVal)
    }

    public var uiColor: Color {
        var y = (L + 16.0) / 116.0
        var x = a / 500.0 + y
        var z = y - b / 200.0

        func unpivot(_ val: Double) -> Double {
            let p = val * val * val
            return p > 0.008856 ? p : (val - 16.0 / 116.0) / 7.787
        }
        x = unpivot(x) * 0.95047
        y = unpivot(y) * 1.00000
        z = unpivot(z) * 1.08883

        let r = x * 3.2406 + y * -1.5372 + z * -0.4986
        let g = x * -0.9689 + y * 1.8758 + z * 0.0415
        let bLinear = x * 0.0557 + y * -0.2040 + z * 1.0570

        func sRgbCompand(_ c: Double) -> Double {
            let clamped = max(0.0, min(1.0, c))
            return clamped <= 0.0031308 ? (clamped * 12.92) : (1.055 * pow(clamped, 1.0 / 2.4) - 0.055)
        }
        return Color(red: sRgbCompand(r), green: sRgbCompand(g), blue: sRgbCompand(bLinear))
    }
}

// SwiftUI Representable Camera View
public struct CameraVideoPreview: UIViewRepresentable {
    public let session: AVCaptureSession

    public init(session: AVCaptureSession) {
        self.session = session
    }

    public func makeUIView(context: Context) -> CameraPreviewUIView {
        let view = CameraPreviewUIView()
        view.previewLayer.session = session
        view.previewLayer.videoGravity = .resizeAspectFill
        if let conn = view.previewLayer.connection, conn.isVideoOrientationSupported {
            conn.videoOrientation = .portrait
        }
        return view
    }

    public func updateUIView(_ uiView: CameraPreviewUIView, context: Context) {
        if uiView.previewLayer.session !== session {
            uiView.previewLayer.session = session
        }
        if let conn = uiView.previewLayer.connection, conn.isVideoOrientationSupported {
            conn.videoOrientation = .portrait
        }
    }

    public class CameraPreviewUIView: UIView {
        public override class var layerClass: AnyClass {
            AVCaptureVideoPreviewLayer.self
        }
        public var previewLayer: AVCaptureVideoPreviewLayer {
            layer as! AVCaptureVideoPreviewLayer
        }
        public override func layoutSubviews() {
            super.layoutSubviews()
            previewLayer.frame = bounds
            if let conn = previewLayer.connection, conn.isVideoOrientationSupported {
                conn.videoOrientation = .portrait
            }
        }
    }
}

private final class ScannerSyncState: @unchecked Sendable {
    private let lock = NSLock()
    private var _isConfigured: Bool = false
    private var _samplingCubeSize: Int = 3
    private var _lastSampleTimestamp: CFTimeInterval = 0
    private var _videoOutput: AVCaptureVideoDataOutput?

    var isConfigured: Bool {
        lock.lock(); defer { lock.unlock() }
        return _isConfigured
    }

    func setConfigured(output: AVCaptureVideoDataOutput) {
        lock.lock(); defer { lock.unlock() }
        _videoOutput = output
        _isConfigured = true
    }

    func setCubeSize(_ size: Int) {
        lock.lock(); defer { lock.unlock() }
        _samplingCubeSize = size
    }

    func tryBeginSample(now: CFTimeInterval, minInterval: CFTimeInterval = 0.08) -> Int? {
        lock.lock(); defer { lock.unlock() }
        guard (now - _lastSampleTimestamp) >= minInterval else { return nil }
        _lastSampleTimestamp = now
        return _samplingCubeSize
    }
}

@MainActor
public class CIELABScanner: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    // Natural beginner-friendly scan order: Front(Green) -> Right(Red) -> Back(Blue) -> Left(Orange) -> Up(White) -> Down(Yellow)
    public static let naturalScanOrder: [Int] = [4, 3, 5, 2, 0, 1]

    @Published public var cubeSize: Int = 3
    @Published public var currentFaceIndex: Int = 4 // 0:U, 1:D, 2:L, 3:R, 4:F, 5:B
    @Published public var faceStatus: [Bool] = Array(repeating: false, count: 6)
    @Published public var liveSampledColors: [LabColor] = []
    @Published public var liveClassifiedColors: [Int] = []
    @Published public var savedFacelets: [[Int]] = Array(repeating: [], count: 6)
    @Published public var isScanningComplete: Bool = false
    @Published public var isCameraAuthorized: Bool = false
    @Published public var isTorchOn: Bool = false
    @Published public var isSessionRunning: Bool = false
    @Published public var isAutoCaptureEnabled: Bool = false
    @Published public var cameraError: String? = nil

    public let captureSession = AVCaptureSession()

    private let sessionQueue = DispatchQueue(label: "com.rubikstudio.cameraSessionQueue")
    private let sampleQueue = DispatchQueue(label: "com.rubikstudio.sampleBufferQueue", qos: .userInteractive)

    // Thread-safe state accessed from background camera/sample queues
    private let syncState = ScannerSyncState()

    private var motionManager: CMMotionManager?
    private var hapticFeedback: UIImpactFeedbackGenerator?

    private var lastMotionTimestamp: TimeInterval = 0
    private var isStableFor035s: Bool = false
    private var lastAngularVelocity: Double = 0.0

    nonisolated private static let standardCentroids: [LabColor] = [
        LabColor(L: 92.0, a: 0.0, b: 0.0),    // 0: White (U)
        LabColor(L: 85.0, a: -4.0, b: 80.0),  // 1: Yellow (D)
        LabColor(L: 64.0, a: 46.0, b: 62.0),  // 2: Orange (L)
        LabColor(L: 48.0, a: 66.0, b: 42.0),  // 3: Red (R)
        LabColor(L: 55.0, a: -50.0, b: 36.0), // 4: Green (F)
        LabColor(L: 38.0, a: 12.0, b: -48.0)  // 5: Blue (B)
    ]

    public override init() {
        super.init()
        resetAllFaces()
    }

    public func startCamera() {
        if hapticFeedback == nil {
            hapticFeedback = UIImpactFeedbackGenerator(style: .medium)
            hapticFeedback?.prepare()
        }
        cameraError = nil
        checkCameraAuthorization()
        startMotionTracking()
    }

    public func stopCamera() {
        stopMotionTracking()
        setTorch(enabled: false)
        let session = captureSession
        sessionQueue.async { [weak self] in
            if session.isRunning {
                session.stopRunning()
            }
            Task { @MainActor [weak self] in
                self?.isSessionRunning = false
            }
        }
    }

    public func checkCameraAuthorization() {
        let status = AVCaptureDevice.authorizationStatus(for: .video)
        switch status {
        case .authorized:
            self.isCameraAuthorized = true
            setupCaptureSession()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
                Task { @MainActor [weak self] in
                    guard let self = self else { return }
                    self.isCameraAuthorized = granted
                    if granted {
                        self.setupCaptureSession()
                    } else {
                        self.cameraError = "Bạn đã từ chối quyền Camera. Vào Cài đặt để bật lại hoặc dùng Bản Đồ Màu."
                    }
                }
            }
        case .denied, .restricted:
            self.isCameraAuthorized = false
            self.cameraError = "Quyền Camera chưa được cấp. Hãy mở Cài đặt iPhone hoặc dùng Bản Đồ Màu."
        @unknown default:
            self.isCameraAuthorized = false
        }
    }

    public func setupCaptureSession() {
        let session = captureSession
        let state = syncState
        sessionQueue.async { [weak self] in
            guard let self = self else { return }

            let alreadyConfigured = state.isConfigured

            if !alreadyConfigured {
                session.beginConfiguration()
                session.sessionPreset = .high

                guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back)
                        ?? AVCaptureDevice.default(for: .video) else {
                    session.commitConfiguration()
                    Task { @MainActor [weak self] in
                        self?.cameraError = "Thiết bị không có camera khả dụng (hoặc đang chạy trên Simulator)."
                    }
                    return
                }

                do {
                    let input = try AVCaptureDeviceInput(device: device)
                    if session.canAddInput(input) {
                        session.addInput(input)
                    } else {
                        session.commitConfiguration()
                        Task { @MainActor [weak self] in
                            self?.cameraError = "Không thể kết nối đầu vào Camera."
                        }
                        return
                    }
                } catch {
                    session.commitConfiguration()
                    let msg = error.localizedDescription
                    Task { @MainActor [weak self] in
                        self?.cameraError = "Lỗi khởi tạo Camera: \(msg)"
                    }
                    return
                }

                let output = AVCaptureVideoDataOutput()
                output.alwaysDiscardsLateVideoFrames = true
                output.videoSettings = [
                    kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_32BGRA)
                ]
                output.setSampleBufferDelegate(self, queue: self.sampleQueue)

                if session.canAddOutput(output) {
                    session.addOutput(output)
                    if let connection = output.connection(with: .video), connection.isVideoOrientationSupported {
                        connection.videoOrientation = .portrait
                    }
                }

                session.commitConfiguration()

                state.setConfigured(output: output)
            }

            if !session.isRunning {
                session.startRunning()
            }

            let running = session.isRunning
            Task { @MainActor [weak self] in
                self?.isSessionRunning = running
                if running {
                    self?.cameraError = nil
                }
            }
        }
    }

    public func toggleTorch() {
        setTorch(enabled: !isTorchOn)
    }

    private func setTorch(enabled: Bool) {
        if !enabled && !isTorchOn { return }
        guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
              device.hasTorch, device.isTorchAvailable else {
            isTorchOn = false
            return
        }
        do {
            try device.lockForConfiguration()
            defer { device.unlockForConfiguration() }
            if enabled && device.isTorchModeSupported(.on) {
                try device.setTorchModeOn(level: 0.85)
                isTorchOn = true
            } else if device.isTorchModeSupported(.off) {
                device.torchMode = .off
                isTorchOn = false
            }
        } catch {
            isTorchOn = false
        }
    }

    public func setCubeSize(_ size: Int) {
        let clamped = max(2, min(5, size))
        self.cubeSize = clamped
        syncState.setCubeSize(clamped)
        resetAllFaces()
    }

    public func resetAllFaces() {
        currentFaceIndex = Self.naturalScanOrder[0]
        faceStatus = Array(repeating: false, count: 6)
        let total = cubeSize * cubeSize
        // Default faces: U=0(White), D=1(Yellow), L=2(Orange), R=3(Red), F=4(Green), B=5(Blue)
        savedFacelets = (0..<6).map { Array(repeating: $0, count: total) }
        liveSampledColors = []
        liveClassifiedColors = []
        isScanningComplete = false
    }

    public func loadFaceletsFlat(_ flat: [Int], size: Int, markScanned: Bool = false) {
        let clamped = max(2, min(5, size))
        let nn = clamped * clamped
        guard flat.count == 6 * nn else { return }
        self.cubeSize = clamped
        syncState.setCubeSize(clamped)

        var faces: [[Int]] = []
        for f in 0..<6 {
            let start = f * nn
            faces.append(Array(flat[start..<(start + nn)]))
        }
        self.savedFacelets = faces
        if markScanned {
            self.faceStatus = Array(repeating: true, count: 6)
        } else {
            self.faceStatus = Array(repeating: false, count: 6)
            self.currentFaceIndex = Self.naturalScanOrder[0]
            self.isScanningComplete = false
        }
    }

    public func getFlatFacelets() -> [Int] {
        var flat: [Int] = []
        let nn = cubeSize * cubeSize
        flat.reserveCapacity(6 * nn)
        for face in 0..<6 {
            if face < savedFacelets.count && savedFacelets[face].count == nn {
                flat.append(contentsOf: savedFacelets[face])
            } else {
                flat.append(contentsOf: Array(repeating: face, count: nn))
            }
        }
        return flat
    }

    public func selectFace(_ index: Int) {
        guard index >= 0 && index < 6 else { return }
        currentFaceIndex = index
        isScanningComplete = false
    }

    private func startMotionTracking() {
        if motionManager == nil {
            motionManager = CMMotionManager()
        }
        guard let mm = motionManager, mm.isDeviceMotionAvailable, !mm.isDeviceMotionActive else { return }
        mm.deviceMotionUpdateInterval = 0.05
        mm.startDeviceMotionUpdates(to: .main) { [weak self] motion, _ in
            guard let self = self, let motion = motion else { return }
            Task { @MainActor [weak self] in
                self?.handleMotionUpdate(motion)
            }
        }
    }

    private func stopMotionTracking() {
        motionManager?.stopDeviceMotionUpdates()
    }

    private func handleMotionUpdate(_ motion: CMDeviceMotion) {
        guard isAutoCaptureEnabled, !isScanningComplete else { return }
        let rotationRate = motion.rotationRate
        let angularSpeed = sqrt(rotationRate.x * rotationRate.x +
                                rotationRate.y * rotationRate.y +
                                rotationRate.z * rotationRate.z)

        let now = Date().timeIntervalSince1970
        if angularSpeed < 0.25 {
            if (now - lastMotionTimestamp) >= 0.45 && !isStableFor035s {
                isStableFor035s = true
                if lastAngularVelocity > 1.2 && liveClassifiedColors.count == cubeSize * cubeSize {
                    captureCurrentFace()
                }
            }
        } else {
            lastMotionTimestamp = now
            isStableFor035s = false
            lastAngularVelocity = angularSpeed
        }
    }

    public func captureCurrentFace() {
        guard currentFaceIndex >= 0 && currentFaceIndex < 6 else { return }
        let expectedCount = cubeSize * cubeSize
        if liveClassifiedColors.count == expectedCount {
            savedFacelets[currentFaceIndex] = liveClassifiedColors
        } else if liveSampledColors.count == expectedCount {
            savedFacelets[currentFaceIndex] = liveSampledColors.map { Self.colorToFaceIndex($0) }
        }
        faceStatus[currentFaceIndex] = true
        hapticFeedback?.impactOccurred()

        if let nextIncomplete = Self.naturalScanOrder.first(where: { !faceStatus[$0] }) {
            currentFaceIndex = nextIncomplete
        } else {
            isScanningComplete = true
        }
    }

    public func setFaceletColor(face: Int, index: Int, colorId: Int) {
        guard face >= 0 && face < savedFacelets.count,
              index >= 0 && index < savedFacelets[face].count,
              colorId >= 0 && colorId < 6 else { return }
        savedFacelets[face][index] = colorId
        faceStatus[face] = true
    }

    nonisolated public static func colorToFaceIndex(_ lab: LabColor) -> Int {
        var bestIdx = 0
        var bestDist = Double.greatestFiniteMagnitude
        for (i, c) in standardCentroids.enumerated() {
            let dist = lab.deltaE2000(to: c)
            if dist < bestDist {
                bestDist = dist
                bestIdx = i
            }
        }
        return bestIdx
    }

    // AVCaptureVideoDataOutputSampleBufferDelegate
    // Samples CVPixelBuffer synchronously while locked so baseAddress is never read after unlock.
    nonisolated public func captureOutput(_ output: AVCaptureOutput,
                                          didOutput sampleBuffer: CMSampleBuffer,
                                          from connection: AVCaptureConnection) {
        let now = CACurrentMediaTime()
        guard let n = syncState.tryBeginSample(now: now),
              n >= 2 && n <= 5,
              let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        guard CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_32BGRA else { return }
        guard CVPixelBufferLockBaseAddress(pixelBuffer, .readOnly) == kCVReturnSuccess else { return }
        defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, .readOnly) }

        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)
        let bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer)
        guard width >= 32, height >= 32, bytesPerRow >= width * 4,
              let baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer) else { return }

        let rawPtr = baseAddress.assumingMemoryBound(to: UInt8.self)
        let reticlePx = max(n * 8, Int(Double(min(width, height)) * 0.66))
        let boxSize = reticlePx / n
        let gridTotal = boxSize * n
        let startX = (width - gridTotal) / 2
        let startY = (height - gridTotal) / 2

        var sampledColors: [LabColor] = []
        var classifiedColors: [Int] = []
        sampledColors.reserveCapacity(n * n)
        classifiedColors.reserveCapacity(n * n)

        let inset = max(1, boxSize / 4)
        for r in 0..<n {
            for c in 0..<n {
                let cellX = startX + c * boxSize
                let cellY = startY + r * boxSize

                let minX = max(0, cellX + inset)
                let maxX = min(width, cellX + boxSize - inset)
                let minY = max(0, cellY + inset)
                let maxY = min(height, cellY + boxSize - inset)

                let spanX = max(1, maxX - minX)
                let spanY = max(1, maxY - minY)
                let stepX = max(1, spanX / 6)
                let stepY = max(1, spanY / 6)

                var rSum = 0.0, gSum = 0.0, bSum = 0.0
                var count = 0

                var y = minY
                while y < maxY {
                    let rowBase = y * bytesPerRow
                    var x = minX
                    while x < maxX {
                        let px = rowBase + x * 4
                        let b = Double(rawPtr[px])
                        let g = Double(rawPtr[px + 1])
                        let r = Double(rawPtr[px + 2])
                        rSum += r
                        gSum += g
                        bSum += b
                        count += 1
                        x += stepX
                    }
                    y += stepY
                }

                let lab: LabColor
                if count > 0 {
                    let inv = 1.0 / Double(count)
                    lab = LabColor.fromRGB(r: rSum * inv, g: gSum * inv, b: bSum * inv)
                } else {
                    lab = LabColor(L: 50.0, a: 0.0, b: 0.0)
                }
                sampledColors.append(lab)
                classifiedColors.append(Self.colorToFaceIndex(lab))
            }
        }

        Task { @MainActor [weak self] in
            guard let self = self, self.isSessionRunning else { return }
            self.liveSampledColors = sampledColors
            self.liveClassifiedColors = classifiedColors
        }
    }

    public func getFaceletsCsv() -> String {
        return getFlatFacelets().map(String.init).joined(separator: ",")
    }
}
