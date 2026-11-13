import Foundation
@preconcurrency import AVFoundation
import CoreMotion
import UIKit
import Combine
import CoreVideo
import QuartzCore
import SwiftUI

public struct RGBSample: Equatable, Sendable {
    public var r: Double
    public var g: Double
    public var b: Double

    public init(r: Double, g: Double, b: Double) {
        self.r = r
        self.g = g
        self.b = b
    }

    public static let neutralWhite = RGBSample(r: 238.0, g: 238.0, b: 238.0)

    public var luminance: Double {
        return 0.299 * r + 0.587 * g + 0.114 * b
    }

    public var neutralityRatio: Double {
        let mx = max(1.0, max(r, max(g, b)))
        let mn = min(r, min(g, b))
        return mn / mx
    }

    public static func srgbToLinear(_ c255: Double) -> Double {
        let c = max(0.0, min(1.0, c255 / 255.0))
        return (c > 0.04045) ? pow((c + 0.055) / 1.055, 2.4) : (c / 12.92)
    }

    public func toVonKriesLab(whiteRef: RGBSample) -> LabColor {
        let wr = max(0.05, Self.srgbToLinear(whiteRef.r))
        let wg = max(0.05, Self.srgbToLinear(whiteRef.g))
        let wb = max(0.05, Self.srgbToLinear(whiteRef.b))
        let targetWhite = Self.srgbToLinear(238.0)

        let gr = max(0.45, min(3.2, targetWhite / wr))
        let gg = max(0.45, min(3.2, targetWhite / wg))
        let gb = max(0.45, min(3.2, targetWhite / wb))

        let lr = min(1.0, Self.srgbToLinear(r) * gr) * 100.0
        let lg = min(1.0, Self.srgbToLinear(g) * gg) * 100.0
        let lb = min(1.0, Self.srgbToLinear(b) * gb) * 100.0
        return LabColor.fromLinearRGB(lr: lr, lg: lg, lb: lb)
    }

    public func toIsotropicLab(gain: Double) -> LabColor {
        let lr = min(1.0, Self.srgbToLinear(r) * gain) * 100.0
        let lg = min(1.0, Self.srgbToLinear(g) * gain) * 100.0
        let lb = min(1.0, Self.srgbToLinear(b) * gain) * 100.0
        return LabColor.fromLinearRGB(lr: lr, lg: lg, lb: lb)
    }
}

public struct LabColor: Equatable, Sendable {
    public var L: Double
    public var a: Double
    public var b: Double

    public init(L: Double, a: Double, b: Double) {
        self.L = L
        self.a = a
        self.b = b
    }

    // Weighted CIEDE2000 color difference formula (Sharma et al. 2005)
    public func deltaE2000(to other: LabColor, kL: Double = 1.0, kC: Double = 1.0, kH: Double = 1.0) -> Double {
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
        let lr = RGBSample.srgbToLinear(r) * 100.0
        let lg = RGBSample.srgbToLinear(g) * 100.0
        let lb = RGBSample.srgbToLinear(b) * 100.0
        return fromLinearRGB(lr: lr, lg: lg, lb: lb)
    }

    public static func fromLinearRGB(lr: Double, lg: Double, lb: Double) -> LabColor {
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

/// Multi-stage color engine: Hue/Chroma-weighted CIEDE2000 + Linear-RGB Von Kries WB + EM Balanced Min-Cost Assignment.
public enum RubikColorClassifier: Sendable {
    public static let standardCentroids: [LabColor] = [
        LabColor(L: 92.0, a: 0.0, b: 0.0),    // 0: White (U)
        LabColor(L: 85.0, a: -4.0, b: 80.0),  // 1: Yellow (D)
        LabColor(L: 64.0, a: 46.0, b: 62.0),  // 2: Orange (L)
        LabColor(L: 48.0, a: 66.0, b: 42.0),  // 3: Red (R)
        LabColor(L: 55.0, a: -50.0, b: 36.0), // 4: Green (F)
        LabColor(L: 38.0, a: 12.0, b: -48.0)  // 5: Blue (B)
    ]

    public static func rubikDistance(sample: LabColor, anchor: LabColor, colorId: Int, hasWhiteCalib: Bool) -> Double {
        var d = sample.deltaE2000(to: anchor, kL: 2.2, kC: 1.35, kH: 0.55)
        let chroma = hypot(sample.a, sample.b)
        let anchorChroma = hypot(anchor.a, anchor.b)

        if !hasWhiteCalib {
            if colorId == 0 && chroma > 34.0 { d += (chroma - 34.0) * 0.75 }
            if colorId != 0 && chroma < 20.0 && sample.L > 52.0 { d += (20.0 - chroma) * 1.1 }
        } else {
            if colorId == 0 && chroma > anchorChroma + 20.0 { d += (chroma - anchorChroma - 20.0) * 0.7 }
            if colorId != 0 && chroma < 18.0 && sample.L > anchor.L * 0.68 { d += (18.0 - chroma) * 1.0 }
        }

        // Sharp Hue separation for Red (3) vs Orange (2) vs Yellow (1)
        if chroma > 20.0 && anchorChroma > 20.0 {
            let h1 = atan2(sample.b, sample.a) * 180.0 / Double.pi
            let h2 = atan2(anchor.b, anchor.a) * 180.0 / Double.pi
            var dh = abs(h1 - h2)
            if dh > 180.0 { dh = 360.0 - dh }
            d += dh * 0.28
        }

        // Lightness contrast bonus between Red & Orange once white-balanced
        if hasWhiteCalib && (colorId == 2 || colorId == 3) {
            d += abs(sample.L - anchor.L) * 0.12
        }
        return d
    }

    public static func estimateWhiteCandidate(from pool: [RGBSample],
                                              takeCount: Int? = nil,
                                              requireNeutralThreshold: Bool = true) -> RGBSample? {
        guard !pool.isEmpty else { return nil }
        let scored = pool.enumerated().map { (idx, px) -> (score: Double, idx: Int) in
            let score = px.neutralityRatio + 0.15 * (px.luminance / 255.0)
            return (score, idx)
        }.sorted { $0.score > $1.score }

        let k = max(1, min(pool.count, takeCount ?? max(1, pool.count / 6)))
        let candidates = scored.prefix(k).map { pool[$0.idx] }
        if requireNeutralThreshold {
            guard let best = candidates.first,
                  best.neutralityRatio >= 0.48,
                  best.luminance >= 52.0 else {
                return nil
            }
        }
        let rs = candidates.map { $0.r }.sorted()
        let gs = candidates.map { $0.g }.sorted()
        let bs = candidates.map { $0.b }.sorted()
        let mid = candidates.count / 2
        return RGBSample(r: rs[mid], g: gs[mid], b: bs[mid])
    }

    public static func nearestColorIndex(_ lab: LabColor, anchors: [LabColor] = standardCentroids, hasWhiteCalib: Bool = false) -> Int {
        var bestIdx = 0
        var bestDist = Double.greatestFiniteMagnitude
        let count = min(6, anchors.count)
        for i in 0..<count {
            let d = rubikDistance(sample: lab, anchor: anchors[i], colorId: i, hasWhiteCalib: hasWhiteCalib)
            if d < bestDist {
                bestDist = d
                bestIdx = i
            }
        }
        return bestIdx
    }

    /// Bellman-Ford negative-cycle cancellation guaranteeing exact M/6 tiles per color.
    public static func balancedAssign(cost: [[Double]], fixedColor: [Int]) -> [Int] {
        let m = cost.count
        guard m >= 6, m % 6 == 0 else {
            return cost.map { row in
                (0..<6).min(by: { row[$0] < row[$1] }) ?? 0
            }
        }
        let k = m / 6
        var assign = Array(repeating: -1, count: m)
        var cap = Array(repeating: k, count: 6)

        for i in 0..<m where i < fixedColor.count && fixedColor[i] >= 0 && fixedColor[i] < 6 {
            let fc = fixedColor[i]
            if cap[fc] > 0 {
                assign[i] = fc
                cap[fc] -= 1
            }
        }

        func margin(_ i: Int) -> Double {
            var b1 = Double.greatestFiniteMagnitude
            var b2 = Double.greatestFiniteMagnitude
            for c in 0..<6 {
                let v = cost[i][c]
                if v < b1 { b2 = b1; b1 = v }
                else if v < b2 { b2 = v }
            }
            return b2 - b1
        }

        let freeIndices = (0..<m).filter { assign[$0] < 0 }.sorted { margin($0) > margin($1) }
        for i in freeIndices {
            var bestC = 0
            var bestVal = Double.greatestFiniteMagnitude
            for c in 0..<6 where cap[c] > 0 {
                if cost[i][c] < bestVal {
                    bestVal = cost[i][c]
                    bestC = c
                }
            }
            assign[i] = bestC
            cap[bestC] -= 1
        }

        for _ in 0..<128 {
            var w = Array(repeating: Array(repeating: 1e18, count: 6), count: 6)
            var arg = Array(repeating: Array(repeating: -1, count: 6), count: 6)

            for i in 0..<m where !(i < fixedColor.count && fixedColor[i] >= 0) {
                let u = assign[i]
                guard u >= 0 && u < 6 else { continue }
                for v in 0..<6 where v != u {
                    let delta = cost[i][v] - cost[i][u]
                    if delta < w[u][v] {
                        w[u][v] = delta
                        arg[u][v] = i
                    }
                }
            }

            var dist = Array(repeating: 0.0, count: 6)
            var parent = Array(repeating: -1, count: 6)
            var lastRelaxed = -1
            for _ in 0..<6 {
                lastRelaxed = -1
                for u in 0..<6 {
                    for v in 0..<6 where arg[u][v] >= 0 {
                        if dist[u] + w[u][v] < dist[v] - 1e-9 {
                            dist[v] = dist[u] + w[u][v]
                            parent[v] = u
                            lastRelaxed = v
                        }
                    }
                }
            }
            if lastRelaxed == -1 { break }

            var cur = lastRelaxed
            for _ in 0..<6 {
                guard cur >= 0 else { break }
                cur = parent[cur]
            }
            guard cur >= 0 else { break }

            var cycle: [Int] = []
            let start = cur
            repeat {
                cycle.append(cur)
                cur = parent[cur]
            } while cur >= 0 && cur != start && cycle.count <= 7
            guard cur == start, cycle.count >= 2 else { break }
            cycle.reverse()

            var updates: [(item: Int, toColor: Int)] = []
            for idx in 0..<cycle.count {
                let u = cycle[idx]
                let v = cycle[(idx + 1) % cycle.count]
                let item = arg[u][v]
                if item >= 0 {
                    updates.append((item, v))
                }
            }
            for up in updates {
                assign[up.item] = up.toColor
            }
        }
        return assign
    }

    public static func refineSixFaces(faceRGBs: [[RGBSample]], cubeSize: Int) -> [[Int]]? {
        let faceCount = cubeSize * cubeSize
        let total = 6 * faceCount
        guard faceRGBs.count == 6, faceRGBs.allSatisfy({ $0.count == faceCount }) else { return nil }

        let allRGBs = faceRGBs.flatMap { $0 }
        let isOdd = (cubeSize % 2 == 1)
        let centerIdx = faceCount / 2

        let whiteRef: RGBSample
        if isOdd {
            whiteRef = faceRGBs[0][centerIdx]
        } else {
            whiteRef = estimateWhiteCandidate(from: allRGBs, takeCount: faceCount, requireNeutralThreshold: false) ?? .neutralWhite
        }

        let globalLabs = allRGBs.map { $0.toVonKriesLab(whiteRef: whiteRef) }
        var anchors: [LabColor] = (0..<6).map { c in
            isOdd ? globalLabs[c * faceCount + centerIdx] : standardCentroids[c]
        }

        var fixedColor = Array(repeating: -1, count: total)
        if isOdd {
            for f in 0..<6 {
                fixedColor[f * faceCount + centerIdx] = f
            }
        }

        var pred = Array(repeating: 0, count: total)
        for _ in 0..<3 {
            var cost = Array(repeating: Array(repeating: 0.0, count: 6), count: total)
            for i in 0..<total {
                for c in 0..<6 {
                    cost[i][c] = rubikDistance(sample: globalLabs[i], anchor: anchors[c], colorId: c, hasWhiteCalib: true)
                }
            }
            pred = balancedAssign(cost: cost, fixedColor: fixedColor)

            for c in 0..<6 {
                var ls: [Double] = []
                var asVals: [Double] = []
                var bsVals: [Double] = []
                for i in 0..<total where pred[i] == c {
                    ls.append(globalLabs[i].L)
                    asVals.append(globalLabs[i].a)
                    bsVals.append(globalLabs[i].b)
                }
                guard !ls.isEmpty else { continue }
                ls.sort(); asVals.sort(); bsVals.sort()
                let mid = ls.count / 2
                let med = LabColor(L: ls[mid], a: asVals[mid], b: bsVals[mid])
                if isOdd {
                    let cen = globalLabs[c * faceCount + centerIdx]
                    anchors[c] = LabColor(L: 0.7 * med.L + 0.3 * cen.L,
                                          a: 0.7 * med.a + 0.3 * cen.a,
                                          b: 0.7 * med.b + 0.3 * cen.b)
                } else {
                    let wPrior = (cubeSize == 2) ? 0.25 : 0.15
                    anchors[c] = LabColor(
                        L: (1.0 - wPrior) * med.L + wPrior * standardCentroids[c].L,
                        a: (1.0 - wPrior) * med.a + wPrior * standardCentroids[c].a,
                        b: (1.0 - wPrior) * med.b + wPrior * standardCentroids[c].b
                    )
                }
            }
        }

        return (0..<6).map { f in
            let start = f * faceCount
            return Array(pred[start..<(start + faceCount)])
        }
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
    struct CalibrationSnapshot: Sendable {
        let cubeSize: Int
        let capturedPool: [RGBSample]
        let capturedCenters: [RGBSample?]
        let hasWhiteCenter: Bool
        let whiteCenterRGB: RGBSample?
    }

    private let lock = NSLock()
    private var _isConfigured: Bool = false
    private var _samplingCubeSize: Int = 3
    private var _lastSampleTimestamp: CFTimeInterval = 0
    private var _videoOutput: AVCaptureVideoDataOutput?

    private var _emaTileRGBs: [RGBSample] = []
    private var _capturedFaceRGBs: [[RGBSample]] = Array(repeating: [], count: 6)
    private var _capturedCenters: [RGBSample?] = Array(repeating: nil, count: 6)

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
        _emaTileRGBs = []
        _capturedFaceRGBs = Array(repeating: [], count: 6)
        _capturedCenters = Array(repeating: nil, count: 6)
    }

    func resetCalibration() {
        lock.lock(); defer { lock.unlock() }
        _emaTileRGBs = []
        _capturedFaceRGBs = Array(repeating: [], count: 6)
        _capturedCenters = Array(repeating: nil, count: 6)
    }

    func resetLiveEMA() {
        lock.lock(); defer { lock.unlock() }
        _emaTileRGBs = []
    }

    func recordCapturedFace(_ faceIdx: Int, rgbs: [RGBSample], cubeSize: Int) {
        guard faceIdx >= 0 && faceIdx < 6 else { return }
        lock.lock(); defer { lock.unlock() }
        _capturedFaceRGBs[faceIdx] = rgbs
        let nn = cubeSize * cubeSize
        if cubeSize % 2 == 1 && rgbs.count == nn {
            _capturedCenters[faceIdx] = rgbs[nn / 2]
        }
        _emaTileRGBs = []
    }

    func tryBeginSample(now: CFTimeInterval, minInterval: CFTimeInterval = 0.08) -> Int? {
        lock.lock(); defer { lock.unlock() }
        guard (now - _lastSampleTimestamp) >= minInterval else { return nil }
        _lastSampleTimestamp = now
        return _samplingCubeSize
    }

    func smoothTiles(_ rawTiles: [RGBSample]) -> [RGBSample] {
        lock.lock(); defer { lock.unlock() }
        if _emaTileRGBs.count != rawTiles.count {
            _emaTileRGBs = rawTiles
            return rawTiles
        }
        // Reset EMA immediately if user rotates to a new face (large RGB jump)
        var diffSum = 0.0
        for i in 0..<rawTiles.count {
            let dr = rawTiles[i].r - _emaTileRGBs[i].r
            let dg = rawTiles[i].g - _emaTileRGBs[i].g
            let db = rawTiles[i].b - _emaTileRGBs[i].b
            diffSum += sqrt(dr * dr + dg * dg + db * db)
        }
        if (diffSum / Double(max(1, rawTiles.count))) > 42.0 {
            _emaTileRGBs = rawTiles
            return rawTiles
        }
        let alpha = 0.45
        for i in 0..<rawTiles.count {
            let prev = _emaTileRGBs[i]
            let cur = rawTiles[i]
            _emaTileRGBs[i] = RGBSample(
                r: alpha * cur.r + (1.0 - alpha) * prev.r,
                g: alpha * cur.g + (1.0 - alpha) * prev.g,
                b: alpha * cur.b + (1.0 - alpha) * prev.b
            )
        }
        return _emaTileRGBs
    }

    func snapshotCalibration() -> CalibrationSnapshot {
        lock.lock(); defer { lock.unlock() }
        let pool = _capturedFaceRGBs.flatMap { $0 }
        let whiteCenter = _capturedCenters[0]
        return CalibrationSnapshot(
            cubeSize: _samplingCubeSize,
            capturedPool: pool,
            capturedCenters: _capturedCenters,
            hasWhiteCenter: whiteCenter != nil,
            whiteCenterRGB: whiteCenter
        )
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
    private var liveSampledRGBs: [RGBSample] = []
    private var savedFaceRGBs: [[RGBSample]] = Array(repeating: [], count: 6)

    private var motionManager: CMMotionManager?
    private var hapticFeedback: UIImpactFeedbackGenerator?

    private var lastMotionTimestamp: TimeInterval = 0
    private var isStableFor035s: Bool = false
    private var lastAngularVelocity: Double = 0.0

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

                // Stabilize focus, exposure, and white balance on the center Rubik's reticle
                do {
                    try device.lockForConfiguration()
                    let centerPoint = CGPoint(x: 0.5, y: 0.5)
                    if device.isFocusPointOfInterestSupported {
                        device.focusPointOfInterest = centerPoint
                    }
                    if device.isFocusModeSupported(.continuousAutoFocus) {
                        device.focusMode = .continuousAutoFocus
                    }
                    if device.isExposurePointOfInterestSupported {
                        device.exposurePointOfInterest = centerPoint
                    }
                    if device.isExposureModeSupported(.continuousAutoExposure) {
                        device.exposureMode = .continuousAutoExposure
                    }
                    if device.isWhiteBalanceModeSupported(.continuousAutoWhiteBalance) {
                        device.whiteBalanceMode = .continuousAutoWhiteBalance
                    }
                    device.unlockForConfiguration()
                } catch {
                    // Continue with default device settings if configuration lock fails
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
        savedFaceRGBs = Array(repeating: [], count: 6)
        liveSampledRGBs = []
        liveSampledColors = []
        liveClassifiedColors = []
        isScanningComplete = false
        syncState.resetCalibration()
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
        self.savedFaceRGBs = Array(repeating: [], count: 6)
        self.liveSampledRGBs = []
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
        syncState.resetLiveEMA()
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
        if liveSampledRGBs.count == expectedCount {
            savedFaceRGBs[currentFaceIndex] = liveSampledRGBs
            syncState.recordCapturedFace(currentFaceIndex, rgbs: liveSampledRGBs, cubeSize: cubeSize)
        } else {
            syncState.resetLiveEMA()
        }
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
            refineAllSixFacesGlobal()
            isScanningComplete = true
        }
    }

    public func refineAllSixFacesGlobal() {
        if let refined = RubikColorClassifier.refineSixFaces(faceRGBs: savedFaceRGBs, cubeSize: cubeSize) {
            savedFacelets = refined
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
        return RubikColorClassifier.nearestColorIndex(lab)
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

        var rawTiles: [RGBSample] = []
        rawTiles.reserveCapacity(n * n)

        // Focus strictly on inner ~46% of each sticker to avoid black plastic grid lines and rounded bevel shadows.
        let inset = max(1, Int(Double(boxSize) * 0.27))
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

                var samples: [(lum: Double, r: Double, g: Double, b: Double)] = []
                samples.reserveCapacity(36)

                var y = minY
                while y < maxY {
                    let rowBase = y * bytesPerRow
                    var x = minX
                    while x < maxX {
                        let px = rowBase + x * 4
                        let b = Double(rawPtr[px])
                        let g = Double(rawPtr[px + 1])
                        let r = Double(rawPtr[px + 2])
                        let lum = 0.299 * r + 0.587 * g + 0.114 * b
                        samples.append((lum, r, g, b))
                        x += stepX
                    }
                    y += stepY
                }

                if samples.isEmpty {
                    rawTiles.append(RGBSample(r: 128.0, g: 128.0, b: 128.0))
                } else {
                    samples.sort { $0.lum < $1.lum }
                    // Trimmed-median: discard darkest 20% (shadow) and brightest 20% (specular glare)
                    let drop = samples.count >= 5 ? (samples.count / 5) : 0
                    let slice = Array(samples[drop..<(samples.count - drop)])
                    let rs = slice.map { $0.r }.sorted()
                    let gs = slice.map { $0.g }.sorted()
                    let bs = slice.map { $0.b }.sorted()
                    let mid = rs.count / 2
                    rawTiles.append(RGBSample(r: rs[mid], g: gs[mid], b: bs[mid]))
                }
            }
        }

        let smoothedTiles = syncState.smoothTiles(rawTiles)
        let snap = syncState.snapshotCalibration()
        guard snap.cubeSize == n else { return }

        let livePool = snap.capturedPool + smoothedTiles
        let detectedWhite: RGBSample?
        if snap.hasWhiteCenter, let wc = snap.whiteCenterRGB {
            detectedWhite = wc
        } else {
            detectedWhite = RubikColorClassifier.estimateWhiteCandidate(
                from: livePool,
                takeCount: max(1, livePool.count / 6),
                requireNeutralThreshold: true
            )
        }

        // Fallback for dim low-light scenes before any white sticker has appeared: isotropic gain only (preserves R:G:B hue)
        let isoGain: Double
        if detectedWhite == nil {
            let lums = smoothedTiles.map { $0.luminance }.sorted()
            let p90Lum = lums[max(0, (lums.count * 9) / 10 - 1)]
            isoGain = p90Lum < 140.0 ? max(1.0, min(2.8, 210.0 / max(45.0, p90Lum))) : 1.0
        } else {
            isoGain = 1.0
        }

        @inline(__always)
        func toLiveLab(_ rgb: RGBSample) -> LabColor {
            if let wr = detectedWhite {
                return rgb.toVonKriesLab(whiteRef: wr)
            } else {
                return rgb.toIsotropicLab(gain: isoGain)
            }
        }

        let isOdd = (n % 2 == 1)
        var anchors = RubikColorClassifier.standardCentroids
        if isOdd {
            for c in 0..<6 {
                if let cenRGB = snap.capturedCenters[c] {
                    anchors[c] = toLiveLab(cenRGB)
                }
            }
        }

        let hasWhiteCalib = (detectedWhite != nil)
        let sampledColors = smoothedTiles.map { toLiveLab($0) }
        let classifiedColors = sampledColors.map {
            RubikColorClassifier.nearestColorIndex($0, anchors: anchors, hasWhiteCalib: hasWhiteCalib)
        }

        Task { @MainActor [weak self] in
            guard let self = self, self.isSessionRunning, self.cubeSize == n else { return }
            self.liveSampledRGBs = smoothedTiles
            self.liveSampledColors = sampledColors
            self.liveClassifiedColors = classifiedColors
        }
    }

    public func getFaceletsCsv() -> String {
        return getFlatFacelets().map(String.init).joined(separator: ",")
    }
}
