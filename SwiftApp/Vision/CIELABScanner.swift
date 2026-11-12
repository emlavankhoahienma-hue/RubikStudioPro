import Foundation
import AVFoundation
import CoreMotion
import UIKit
import Combine
import CoreVideo

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
}

@MainActor
public class CIELABScanner: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    @Published public var cubeSize: Int = 3
    @Published public var currentFaceIndex: Int = 0 // 0:U, 1:D, 2:L, 3:R, 4:F, 5:B
    @Published public var faceStatus: [Bool] = Array(repeating: false, count: 6)
    @Published public var previewFrame: UIImage?
    @Published public var currentFaceColors: [LabColor] = []
    @Published public var isScanningComplete: Bool = false

    private let motionManager = CMMotionManager()
    private let hapticFeedback = UIImpactFeedbackGenerator(style: .rigid)
    private var allScannedFacelets: [[LabColor]] = Array(repeating: [], count: 6)

    // Motion stabilization tracking
    private var lastMotionTimestamp: TimeInterval = 0
    private var isStableFor035s: Bool = false
    private var lastAngularVelocity: Double = 0.0

    public override init() {
        super.init()
        hapticFeedback.prepare()
        startMotionTracking()
    }

    public func setCubeSize(_ size: Int) {
        self.cubeSize = size
        resetScanning()
    }

    public func resetScanning() {
        currentFaceIndex = 0
        faceStatus = Array(repeating: false, count: 6)
        allScannedFacelets = Array(repeating: [], count: 6)
        currentFaceColors = []
        isScanningComplete = false
    }

    private func startMotionTracking() {
        guard motionManager.isDeviceMotionAvailable else { return }
        motionManager.deviceMotionUpdateInterval = 0.05
        motionManager.startDeviceMotionUpdates(to: .main) { [weak self] motion, _ in
            guard let self = self, let motion = motion else { return }
            self.handleMotionUpdate(motion)
        }
    }

    private func handleMotionUpdate(_ motion: CMDeviceMotion) {
        let rotationRate = motion.rotationRate
        let angularSpeed = sqrt(rotationRate.x * rotationRate.x +
                                rotationRate.y * rotationRate.y +
                                rotationRate.z * rotationRate.z)

        let now = Date().timeIntervalSince1970
        if angularSpeed < 0.25 { // Device held steady
            if (now - lastMotionTimestamp) >= 0.35 && !isStableFor035s {
                isStableFor035s = true
                if lastAngularVelocity > 1.2 {
                    // Turn detected followed by 0.35s stillness -> auto-capture
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
        guard !currentFaceColors.isEmpty, currentFaceColors.count == (cubeSize * cubeSize) else { return }

        allScannedFacelets[currentFaceIndex] = currentFaceColors
        faceStatus[currentFaceIndex] = true
        hapticFeedback.impactOccurred()

        if currentFaceIndex < 5 {
            currentFaceIndex += 1
        } else {
            isScanningComplete = true
        }
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

                    // Sample central 50% core region
                    let coreInset = boxSize / 4
                    var rSum = 0.0, gSum = 0.0, bSum = 0.0
                    var sampleCount = 0

                    for y in (cellY + coreInset)..<(cellY + boxSize - coreInset) {
                        let rowPtr = baseAddress.advanced(by: y * bytesPerRow).assumingMemoryBound(to: UInt8.self)
                        for x in (cellX + coreInset)..<(cellX + boxSize - coreInset) {
                            // BGRA or RGBA pixel
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
                    }
                }
            }

            self.currentFaceColors = sampledColors
        }
    }

    // Constrained K-Means Clustering: partitions the 6*N^2 colors into 6 groups
    // with EXACTLY N^2 facelets each using Delta E 2000.
    public func computeClassifiedColorsCsv() -> String? {
        let totalFacelets = 6 * cubeSize * cubeSize
        var flatList: [LabColor] = []
        for face in 0..<6 {
            flatList.append(contentsOf: allScannedFacelets[face])
        }
        guard flatList.count == totalFacelets else { return nil }

        // Initial centroids: standard 6 face colors (White, Yellow, Orange, Red, Green, Blue)
        var centroids: [LabColor] = [
            LabColor(L: 95.0, a: 0.0, b: 0.0),    // 0: U (White)
            LabColor(L: 85.0, a: -5.0, b: 85.0),  // 1: D (Yellow)
            LabColor(L: 65.0, a: 45.0, b: 60.0),  // 2: L (Orange)
            LabColor(L: 50.0, a: 65.0, b: 40.0),  // 3: R (Red)
            LabColor(L: 55.0, a: -50.0, b: 35.0), // 4: F (Green)
            LabColor(L: 40.0, a: 10.0, b: -45.0)  // 5: B (Blue)
        ]

        let targetPerCluster = cubeSize * cubeSize
        var assignments = Array(repeating: 0, count: totalFacelets)

        // Constrained iterations
        for _ in 0..<10 {
            var distances: [(faceletIdx: Int, clusterIdx: Int, dist: Double)] = []
            for i in 0..<totalFacelets {
                for k in 0..<6 {
                    distances.append((i, k, flatList[i].deltaE2000(to: centroids[k])))
                }
            }
            distances.sort { $0.dist < $1.dist }

            var clusterCounts = Array(repeating: 0, count: 6)
            var assignedSet = Set<Int>()

            for item in distances {
                if !assignedSet.contains(item.faceletIdx) && clusterCounts[item.clusterIdx] < targetPerCluster {
                    assignments[item.faceletIdx] = item.clusterIdx
                    clusterCounts[item.clusterIdx] += 1
                    assignedSet.insert(item.faceletIdx)
                }
            }

            // Update centroids
            for k in 0..<6 {
                var sL = 0.0, sa = 0.0, sb = 0.0, cnt = 0.0
                for i in 0..<totalFacelets where assignments[i] == k {
                    sL += flatList[i].L
                    sa += flatList[i].a
                    sb += flatList[i].b
                    cnt += 1.0
                }
                if cnt > 0 {
                    centroids[k] = LabColor(L: sL / cnt, a: sa / cnt, b: sb / cnt)
                }
            }
        }

        return assignments.map { String($0) }.joined(separator: ",")
    }
}
