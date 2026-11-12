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

public class CubieNode: SCNNode {
    public var gridCoord: CubieCoord

    public init(coord: CubieCoord, geometry: SCNGeometry) {
        self.gridCoord = coord
        super.init()
        self.geometry = geometry
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}

@MainActor
public class ParametricRubikScene: ObservableObject {
    public let scene = SCNScene()
    public let cameraNode = SCNNode()
    public let cubeNode = SCNNode()

    @Published public var cubeSize: Int = 3
    @Published public var isAnimating: Bool = false

    private var cubieNodes: [CubieNode] = []
    private let cubieSpacing: Float = 1.05
    private let arrowOrangeColor = UIColor(red: 1.0, green: 0.341, blue: 0.133, alpha: 1.0) // #FF5722

    // Standard Face Colors
    public static let faceColors: [UIColor] = [
        UIColor(red: 0.96, green: 0.96, blue: 0.96, alpha: 1.0), // 0: U (White)
        UIColor(red: 1.00, green: 0.84, blue: 0.00, alpha: 1.0), // 1: D (Yellow)
        UIColor(red: 1.00, green: 0.42, blue: 0.00, alpha: 1.0), // 2: L (Orange)
        UIColor(red: 0.86, green: 0.15, blue: 0.15, alpha: 1.0), // 3: R (Red)
        UIColor(red: 0.09, green: 0.64, blue: 0.29, alpha: 1.0), // 4: F (Green)
        UIColor(red: 0.15, green: 0.39, blue: 0.92, alpha: 1.0)  // 5: B (Blue)
    ]

    public init(cubeSize: Int = 3) {
        self.cubeSize = cubeSize
        setupScene()
        buildCube(size: cubeSize)
    }

    private func setupScene() {
        scene.background.contents = UIColor(red: 0.05, green: 0.055, blue: 0.067, alpha: 1.0) // #0D0E11

        // Camera setup
        cameraNode.camera = SCNCamera()
        cameraNode.camera?.fieldOfView = 45
        cameraNode.position = SCNVector3(x: 4.5, y: 5.5, z: 7.0)
        cameraNode.look(at: SCNVector3(0, 0, 0))
        scene.rootNode.addChildNode(cameraNode)

        // Lighting
        let omniLightNode = SCNNode()
        omniLightNode.light = SCNLight()
        omniLightNode.light?.type = .omni
        omniLightNode.light?.intensity = 1000
        omniLightNode.position = SCNVector3(x: 10, y: 15, z: 12)
        scene.rootNode.addChildNode(omniLightNode)

        let ambientLightNode = SCNNode()
        ambientLightNode.light = SCNLight()
        ambientLightNode.light?.type = .ambient
        ambientLightNode.light?.color = UIColor(white: 0.45, alpha: 1.0)
        scene.rootNode.addChildNode(ambientLightNode)

        scene.rootNode.addChildNode(cubeNode)
    }

    public func rebuildCube(size: Int) {
        self.cubeSize = size
        buildCube(size: size)
    }

    private func buildCube(size: Int) {
        cubeNode.childNodes.forEach { $0.removeFromParentNode() }
        cubieNodes.removeAll()

        let offset = Float(size - 1) / 2.0

        for x in 0..<size {
            for y in 0..<size {
                for z in 0..<size {
                    // Skip completely interior cubies to optimize rendering
                    let isInterior = (x > 0 && x < size - 1) &&
                                     (y > 0 && y < size - 1) &&
                                     (z > 0 && z < size - 1)
                    if isInterior { continue }

                    let box = SCNBox(width: 1.0, height: 1.0, length: 1.0, chamferRadius: 0.08)

                    // Black matte plastic base
                    let blackMat = SCNMaterial()
                    blackMat.diffuse.contents = UIColor(red: 0.08, green: 0.08, blue: 0.09, alpha: 1.0)
                    blackMat.roughness.contents = 0.35

                    // 6 materials for box faces: [Front, Right, Back, Left, Top, Bottom]
                    var materials = Array(repeating: blackMat, count: 6)

                    // Front (+Z): index 0
                    if z == size - 1 {
                        let m = SCNMaterial()
                        m.diffuse.contents = ParametricRubikScene.faceColors[4] // Green
                        m.roughness.contents = 0.2
                        materials[0] = m
                    }
                    // Right (+X): index 1
                    if x == size - 1 {
                        let m = SCNMaterial()
                        m.diffuse.contents = ParametricRubikScene.faceColors[3] // Red
                        m.roughness.contents = 0.2
                        materials[1] = m
                    }
                    // Back (-Z): index 2
                    if z == 0 {
                        let m = SCNMaterial()
                        m.diffuse.contents = ParametricRubikScene.faceColors[5] // Blue
                        m.roughness.contents = 0.2
                        materials[2] = m
                    }
                    // Left (-X): index 3
                    if x == 0 {
                        let m = SCNMaterial()
                        m.diffuse.contents = ParametricRubikScene.faceColors[2] // Orange
                        m.roughness.contents = 0.2
                        materials[3] = m
                    }
                    // Top (+Y): index 4
                    if y == size - 1 {
                        let m = SCNMaterial()
                        m.diffuse.contents = ParametricRubikScene.faceColors[0] // White
                        m.roughness.contents = 0.2
                        materials[4] = m
                    }
                    // Bottom (-Y): index 5
                    if y == 0 {
                        let m = SCNMaterial()
                        m.diffuse.contents = ParametricRubikScene.faceColors[1] // Yellow
                        m.roughness.contents = 0.2
                        materials[5] = m
                    }

                    box.materials = materials

                    let cubie = CubieNode(coord: CubieCoord(x: x, y: y, z: z), geometry: box)
                    cubie.position = SCNVector3(
                        x: (Float(x) - offset) * cubieSpacing,
                        y: (Float(y) - offset) * cubieSpacing,
                        z: (Float(z) - offset) * cubieSpacing
                    )
                    cubeNode.addChildNode(cubie)
                    cubieNodes.append(cubie)
                }
            }
        }
    }

    // Animate a move and update logical grid positions
    public func animateMove(axis: Int, sliceMask: Int, power: Int, duration: TimeInterval, completion: (() -> Void)? = nil) {
        guard !isAnimating else { return }
        isAnimating = true

        let n = cubeSize
        var affectedCubies: [CubieNode] = []

        // Find cubies matching axis and sliceMask
        for cubie in cubieNodes {
            let c = cubie.gridCoord
            var matches = false
            for layer in 0..<3 {
                if (sliceMask & (1 << layer)) != 0 {
                    switch axis {
                    case 0: // U: layer from top
                        if c.y == (n - 1 - layer) { matches = true }
                    case 1: // D: layer from bottom
                        if c.y == layer { matches = true }
                    case 2: // L: layer from left
                        if c.x == layer { matches = true }
                    case 3: // R: layer from right
                        if c.x == (n - 1 - layer) { matches = true }
                    case 4: // F: layer from front
                        if c.z == (n - 1 - layer) { matches = true }
                    case 5: // B: layer from back
                        if c.z == layer { matches = true }
                    default: break
                    }
                }
            }
            if matches {
                affectedCubies.append(cubie)
            }
        }

        // Attach to temporary pivotNode
        let pivotNode = SCNNode()
        cubeNode.addChildNode(pivotNode)

        for cubie in affectedCubies {
            let worldTransform = cubie.worldTransform
            cubie.removeFromParentNode()
            pivotNode.addChildNode(cubie)
            cubie.transform = cubeNode.convertTransform(worldTransform, to: pivotNode)
        }

        // Add 3D curved indicator arrow
        let arrowNode = createCurvedArrowNode(axis: axis, power: power)
        pivotNode.addChildNode(arrowNode)

        // Rotation angles
        let baseAngle: Float = (power == 2) ? .pi : ((power == 3) ? -.pi / 2.0 : .pi / 2.0)
        var rotAxis = SCNVector3(0, 0, 0)
        switch axis {
        case 0: rotAxis = SCNVector3(0, -1, 0) // U turns clockwise looking from top
        case 1: rotAxis = SCNVector3(0, 1, 0)  // D
        case 2: rotAxis = SCNVector3(1, 0, 0)  // L
        case 3: rotAxis = SCNVector3(-1, 0, 0) // R
        case 4: rotAxis = SCNVector3(0, 0, -1) // F
        case 5: rotAxis = SCNVector3(0, 0, 1)  // B
        default: break
        }

        let rotationAction = SCNAction.rotate(by: CGFloat(baseAngle), around: rotAxis, duration: duration)
        rotationAction.timingMode = .easeInEaseOut

        pivotNode.runAction(rotationAction) { [weak self] in
            Task { @MainActor in
                guard let self = self else { return }
                arrowNode.removeFromParentNode()

                // Reparent cubies back to cubeNode and round grid coordinates
                let offset = Float(n - 1) / 2.0
                for cubie in affectedCubies {
                    let worldTransform = cubie.worldTransform
                    cubie.removeFromParentNode()
                    self.cubeNode.addChildNode(cubie)
                    cubie.transform = self.cubeNode.convertTransform(worldTransform, from: nil)

                    // Update integer grid coordinates
                    let pos = cubie.position
                    let gx = Int(round((pos.x / self.cubieSpacing) + offset))
                    let gy = Int(round((pos.y / self.cubieSpacing) + offset))
                    let gz = Int(round((pos.z / self.cubieSpacing) + offset))
                    cubie.gridCoord = CubieCoord(x: max(0, min(n - 1, gx)),
                                                 y: max(0, min(n - 1, gy)),
                                                 z: max(0, min(n - 1, gz)))
                }

                pivotNode.removeFromParentNode()
                self.isAnimating = false
                completion?()
            }
        }
    }

    private func createCurvedArrowNode(axis: Int, power: Int) -> SCNNode {
        let torus = SCNTorus(ringRadius: 1.8, pipeRadius: 0.06)
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
