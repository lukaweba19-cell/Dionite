// ============================================================================
// Dionite — Metal snapshot renderer.
//
// Draws the per-frame `DIInstance` list produced by the C++ runtime. Six
// procedurally generated primitives live in static vertex buffers; the frame
// buffer of instances is bucketed by (pass, mesh) so each combination becomes
// a single instanced draw call — at most 18 draws per frame.
//
// SceneUniforms must match `struct Scene` in Shaders.metal (176 bytes).
// ============================================================================
import Metal
import MetalKit
import simd

// MARK: - Scene uniforms

struct SceneUniforms {
    var viewProj = matrix_identity_float4x4
    var eye: SIMD4<Float> = .zero       // xyz = camera position, w = time
    var sun: SIMD4<Float> = .zero       // xyz = direction to sun, w = intensity
    var sunColor: SIMD4<Float> = .zero  // rgb = sun colour, w = ambient
    var fog: SIMD4<Float> = .zero       // rgb = fog colour, w = density
    var post: SIMD4<Float> = .zero      // x = exposure, y = shake
    var camRight: SIMD4<Float> = .zero
    var camUp: SIMD4<Float> = .zero
}

// MARK: - Procedural primitive meshes

private typealias MeshData = (vertices: [Float], indices: [UInt16])

enum MeshFactory {
    static let vertexStride = 8 // pos3 + normal3 + uv2

    private static func lathe(
        _ profile: [(radius: Float, y: Float, nRadius: Float, nY: Float)],
        segments: Int
    ) -> MeshData {
        var vertices: [Float] = []
        var indices: [UInt16] = []
        let rings = profile.count
        guard rings >= 2, segments >= 3 else { return (vertices, indices) }

        for (ring, point) in profile.enumerated() {
            let v = Float(ring) / Float(rings - 1)
            for segment in 0...segments {
                let t = Float(segment) / Float(segments)
                let theta = t * 2.0 * Float.pi
                let cosine = cos(theta)
                let sine = sin(theta)
                var nx = point.nRadius * cosine
                var ny = point.nY
                var nz = point.nRadius * sine
                let length = (nx * nx + ny * ny + nz * nz).squareRoot()
                if length > 1e-5 { nx /= length; ny /= length; nz /= length }
                vertices += [point.radius * cosine, point.y, point.radius * sine,
                             nx, ny, nz, t, v]
            }
        }

        let stride = segments + 1
        for ring in 0..<(rings - 1) {
            for segment in 0..<segments {
                let a = UInt16(ring * stride + segment)
                let b = UInt16(ring * stride + segment + 1)
                let c = UInt16((ring + 1) * stride + segment)
                let d = UInt16((ring + 1) * stride + segment + 1)
                indices += [a, c, d, a, d, b]
            }
        }
        return (vertices, indices)
    }

    private static func disc(centerY: Float, radius: Float, normalY: Float,
                             segments: Int) -> MeshData {
        var vertices: [Float] = [0, centerY, 0, 0, normalY, 0, 0.5, 0.5]
        var indices: [UInt16] = []
        for segment in 0...segments {
            let theta = Float(segment) / Float(segments) * 2.0 * Float.pi
            let cosine = cos(theta)
            let sine = sin(theta)
            vertices += [radius * cosine, centerY, radius * sine,
                         0, normalY, 0, 0.5 + 0.5 * cosine, 0.5 + 0.5 * sine]
        }
        for segment in 0..<segments {
            let first = UInt16(segment + 1)
            let second = UInt16(segment + 2)
            indices += normalY >= 0 ? [0, second, first] : [0, first, second]
        }
        return (vertices, indices)
    }

    private static func merge(_ parts: [MeshData]) -> MeshData {
        var vertices: [Float] = []
        var indices: [UInt16] = []
        for part in parts {
            let base = UInt16(vertices.count / vertexStride)
            vertices += part.vertices
            indices += part.indices.map { $0 + base }
        }
        return (vertices, indices)
    }

    static func quad() -> MeshData {
        let vertices: [Float] = [
            -0.5, 0, -0.5, 0, 1, 0, 0, 0,
             0.5, 0, -0.5, 0, 1, 0, 1, 0,
             0.5, 0,  0.5, 0, 1, 0, 1, 1,
            -0.5, 0,  0.5, 0, 1, 0, 0, 1
        ]
        let indices: [UInt16] = [0, 1, 2, 0, 2, 3]
        return (vertices, indices)
    }

    static func box() -> MeshData {
        var vertices: [Float] = []
        var indices: [UInt16] = []
        // (four corners counter-clockwise, normal)
        let faces: [(corners: [SIMD3<Float>], normal: SIMD3<Float>)] = [
            ([SIMD3(-0.5, 1, -0.5), SIMD3(0.5, 1, -0.5),
              SIMD3(0.5, 1, 0.5), SIMD3(-0.5, 1, 0.5)], SIMD3(0, 1, 0)),
            ([SIMD3(-0.5, 0, 0.5), SIMD3(0.5, 0, 0.5),
              SIMD3(0.5, 0, -0.5), SIMD3(-0.5, 0, -0.5)], SIMD3(0, -1, 0)),
            ([SIMD3(-0.5, 0, 0.5), SIMD3(0.5, 0, 0.5),
              SIMD3(0.5, 1, 0.5), SIMD3(-0.5, 1, 0.5)], SIMD3(0, 0, 1)),
            ([SIMD3(0.5, 0, -0.5), SIMD3(-0.5, 0, -0.5),
              SIMD3(-0.5, 1, -0.5), SIMD3(0.5, 1, -0.5)], SIMD3(0, 0, -1)),
            ([SIMD3(0.5, 0, 0.5), SIMD3(0.5, 0, -0.5),
              SIMD3(0.5, 1, -0.5), SIMD3(0.5, 1, 0.5)], SIMD3(1, 0, 0)),
            ([SIMD3(-0.5, 0, -0.5), SIMD3(-0.5, 0, 0.5),
              SIMD3(-0.5, 1, 0.5), SIMD3(-0.5, 1, -0.5)], SIMD3(-1, 0, 0))
        ]
        let uvs: [SIMD2<Float>] = [SIMD2(0, 0), SIMD2(1, 0), SIMD2(1, 1), SIMD2(0, 1)]
        for face in faces {
            let base = UInt16(vertices.count / vertexStride)
            for (corner, uv) in zip(face.corners, uvs) {
                vertices += [corner.x, corner.y, corner.z,
                             face.normal.x, face.normal.y, face.normal.z, uv.x, uv.y]
            }
            indices += [base, base + 1, base + 2, base, base + 2, base + 3]
        }
        return (vertices, indices)
    }

    static func sphere() -> MeshData {
        let radius: Float = 0.5
        let rings = 12
        let segments = 16
        var profile: [(radius: Float, y: Float, nRadius: Float, nY: Float)] = []
        for ring in 0...rings {
            let phi = Float(ring) / Float(rings) * Float.pi
            profile.append((sin(phi) * radius, cos(phi) * radius, sin(phi), cos(phi)))
        }
        return lathe(profile, segments: segments)
    }

    static func capsule() -> MeshData {
        let radius: Float = 0.35
        let caps = 5
        let segments = 16
        var profile: [(radius: Float, y: Float, nRadius: Float, nY: Float)] = []
        for index in 0...caps {
            let angle = (.pi / 2) * (1 - Float(index) / Float(caps))
            profile.append((radius * cos(angle), (1 - radius) + radius * sin(angle),
                            cos(angle), sin(angle)))
        }
        profile.append((radius, radius, 1, 0))
        for index in 1...caps {
            let angle = -(Float.pi / 2) * (Float(index) / Float(caps))
            profile.append((radius * cos(angle), radius + radius * sin(angle),
                            cos(angle), sin(angle)))
        }
        return lathe(profile, segments: segments)
    }

    static func cone() -> MeshData {
        let length: Float = (1.0 + 0.25).squareRoot()
        let nRadius = 1.0 / length
        let nY = 0.5 / length
        let side: MeshData = lathe([(0.5, 0, nRadius, nY), (0, 1, nRadius, nY)], segments: 16)
        let base = disc(centerY: 0, radius: 0.5, normalY: -1, segments: 16)
        return merge([side, base])
    }

    static func cylinder() -> MeshData {
        let side: MeshData = lathe([(0.5, 0, 1, 0), (0.5, 1, 1, 0)], segments: 16)
        let top = disc(centerY: 1, radius: 0.5, normalY: 1, segments: 16)
        let bottom = disc(centerY: 0, radius: 0.5, normalY: -1, segments: 16)
        return merge([side, top, bottom])
    }
}

// MARK: - Renderer

final class GameRenderer {
    static let meshCount = 6
    static let maxInstances = 24_000

    private struct Mesh {
        let vertexBuffer: MTLBuffer
        let indexBuffer: MTLBuffer
        let indexCount: Int
    }

    private let meshes: [Mesh]
    private let pipelines: [MTLRenderPipelineState]
    private let depthWriteState: MTLDepthStencilState
    private let depthReadState: MTLDepthStencilState
    private let instanceBuffers: [MTLBuffer]
    private var bufferCursor = 0

    init?(device: MTLDevice, pixelFormat: MTLPixelFormat, depthFormat: MTLPixelFormat) {
        guard let library = device.makeDefaultLibrary(),
              let vertexFunction = library.makeFunction(name: "vertex_instanced"),
              let fragmentFunction = library.makeFunction(name: "fragment_instanced") else {
            return nil
        }

        let descriptor = MTLVertexDescriptor()
        descriptor.attributes[0].format = .float3
        descriptor.attributes[0].offset = 0
        descriptor.attributes[0].bufferIndex = 0
        descriptor.attributes[1].format = .float3
        descriptor.attributes[1].offset = 12
        descriptor.attributes[1].bufferIndex = 0
        descriptor.attributes[2].format = .float2
        descriptor.attributes[2].offset = 24
        descriptor.attributes[2].bufferIndex = 0
        descriptor.layouts[0].stride = MeshFactory.vertexStride * 4
        descriptor.layouts[0].stepFunction = .perVertex

        let pipelineDescriptor = MTLRenderPipelineDescriptor()
        pipelineDescriptor.vertexFunction = vertexFunction
        pipelineDescriptor.fragmentFunction = fragmentFunction
        pipelineDescriptor.vertexDescriptor = descriptor
        pipelineDescriptor.colorAttachments[0].pixelFormat = pixelFormat
        pipelineDescriptor.depthAttachmentPixelFormat = depthFormat

        var built: [MTLRenderPipelineState] = []
        let blendModes: [(src: MTLBlendFactor, dst: MTLBlendFactor, write: Bool)] = [
            (.sourceAlpha, .oneMinusSourceAlpha, true),   // opaque
            (.sourceAlpha, .oneMinusSourceAlpha, false),  // translucent
            (.sourceAlpha, .one, false)                   // additive
        ]
        for mode in blendModes {
            let attachment = pipelineDescriptor.colorAttachments[0]!
            attachment.isBlendingEnabled = true
            attachment.rgbBlendSourceFactor = mode.src
            attachment.rgbBlendDestinationFactor = mode.dst
            attachment.alphaBlendSourceFactor = mode.src
            attachment.alphaBlendDestinationFactor = mode.dst
            do {
                built.append(try device.makeRenderPipelineState(descriptor: pipelineDescriptor))
            } catch {
                return nil
            }
        }
        pipelines = built

        let depthDescriptor = MTLDepthStencilDescriptor()
        depthDescriptor.depthCompareFunction = .lessEqual
        depthDescriptor.isDepthWriteEnabled = true
        guard let writeState = device.makeDepthStencilState(descriptor: depthDescriptor) else {
            return nil
        }
        depthDescriptor.isDepthWriteEnabled = false
        guard let readState = device.makeDepthStencilState(descriptor: depthDescriptor) else {
            return nil
        }
        depthWriteState = writeState
        depthReadState = readState

        let factories: [() -> MeshData] = [
            MeshFactory.quad, MeshFactory.box, MeshFactory.capsule,
            MeshFactory.sphere, MeshFactory.cone, MeshFactory.cylinder
        ]
        var builtMeshes: [Mesh] = []
        for factory in factories {
            let data = factory()
            guard let vertexBuffer = device.makeBuffer(
                    bytes: data.vertices,
                    length: data.vertices.count * 4,
                    options: .storageModeShared),
                  let indexBuffer = device.makeBuffer(
                    bytes: data.indices,
                    length: data.indices.count * 2,
                    options: .storageModeShared) else {
                return nil
            }
            builtMeshes.append(Mesh(vertexBuffer: vertexBuffer,
                                    indexBuffer: indexBuffer,
                                    indexCount: data.indices.count))
        }
        meshes = builtMeshes

        let capacity = GameRenderer.maxInstances * MemoryLayout<DIInstance>.stride + 8_192
        var buffers: [MTLBuffer] = []
        for _ in 0..<3 {
            guard let buffer = device.makeBuffer(length: capacity,
                                                 options: .storageModeShared) else {
                return nil
            }
            buffers.append(buffer)
        }
        instanceBuffers = buffers
    }

    /// Buckets the instance list by pass and mesh, then issues the draws.
    func encode(_ encoder: MTLRenderCommandEncoder,
                scene: inout SceneUniforms,
                instances: UnsafePointer<DIInstance>?,
                opaque: Int32, translucent: Int32, additive: Int32) {
        let total = Int(opaque + translucent + additive)
        guard total > 0, let instances = instances, total <= GameRenderer.maxInstances else {
            return
        }

        let ranges: [(start: Int, end: Int)] = [
            (0, Int(opaque)),
            (Int(opaque), Int(opaque + translucent)),
            (Int(opaque + translucent), total)
        ]

        var counts = [[Int]](repeating: [Int](repeating: 0, count: GameRenderer.meshCount),
                             count: 3)
        for pass in 0..<3 {
            for index in ranges[pass].start..<ranges[pass].end {
                let mesh = Int(instances[index].mesh)
                if mesh >= 0 && mesh < GameRenderer.meshCount { counts[pass][mesh] += 1 }
            }
        }

        // Region byte offsets, aligned so each draw starts on a clean boundary.
        var offsets = [[Int]](repeating: [Int](repeating: 0, count: GameRenderer.meshCount),
                              count: 3)
        var cursor = 0
        let alignment = 256
        for pass in 0..<3 {
            for mesh in 0..<GameRenderer.meshCount {
                let aligned = (cursor + alignment - 1) & ~(alignment - 1)
                offsets[pass][mesh] = aligned
                cursor = aligned + counts[pass][mesh] * MemoryLayout<DIInstance>.stride
            }
        }
        guard cursor <= instanceBuffers[0].length else { return }

        let buffer = instanceBuffers[bufferCursor]
        bufferCursor = (bufferCursor + 1) % instanceBuffers.count
        let destination = buffer.contents().assumingMemoryBound(to: DIInstance.self)

        var writeCursor = offsets
        for pass in 0..<3 {
            for index in ranges[pass].start..<ranges[pass].end {
                let mesh = Int(instances[index].mesh)
                guard mesh >= 0, mesh < GameRenderer.meshCount else { continue }
                destination[writeCursor[pass][mesh] / MemoryLayout<DIInstance>.stride] =
                    instances[index]
                writeCursor[pass][mesh] += MemoryLayout<DIInstance>.stride
            }
        }

        withUnsafeBytes(of: scene) { raw in
            if let base = raw.baseAddress {
                encoder.setVertexBytes(base, length: raw.count, index: 2)
                encoder.setFragmentBytes(base, length: raw.count, index: 2)
            }
        }

        for pass in 0..<3 {
            encoder.setDepthStencilState(pass == 0 ? depthWriteState : depthReadState)
            encoder.setRenderPipelineState(pipelines[pass])
            encoder.setCullMode(.none)
            for mesh in 0..<GameRenderer.meshCount {
                let count = counts[pass][mesh]
                if count == 0 { continue }
                let target = meshes[mesh]
                encoder.setVertexBuffer(target.vertexBuffer, offset: 0, index: 0)
                encoder.setVertexBuffer(buffer, offset: offsets[pass][mesh], index: 1)
                encoder.drawIndexedPrimitives(type: .triangle,
                                              indexCount: target.indexCount,
                                              indexType: .uint16,
                                              indexBuffer: target.indexBuffer,
                                              indexBufferOffset: 0,
                                              instanceCount: count)
            }
        }
    }
}
