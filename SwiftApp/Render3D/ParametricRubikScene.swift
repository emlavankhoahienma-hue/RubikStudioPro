import Foundation
import SceneKit
import UIKit
import Combine

public struct CubieCoord: Hashable, Sendable {
    public var x: Int
    public var y: Int
    public var z: Int

    public init(x: Int, y: Int, z: Int) {
        self.x = x
        self.y = y
        self.z = z
    }
}

@MainActor
public class ParametricRubikScene: ObservableObject {
    public let scene = SCNScene()
    public let cameraNode = SCNNode()
    public let cubeNode = SCNNode()

    @Published public var cubeSize: Int = 3
    @Published public var isAnimating: Bool = false

    private var cubieNodes: [SCNNode] = []
    private var nodeCoords: [SCNNode: CubieCoord] = [:]
    private var activePivotNode: SCNNode?
    private var animationToken: Int = 0
    private let cubieSpacing: Float = 1.05
    private let arrowOrangeColor = UIColor(red: 1.0, green: 0.341, blue: 0.133, alpha: 1.0) // #FF5722

    // Standard Face Colors: [0:U, 1:D, 2:L, 3:R, 4:F, 5:B]
    public static let faceColors: [UIColor] = [
        UIColor(red: 0.96, green: 0.96, blue: 0.96, alpha: 1.0), // 0: U (White)
        UIColor(red: 1.00, green: 0.84, blue: 0.00, alpha: 1.0), // 1: D (Yellow)
        UIColor(red: 1.00, green: 0.42, blue: 0.00, alpha: 1.0), // 2: L (Orange)
        UIColor(red: 0.86, green: 0.15, blue: 0.15, alpha: 1.0), // 3: R (Red)
        UIColor(red: 0.09, green: 0.64, blue: 0.29, alpha: 1.0), // 4: F (Green)
        UIColor(red: 0.15, green: 0.39, blue: 0.92, alpha: 1.0)  // 5: B (Blue)
    ]

    // MARK: - Canonical WCA Facelet Simulator (matches CoreCpp 100%)
    public static func solvedFacelets(size n: Int) -> [Int] {
        var arr: [Int] = []
        let nn = n * n
        arr.reserveCapacity(6 * nn)
        for face in 0..<6 {
            for _ in 0..<nn {
                arr.append(face)
            }
        }
        return arr
    }

    private static func rotateFaceCW(_ f: inout [Int], faceIdx: Int, n: Int) {
        let base = faceIdx * n * n
        var tmp = Array(repeating: 0, count: n * n)
        for r in 0..<n {
            for c in 0..<n {
                tmp[r * n + c] = f[base + (n - 1 - c) * n + r]
            }
        }
        for i in 0..<(n * n) {
            f[base + i] = tmp[i]
        }
    }

    private static func cycle4(_ f: inout [Int], _ i0: Int, _ i1: Int, _ i2: Int, _ i3: Int) {
        let t = f[i0]
        f[i0] = f[i3]
        f[i3] = f[i2]
        f[i2] = f[i1]
        f[i1] = t
    }

    public static func applyMove(to f: inout [Int], size n: Int, axis: Int, sliceMask: Int, power: Int) {
        guard n >= 2, n <= 5, axis >= 0, axis < 6, power >= 1, power <= 3 else { return }
        let expectedCount = 6 * n * n
        guard f.count == expectedCount else { return }
        let nn = n * n

        for _ in 0..<power {
            if (sliceMask & 0x01) != 0 {
                rotateFaceCW(&f, faceIdx: axis, n: n)
            }
            for layer in 0..<n {
                if (sliceMask & (1 << layer)) == 0 { continue }
                if layer == n - 1 {
                    let opp = axis ^ 1
                    rotateFaceCW(&f, faceIdx: opp, n: n)
                    rotateFaceCW(&f, faceIdx: opp, n: n)
                    rotateFaceCW(&f, faceIdx: opp, n: n)
                }
                switch axis {
                case 0: // U: F -> L -> B -> R -> F
                    let r = layer
                    for c in 0..<n {
                        cycle4(&f, 4 * nn + r * n + c,
                                   2 * nn + r * n + c,
                                   5 * nn + r * n + c,
                                   3 * nn + r * n + c)
                    }
                case 1: // D: F -> R -> B -> L -> F
                    let r = n - 1 - layer
                    for c in 0..<n {
                        cycle4(&f, 4 * nn + r * n + c,
                                   3 * nn + r * n + c,
                                   5 * nn + r * n + c,
                                   2 * nn + r * n + c)
                    }
                case 2: // L: U -> F -> D -> B(rev) -> U
                    let c = layer
                    for r in 0..<n {
                        cycle4(&f, 0 * nn + r * n + c,
                                   4 * nn + r * n + c,
                                   1 * nn + r * n + c,
                                   5 * nn + (n - 1 - r) * n + (n - 1 - c))
                    }
                case 3: // R: U -> B(rev) -> D -> F -> U
                    let c = n - 1 - layer
                    for r in 0..<n {
                        cycle4(&f, 0 * nn + r * n + c,
                                   5 * nn + (n - 1 - r) * n + (n - 1 - c),
                                   1 * nn + r * n + c,
                                   4 * nn + r * n + c)
                    }
                case 4: // F: U -> R -> D -> L -> U
                    let off = layer
                    for i in 0..<n {
                        cycle4(&f, 0 * nn + (n - 1 - off) * n + i,
                                   3 * nn + i * n + off,
                                   1 * nn + off * n + (n - 1 - i),
                                   2 * nn + (n - 1 - i) * n + (n - 1 - off))
                    }
                case 5: // B: U -> L -> D -> R -> U
                    let off = layer
                    for i in 0..<n {
                        cycle4(&f, 0 * nn + off * n + (n - 1 - i),
                                   2 * nn + i * n + off,
                                   1 * nn + (n - 1 - off) * n + i,
                                   3 * nn + (n - 1 - i) * n + (n - 1 - off))
                    }
                default: break
                }
            }
        }
    }

    public static func stateAfterMoves(initialFacelets: [Int], size n: Int, moves: [MoveModel], count: Int) -> [Int] {
        var state = (initialFacelets.count == 6 * n * n) ? initialFacelets : solvedFacelets(size: n)
        let clamped = max(0, min(count, moves.count))
        for i in 0..<clamped {
            let m = moves[i]
            applyMove(to: &state, size: n, axis: m.axis, sliceMask: m.sliceMask, power: m.power)
        }
        return state
    }

    // MARK: - Scene Setup
    public init(cubeSize: Int = 3) {
        self.cubeSize = cubeSize
        setupScene()
        buildCube(size: cubeSize, facelets: nil)
    }

    private func setupScene() {
        scene.background.contents = UIColor(red: 0.05, green: 0.055, blue: 0.067, alpha: 1.0) // #0D0E11

        cameraNode.camera = SCNCamera()
        cameraNode.camera?.fieldOfView = 45
        adjustCameraDistance(for: cubeSize)
        scene.rootNode.addChildNode(cameraNode)

        let mainLight = SCNNode()
        mainLight.light = SCNLight()
        mainLight.light?.type = .omni
        mainLight.light?.intensity = 1100
        mainLight.position = SCNVector3(x: 10, y: 15, z: 14)
        scene.rootNode.addChildNode(mainLight)

        let fillLight = SCNNode()
        fillLight.light = SCNLight()
        fillLight.light?.type = .omni
        fillLight.light?.intensity = 550
        fillLight.position = SCNVector3(x: -10, y: -10, z: -10)
        scene.rootNode.addChildNode(fillLight)

        let ambientLight = SCNNode()
        ambientLight.light = SCNLight()
        ambientLight.light?.type = .ambient
        ambientLight.light?.color = UIColor(white: 0.48, alpha: 1.0)
        scene.rootNode.addChildNode(ambientLight)

        scene.rootNode.addChildNode(cubeNode)
    }

    public func resetCameraView() {
        adjustCameraDistance(for: cubeSize)
    }

    private func adjustCameraDistance(for size: Int) {
        let dist: Float = (size == 2) ? 6.2 : ((size == 3) ? 7.8 : ((size == 4) ? 9.8 : 12.0))
        cameraNode.position = SCNVector3(x: dist * 0.6, y: dist * 0.7, z: dist * 0.9)
        cameraNode.look(at: SCNVector3(0, 0, 0))
    }

    public func cancelAnimations() {
        animationToken += 1
        activePivotNode?.removeAllActions()
        activePivotNode?.removeFromParentNode()
        activePivotNode = nil
        isAnimating = false
    }

    public func rebuildCube(size: Int, facelets: [Int]? = nil) {
        cancelAnimations()
        let clamped = max(2, min(5, size))
        let sizeChanged = (self.cubeSize != clamped)
        self.cubeSize = clamped
        if sizeChanged {
            adjustCameraDistance(for: clamped)
        }
        buildCube(size: clamped, facelets: facelets)
    }

    private func makeStickerMaterial(colorId: Int) -> SCNMaterial {
        let m = SCNMaterial()
        if colorId >= 0 && colorId < ParametricRubikScene.faceColors.count {
            m.diffuse.contents = ParametricRubikScene.faceColors[colorId]
        } else {
            m.diffuse.contents = UIColor.darkGray
        }
        m.roughness.contents = 0.22
        return m
    }

    private func buildCube(size n: Int, facelets: [Int]?) {
        cubeNode.childNodes.forEach { $0.removeFromParentNode() }
        cubieNodes.removeAll()
        nodeCoords.removeAll()

        let nn = n * n
        let activeFacelets = (facelets?.count == 6 * nn) ? facelets! : Self.solvedFacelets(size: n)
        let offset = Float(n - 1) / 2.0

        let blackMat = SCNMaterial()
        blackMat.diffuse.contents = UIColor(red: 0.08, green: 0.08, blue: 0.09, alpha: 1.0)
        blackMat.roughness.contents = 0.35

        for x in 0..<n {
            for y in 0..<n {
                for z in 0..<n {
                    let isInterior = (x > 0 && x < n - 1) &&
                                     (y > 0 && y < n - 1) &&
                                     (z > 0 && z < n - 1)
                    if isInterior { continue }

                    let box = SCNBox(width: 1.0, height: 1.0, length: 1.0, chamferRadius: 0.08)
                    // 6 materials: [0:Front(+Z), 1:Right(+X), 2:Back(-Z), 3:Left(-X), 4:Top(+Y), 5:Bottom(-Y)]
                    var materials = Array(repeating: blackMat, count: 6)

                    // Front (face 4, +Z): row = n-1-y, col = x
                    if z == n - 1 {
                        let cId = activeFacelets[4 * nn + (n - 1 - y) * n + x]
                        materials[0] = makeStickerMaterial(colorId: cId)
                    }
                    // Right (face 3, +X): row = n-1-y, col = n-1-z
                    if x == n - 1 {
                        let cId = activeFacelets[3 * nn + (n - 1 - y) * n + (n - 1 - z)]
                        materials[1] = makeStickerMaterial(colorId: cId)
                    }
                    // Back (face 5, -Z): row = n-1-y, col = n-1-x
                    if z == 0 {
                        let cId = activeFacelets[5 * nn + (n - 1 - y) * n + (n - 1 - x)]
                        materials[2] = makeStickerMaterial(colorId: cId)
                    }
                    // Left (face 2, -X): row = n-1-y, col = z
                    if x == 0 {
                        let cId = activeFacelets[2 * nn + (n - 1 - y) * n + z]
                        materials[3] = makeStickerMaterial(colorId: cId)
                    }
                    // Top (face 0, +Y): row = z, col = x
                    if y == n - 1 {
                        let cId = activeFacelets[0 * nn + z * n + x]
                        materials[4] = makeStickerMaterial(colorId: cId)
                    }
                    // Bottom (face 1, -Y): row = n-1-z, col = x
                    if y == 0 {
                        let cId = activeFacelets[1 * nn + (n - 1 - z) * n + x]
                        materials[5] = makeStickerMaterial(colorId: cId)
                    }

                    box.materials = materials

                    let cubie = SCNNode(geometry: box)
                    cubie.name = "cubie_\(x)_\(y)_\(z)"
                    cubie.position = SCNVector3(
                        x: (Float(x) - offset) * cubieSpacing,
                        y: (Float(y) - offset) * cubieSpacing,
                        z: (Float(z) - offset) * cubieSpacing
                    )
                    cubeNode.addChildNode(cubie)
                    cubieNodes.append(cubie)
                    nodeCoords[cubie] = CubieCoord(x: x, y: y, z: z)
                }
            }
        }
    }

    // Animate a single move and update logical grid positions
    public func animateMove(axis: Int, sliceMask: Int, power: Int, duration: TimeInterval, completion: (() -> Void)? = nil) {
        guard !isAnimating else { return }
        isAnimating = true
        let token = animationToken

        let n = cubeSize
        var affectedCubies: [SCNNode] = []

        for cubie in cubieNodes {
            guard let c = nodeCoords[cubie] else { continue }
            var matches = false
            for layer in 0..<n {
                if (sliceMask & (1 << layer)) != 0 {
                    switch axis {
                    case 0: if c.y == (n - 1 - layer) { matches = true }
                    case 1: if c.y == layer { matches = true }
                    case 2: if c.x == layer { matches = true }
                    case 3: if c.x == (n - 1 - layer) { matches = true }
                    case 4: if c.z == (n - 1 - layer) { matches = true }
                    case 5: if c.z == layer { matches = true }
                    default: break
                    }
                }
            }
            if matches {
                affectedCubies.append(cubie)
            }
        }

        let pivotNode = SCNNode()
        activePivotNode = pivotNode
        cubeNode.addChildNode(pivotNode)

        for cubie in affectedCubies {
            let localTransform = cubie.transform
            cubie.removeFromParentNode()
            pivotNode.addChildNode(cubie)
            cubie.transform = localTransform
        }

        let arrowNode = createCurvedArrowNode(axis: axis)
        pivotNode.addChildNode(arrowNode)

        let baseAngle: Float = (power == 2) ? .pi : ((power == 3) ? -.pi / 2.0 : .pi / 2.0)
        var rotAxis = SCNVector3(0, 1, 0)
        switch axis {
        case 0: rotAxis = SCNVector3(0, -1, 0) // U CW looking from +Y
        case 1: rotAxis = SCNVector3(0, 1, 0)  // D CW looking from -Y
        case 2: rotAxis = SCNVector3(1, 0, 0)  // L CW looking from -X
        case 3: rotAxis = SCNVector3(-1, 0, 0) // R CW looking from +X
        case 4: rotAxis = SCNVector3(0, 0, -1) // F CW looking from +Z
        case 5: rotAxis = SCNVector3(0, 0, 1)  // B CW looking from -Z
        default: break
        }

        let rotationAction = SCNAction.rotate(by: CGFloat(baseAngle), around: rotAxis, duration: max(0.02, duration))
        rotationAction.timingMode = .easeInEaseOut

        pivotNode.runAction(rotationAction) { [weak self] in
            Task { @MainActor [weak self] in
                guard let self = self, self.animationToken == token else { return }
                arrowNode.removeFromParentNode()

                let offset = Float(n - 1) / 2.0
                for cubie in affectedCubies {
                    let finalTransform = pivotNode.convertTransform(cubie.transform, to: self.cubeNode)
                    cubie.removeFromParentNode()
                    self.cubeNode.addChildNode(cubie)
                    cubie.transform = finalTransform

                    let gx = max(0, min(n - 1, Int(round((cubie.position.x / self.cubieSpacing) + offset))))
                    let gy = max(0, min(n - 1, Int(round((cubie.position.y / self.cubieSpacing) + offset))))
                    let gz = max(0, min(n - 1, Int(round((cubie.position.z / self.cubieSpacing) + offset))))
                    cubie.position = SCNVector3(
                        x: (Float(gx) - offset) * self.cubieSpacing,
                        y: (Float(gy) - offset) * self.cubieSpacing,
                        z: (Float(gz) - offset) * self.cubieSpacing
                    )
                    self.nodeCoords[cubie] = CubieCoord(x: gx, y: gy, z: gz)
                }

                pivotNode.removeFromParentNode()
                if self.activePivotNode === pivotNode {
                    self.activePivotNode = nil
                }
                self.isAnimating = false
                completion?()
            }
        }
    }

    private func createCurvedArrowNode(axis: Int) -> SCNNode {
        let ringR: CGFloat = CGFloat(Float(cubeSize) * 0.55 + 0.3)
        let torus = SCNTorus(ringRadius: ringR, pipeRadius: 0.05)
        let mat = SCNMaterial()
        mat.diffuse.contents = arrowOrangeColor
        mat.emission.contents = arrowOrangeColor.withAlphaComponent(0.6)
        torus.materials = [mat]

        let node = SCNNode(geometry: torus)
        switch axis {
        case 0, 1:
            node.eulerAngles = SCNVector3(0, 0, 0)
        case 2, 3:
            node.eulerAngles = SCNVector3(0, 0, Float.pi / 2.0)
        case 4, 5:
            node.eulerAngles = SCNVector3(Float.pi / 2.0, 0, 0)
        default: break
        }
        return node
    }
}
