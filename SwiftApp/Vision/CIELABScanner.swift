import Foundation
import AVFoundation
import CoreMotion
import UIKit
import Combine
import CoreVideo
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

    // Standard Delta E 2000 (CIE dE2000) color difference formula
    public func deltaE2000(to other: LabColor) -> Double {
        let kL = 1.0, kC = 1.0, kH = 1.0
        let avgL = (self.L + other.L) / 2.0
        let c1 = sqrt(self.a * self.a + self.b * self.b)
        let c2 = sqrt(other.a * other.a + other.b * other.b)
        let avgC = (c1 + c2) / 2.0

        let g = 0.5 * (1.0 - sqrt(pow(avgC, 7.0) / (pow(avgC, 7.0) + pow(25.0, 7.0))))
        let a1p = (1.0 + g) * self.a
        let a2p = (1.0 + g) * other.a

        let c1p = sqrt(a1p * a1p + self.b * self.b)
        let c2p = sqrt(a2p * a2p + other.b * other.b)
        let avgCp = (c1p + c2p) / 2.0

        let h1p = atan2(self.b, a1p) >= 0 ? atan2(self.b, a1p) * 180.0 / .pi : (atan2(self.b, a1p) * 180.0 / .pi) + 360.0
        let h2p = atan2(other.b, a2p) >= 0 ? atan2(other.b, a2p) * 180.0 / .pi : (atan2(other.b, a2p) * 180.0 / .pi) + 360.0

        var dkp = h2p - h1p
        if abs(h1p - h2p) > 180.0 {
            if h2p <= h1p {
                dkp += 360.0
            } else {
                dkp -= 360.0
            }
        }
        let dHp = 2.0 * sqrt(c1p * c2p) * sin(dkp * .pi / 360.0)

        var avgHp = (h1p + h2p) / 2.0
        if abs(h1p - h2p) > 180.0 {
            if (h1p + h2p) < 360.0 {
                avgHp += 180.0
            } else {
                avgHp -= 180.0
            }
        }

        let t = 1.0 - 0.17 * cos((avgHp - 30.0) * .pi / 180.0)
                    + 0.24 * cos((2.0 * avgHp) * .pi / 180.0)
                    + 0.32 * cos((3.0 * avgHp + 6.0) * .pi / 180.0)
                    - 0.20 * cos((4.0 * avgHp - 63.0) * .pi / 180.0)

        let dLp = other.L - self.L
        let dCp = c2p - c1p

        let sL = 1.0 + ((0.015 * pow(avgL - 50.0, 2.0)) / sqrt(20.0 + pow(avgL - 50.0, 2.0)))
        let sC = 1.0 + 0.045 * avgCp
        let sH = 1.0 + 0.015 * avgCp * t

        let dTheta = 30.0 * exp(-pow((avgHp - 275.0) / 25.0, 2.0))
        let rC = 2.0 * sqrt(pow(avgCp, 7.0) / (pow(avgCp, 7.0) + pow(25.0, 7.0)))
        let rT = -sin(2.0 * dTheta * .pi / 180.0) * rC

        let vL = dLp / (kL * sL)
        let vC = dCp / (kC * sC)
        let vH = dHp / (kH * sH)

        return sqrt(vL * vL + vC * vC + vH * vH + rT * vC * vH)
    }

    public static func fromRGB(r: Double, g: Double, b: Double) -> LabColor {
        // Linearize sRGB
        func pivotRgb(_ n: Double) -> Double {
            return (n > 0.04045) ? pow((n + 0.055) / 1.055, 2.4) : (n / 12.92)
        }
        let lr = pivotRgb(r / 255.0) * 100.0
        let lg = pivotRgb(g / 255.0) * 100.0
        let lb = pivotRgb(b / 255.0) * 100.0

        // Convert to XYZ (D65 standard)
        let x = lr * 0.4124 + lg * 0.3576 + lb * 0.1805
        let y = lr * 0.2126 + lg * 0.7152 + lb * 0.0722
        let z = lr * 0.0193 + lg * 0.1192 + lb * 0.9505

        // Normalize for D65 reference white
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

        var r = x * 3.2406 + y * -1.5372 + z * -0.4986
        var g = x * -0.9689 + y * 1.8758 + z * 0.0415
        var bLinear = x * 0.0557 + y * -0.2040 + z * 1.0570

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
        return view
    }

    public func updateUIView(_ uiView: CameraPreviewUIView, context: Context) {}

    public class CameraPreviewUIView: UIView {
        public override class var layerClass: AnyClass {
            AVCaptureVideoPreviewLayer.self
        }
        public var previewLayer: AVCaptureVideoPreviewLayer {
            layer as! AVCaptureVideoPreviewLayer
        }
    }
}

@MainActor
public class CIELABScanner: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    @Published public var cubeSize: Int = 3
    @Published public var currentFaceIndex: Int = 0 // 0:U, 1:D, 2:L, 3:R, 4:F, 5:B
    @Published public var faceStatus: [Bool] = Array(repeating: false, count: 6)
    @Published public var liveSampledColors: [LabColor] = []
    @Published public var savedFacelets: [[Int]] = Array(repeating: [], count: 6)
    @Published public var isScanningComplete: Bool = false
    @Published public var isCameraAuthorized: Bool = false
    @Published public var isTorchOn: Bool = false
    @Published public var isSessionRunning: Bool = false

    public var captureSession: AVCaptureSession?
    private var videoOutput: AVCaptureVideoDataOutput?
    private let sessionQueue = DispatchQueue(label: "com.rubikstudio.cameraSessionQueue")
    private let sampleQueue = DispatchQueue(label: "com.rubikstudio.sampleBufferQueue", qos: .userInteractive)

    private var motionManager: CMMotionManager?
    private var hapticFeedback: UIImpactFeedbackGenerator?

    // Motion tracking for auto-capture
    private var lastMotionTimestamp: TimeInterval = 0
    private var isStableFor035s: Bool = false
    private var lastAngularVelocity: Double = 0.0
    private var isConfigured: Bool = false

    public override init() {
        super.init()
        resetAllFaces()
    }

    public func startCamera() {
        if hapticFeedback == nil {
            hapticFeedback = UIImpactFeedbackGenerator(style: .medium)
            hapticFeedback?.prepare()
        }
        checkCameraAuthorization()
        startMotionTracking()
    }

    public func stopCamera() {
        stopMotionTracking()
        sessionQueue.async { [weak self] in
            guard let self = self, let session = self.captureSession, session.isRunning else { return }
            session.stopRunning()
            Task { @MainActor in
                self.isSessionRunning = false
            }
        }
    }

    public func checkCameraAuthorization() {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized:
            self.isCameraAuthorized = true
            setupCaptureSession()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
                Task { @MainActor in
                    self?.isCameraAuthorized = granted
                    if granted {
                        self?.setupCaptureSession()
                    }
                }
            }
        default:
            self.isCameraAuthorized = false
        }
    }

    public func setupCaptureSession() {
        sessionQueue.async { [weak self] in
            guard let self = self else { return }
            if self.captureSession == nil {
                self.captureSession = AVCaptureSession()
            }
            guard let session = self.captureSession else { return }

            if !self.isConfigured {
                session.beginConfiguration()
                session.sessionPreset = .high

                guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
                      let input = try? AVCaptureDeviceInput(device: device),
                      session.canAddInput(input) else {
                    session.commitConfiguration()
                    return
                }
                session.addInput(input)

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
                self.videoOutput = output
                session.commitConfiguration()
                self.isConfigured = true
            }

            if !session.isRunning {
                session.startRunning()
            }

            Task { @MainActor in
                self.isSessionRunning = session.isRunning
            }
        }
    }

    public func toggleTorch() {
        guard let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
              device.hasTorch else { return }
        do {
            try device.lockForConfiguration()
            if device.torchMode == .on {
                device.torchMode = .off
                self.isTorchOn = false
            } else {
                try device.setTorchModeOn(level: 1.0)
                self.isTorchOn = true
            }
            device.unlockForConfiguration()
        } catch {
            print("Torch error: \(error)")
        }
    }

    public func setCubeSize(_ size: Int) {
        self.cubeSize = size
        resetAllFaces()
    }

    public func resetAllFaces() {
        currentFaceIndex = 0
        faceStatus = Array(repeating: false, count: 6)
        let total = cubeSize * cubeSize
        // Default faces: U=0(White), D=1(Yellow), L=2(Orange), R=3(Red), F=4(Green), B=5(Blue)
        savedFacelets = (0..<6).map { Array(repeating: $0, count: total) }
        liveSampledColors = []
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
            self.handleMotionUpdate(motion)
        }
    }

    private func stopMotionTracking() {
        motionManager?.stopDeviceMotionUpdates()
    }

    private func handleMotionUpdate(_ motion: CMDeviceMotion) {
        let rotationRate = motion.rotationRate
        let angularSpeed = sqrt(rotationRate.x * rotationRate.x +
                                rotationRate.y * rotationRate.y +
                                rotationRate.z * rotationRate.z)

        let now = Date().timeIntervalSince1970
        if angularSpeed < 0.25 {
            if (now - lastMotionTimestamp) >= 0.35 && !isStableFor035s {
                isStableFor035s = true
                if lastAngularVelocity > 1.2 && !liveSampledColors.isEmpty {
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
        let expectedCount = cubeSize * cubeSize
        if !liveSampledColors.isEmpty && liveSampledColors.count == expectedCount {
            let classified = liveSampledColors.map { colorToFaceIndex($0) }
            savedFacelets[currentFaceIndex] = classified
        }
        faceStatus[currentFaceIndex] = true
        hapticFeedback?.impactOccurred()

        if currentFaceIndex < 5 {
            currentFaceIndex += 1
        } else {
            isScanningComplete = true
        }
    }

    public func setFaceletColor(face: Int, index: Int, colorId: Int) {
        guard face >= 0 && face < 6, index >= 0 && index < savedFacelets[face].count else { return }
        savedFacelets[face][index] = colorId
        faceStatus[face] = true
    }

    private func colorToFaceIndex(_ lab: LabColor) -> Int {
        let standardCentroids: [LabColor] = [
            LabColor(L: 95.0, a: 0.0, b: 0.0),    // 0: White (U)
            LabColor(L: 85.0, a: -5.0, b: 85.0),  // 1: Yellow (D)
            LabColor(L: 65.0, a: 45.0, b: 60.0),  // 2: Orange (L)
            LabColor(L: 50.0, a: 65.0, b: 40.0),  // 3: Red (R)
            LabColor(L: 55.0, a: -50.0, b: 35.0), // 4: Green (F)
            LabColor(L: 40.0, a: 10.0, b: -45.0)  // 5: Blue (B)
        ]

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
    nonisolated public func captureOutput(_ output: AVCaptureOutput,
                                          didOutput sampleBuffer: CMSampleBuffer,
                                          from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }

        CVPixelBufferLockBaseAddress(pixelBuffer, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, .readOnly) }

        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)
        guard let baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer) else { return }
        let bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer)

        Task { @MainActor in
            let n = self.cubeSize
            var sampledColors: [LabColor] = []

            let boxSize = min(width, height) / (n + 2)
            let startX = (width - boxSize * n) / 2
            let startY = (height - boxSize * n) / 2

            for r in 0..<n {
                for c in 0..<n {
                    let cellX = startX + c * boxSize
                    let cellY = startY + r * boxSize

                    let coreInset = max(1, boxSize / 4)
                    var rSum = 0.0, gSum = 0.0, bSum = 0.0
                    var sampleCount = 0

                    let minY = max(0, cellY + coreInset)
                    let maxY = min(height, cellY + boxSize - coreInset)
                    let minX = max(0, cellX + coreInset)
                    let maxX = min(width, cellX + boxSize - coreInset)

                    for y in minY..<maxY {
                        let rowPtr = baseAddress.advanced(by: y * bytesPerRow).assumingMemoryBound(to: UInt8.self)
                        for x in minX..<maxX {
                            let b = Double(rowPtr[x * 4])
                            let g = Double(rowPtr[x * 4 + 1])
                            let r = Double(rowPtr[x * 4 + 2])
                            rSum += r; gSum += g; bSum += b
                            sampleCount += 1
                        }
                    }

                    if sampleCount > 0 {
                        let avgR = rSum / Double(sampleCount)
                        let avgG = gSum / Double(sampleCount)
                        let avgB = bSum / Double(sampleCount)
                        sampledColors.append(LabColor.fromRGB(r: avgR, g: avgG, b: avgB))
                    } else {
                        sampledColors.append(LabColor(L: 50, a: 0, b: 0))
                    }
                }
            }

            self.liveSampledColors = sampledColors
        }
    }

    public func getFaceletsCsv() -> String {
        var flat: [String] = []
        for face in 0..<6 {
            for val in savedFacelets[face] {
                flat.append(String(val))
            }
        }
        return flat.joined(separator: ",")
    }
}
