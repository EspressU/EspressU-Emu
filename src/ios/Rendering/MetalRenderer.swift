import Foundation
import MetalKit
import simd

class AdvancedMetalRenderer: NSObject, MTKViewDelegate {
    private let device: MTLDevice
    private let commandQueue: MTLCommandQueue
    private var library: MTLLibrary?

    private var pipelineState: MTLRenderPipelineState?
    private var upscalePipelineState: MTLRenderPipelineState?
    private var sharpenPipelineState: MTLRenderPipelineState?

    private var renderTexture: MTLTexture?
    private var upscaledTexture: MTLTexture?

    private var gameManager: GameManager?

    private let upscaleMode: UpscaleMode

    enum UpscaleMode {
        case bilinear
        case lanczos
        case none
    }

    init(device: MTLDevice, upscaleMode: UpscaleMode = .lanczos) {
        self.device = device
        self.commandQueue = device.makeCommandQueue()!
        self.upscaleMode = upscaleMode
        super.init()

        setupLibrary()
        setupPipelines()
    }

    private func setupLibrary() {
        library = device.makeDefaultLibrary()
    }

    private func setupPipelines() {
        guard let library = library else { return }

        let pipelineDescriptor = MTLRenderPipelineDescriptor()
        pipelineDescriptor.vertexFunction = library.makeFunction(name: "screenVertex")
        pipelineDescriptor.fragmentFunction = library.makeFunction(name: "screenFragment")
        pipelineDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm

        do {
            pipelineState = try device.makeRenderPipelineState(descriptor: pipelineDescriptor)
        } catch {
            print("Error creating render pipeline: \(error)")
        }

        let upscaleFunctionName: String
        switch upscaleMode {
        case .bilinear:
            upscaleFunctionName = "bilinearUpscaleFragment"
        case .lanczos:
            upscaleFunctionName = "lanczosUpscaleFragment"
        case .none:
            upscaleFunctionName = "screenFragment"
        }

        let upscaleDescriptor = MTLRenderPipelineDescriptor()
        upscaleDescriptor.vertexFunction = library.makeFunction(name: "screenVertex")
        upscaleDescriptor.fragmentFunction = library.makeFunction(name: upscaleFunctionName)
        upscaleDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm

        do {
            upscalePipelineState = try device.makeRenderPipelineState(descriptor: upscaleDescriptor)
        } catch {
            print("Error creating upscale pipeline: \(error)")
        }

        let sharpenDescriptor = MTLRenderPipelineDescriptor()
        sharpenDescriptor.vertexFunction = library.makeFunction(name: "screenVertex")
        sharpenDescriptor.fragmentFunction = library.makeFunction(name: "sharpeningFragment")
        sharpenDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm

        do {
            sharpenPipelineState = try device.makeRenderPipelineState(descriptor: sharpenDescriptor)
        } catch {
            print("Error creating sharpen pipeline: \(error)")
        }
    }

    func setGameManager(_ manager: GameManager) {
        self.gameManager = manager
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: Int(size.width),
            height: Int(size.height),
            mipmapped: false
        )
        descriptor.usage = [.renderTarget, .shaderRead]
        renderTexture = device.makeTexture(descriptor: descriptor)

        let upscaledDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: Int(size.width),
            height: Int(size.height),
            mipmapped: false
        )
        upscaledDescriptor.usage = [.renderTarget, .shaderRead]
        upscaledTexture = device.makeTexture(descriptor: upscaledDescriptor)
    }

    func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let descriptor = view.currentRenderPassDescriptor,
              let commandBuffer = commandQueue.makeCommandBuffer() else { return }

        descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1)
        descriptor.colorAttachments[0].loadAction = .clear
        descriptor.colorAttachments[0].storeAction = .store

        guard let renderEncoder = commandBuffer.makeRenderCommandEncoder(descriptor: descriptor),
              let pipelineState else { return }

        renderEncoder.setRenderPipelineState(pipelineState)

        if let frameTexture = gameManager?.getFrameTexture() {
            renderGameFrame(frameTexture, encoder: renderEncoder, view: view)
        } else {
            renderBlackScreen(encoder: renderEncoder)
        }

        renderEncoder.endEncoding()

        commandBuffer.present(drawable)

        commandBuffer.commit()
    }

    private func renderGameFrame(_ texture: MTLTexture, encoder: MTLRenderCommandEncoder, view: MTKView) {
        let viewSize = view.bounds.size
        let quad = createScreenQuad(viewSize: viewSize)

        guard let vertexBuffer = device.makeBuffer(bytes: quad, length: MemoryLayout<Float>.size * quad.count, options: []) else { return }

        encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        encoder.setFragmentTexture(texture, index: 0)

        let samplerDescriptor = MTLSamplerDescriptor()
        samplerDescriptor.minFilter = .linear
        samplerDescriptor.magFilter = .linear
        samplerDescriptor.sAddressMode = .clampToEdge
        samplerDescriptor.tAddressMode = .clampToEdge

        if let sampler = device.makeSamplerState(descriptor: samplerDescriptor) {
            encoder.setFragmentSamplerState(sampler, index: 0)
        }

        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
    }

    private func renderBlackScreen(encoder: MTLRenderCommandEncoder) {
        let quad: [Float] = [
            -1.0, 1.0, 0.0, 0.0,
            -1.0, -1.0, 0.0, 1.0,
            1.0, -1.0, 1.0, 1.0,
            -1.0, 1.0, 0.0, 0.0,
            1.0, -1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 0.0
        ]

        guard let vertexBuffer = device.makeBuffer(bytes: quad, length: MemoryLayout<Float>.size * quad.count, options: []) else { return }

        encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
    }

    private func createScreenQuad(viewSize: CGSize) -> [Float] {
        if FrameStretch.isEnabled {
            return [
                -1, 1, 0, 1,
                -1, -1, 0, 0,
                1, -1, 1, 0,
                -1, 1, 0, 1,
                1, -1, 1, 0,
                1, 1, 1, 1
            ]
        }

        let aspectRatio = Float(viewSize.width / viewSize.height)
        let targetAspect: Float = 1280.0 / 720.0

        let scale: Float
        if aspectRatio > targetAspect {
            scale = Float(viewSize.height) / 720.0
        } else {
            scale = Float(viewSize.width) / 1280.0
        }

        let width = 1280.0 * scale / Float(viewSize.width)
        let height = 720.0 * scale / Float(viewSize.height)

        return [
            -width, height, 0, 1,
            -width, -height, 0, 0,
            width, -height, 1, 0,
            -width, height, 0, 1,
            width, -height, 1, 0,
            width, height, 1, 1
        ]
    }
}
