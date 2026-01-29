/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#include "gpu/GPURenderer.hpp"
#include "gpu/GPUShaderManager.hpp"
#include "managers/TextureManager.hpp"
#include "core/Logger.hpp"
#include <cstring>
#include <format>

namespace HammerEngine {

GPURenderer& GPURenderer::Instance() {
    static GPURenderer instance;
    return instance;
}

bool GPURenderer::init() {
    if (m_initialized) {
        GAMEENGINE_WARN("GPURenderer already initialized");
        return true;
    }

    auto& gpuDevice = GPUDevice::Instance();
    if (!gpuDevice.isInitialized()) {
        GAMEENGINE_ERROR("GPURenderer::init: GPUDevice not initialized");
        return false;
    }

    m_device = gpuDevice.get();
    m_window = gpuDevice.getWindow();

    // Get window size for viewport (logical size - matches swapchain)
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    m_viewportWidth = static_cast<uint32_t>(w);
    m_viewportHeight = static_cast<uint32_t>(h);

    // Initialize shader manager
    if (!GPUShaderManager::Instance().init(m_device)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init shader manager");
        return false;
    }

    // Create samplers
    m_nearestSampler = GPUSampler::createNearest(m_device);
    m_linearSampler = GPUSampler::createLinear(m_device);

    if (!m_nearestSampler.isValid() || !m_linearSampler.isValid()) {
        GAMEENGINE_ERROR("GPURenderer: failed to create samplers");
        cleanupPartialInit();
        return false;
    }

    // Create scene texture
    if (!createSceneTexture()) {
        GAMEENGINE_ERROR("GPURenderer: failed to create scene texture");
        cleanupPartialInit();
        return false;
    }

    // Load shaders and create pipelines
    if (!loadShaders()) {
        GAMEENGINE_ERROR("GPURenderer: failed to load shaders");
        cleanupPartialInit();
        return false;
    }

    if (!createPipelines()) {
        GAMEENGINE_ERROR("GPURenderer: failed to create pipelines");
        cleanupPartialInit();
        return false;
    }

    // Initialize vertex pools
    // 4K (3840x2160) at 32px tiles = 120x68 = 8160 tiles visible
    // 2 layers (biome + obstacle) = ~16k sprites, 6 verts each = ~100k vertices
    // Use 150k for zoom headroom
    if (!m_spriteVertexPool.init(m_device, sizeof(SpriteVertex), 150000)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init sprite vertex pool");
        cleanupPartialInit();
        return false;
    }

    // Entity vertex pool for player/NPCs with separate textures
    if (!m_entityVertexPool.init(m_device, sizeof(SpriteVertex), 1000)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init entity vertex pool");
        cleanupPartialInit();
        return false;
    }

    if (!m_particleVertexPool.init(m_device, sizeof(ColorVertex), 100000)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init particle vertex pool");
        cleanupPartialInit();
        return false;
    }

    if (!m_primitiveVertexPool.init(m_device, sizeof(ColorVertex), 10000)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init primitive vertex pool");
        cleanupPartialInit();
        return false;
    }

    // Initialize UI vertex pool (for text/icons on swapchain)
    if (!m_uiVertexPool.init(m_device, sizeof(SpriteVertex), 4000)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init UI vertex pool");
        cleanupPartialInit();
        return false;
    }

    // Initialize sprite batches
    if (!m_spriteBatch.init(m_device)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init sprite batch");
        cleanupPartialInit();
        return false;
    }

    if (!m_entityBatch.init(m_device)) {
        GAMEENGINE_ERROR("GPURenderer: failed to init entity batch");
        cleanupPartialInit();
        return false;
    }

    m_initialized = true;
    GAMEENGINE_INFO(std::format("GPURenderer initialized: {}x{}", m_viewportWidth, m_viewportHeight));
    return true;
}

void GPURenderer::shutdown() {
    if (!m_initialized) {
        return;
    }

    // Release sprite batches
    m_spriteBatch.shutdown();
    m_entityBatch.shutdown();

    // Release vertex pools
    m_spriteVertexPool.shutdown();
    m_entityVertexPool.shutdown();
    m_particleVertexPool.shutdown();
    m_primitiveVertexPool.shutdown();
    m_uiVertexPool.shutdown();

    // Release pipelines
    m_spriteOpaquePipeline.release();
    m_spriteAlphaPipeline.release();
    m_particlePipeline.release();
    m_primitivePipeline.release();
    m_compositePipeline.release();
    m_uiSpritePipeline.release();
    m_uiPrimitivePipeline.release();

    // Release textures and samplers
    m_sceneTexture.reset();
    m_nearestSampler = GPUSampler();
    m_linearSampler = GPUSampler();

    // Shutdown shader manager
    GPUShaderManager::Instance().shutdown();

    m_device = nullptr;
    m_window = nullptr;
    m_initialized = false;

    GAMEENGINE_INFO("GPURenderer shutdown complete");
}

void GPURenderer::cleanupPartialInit() {
    // Clean up any resources that may have been created during failed init
    // Unlike shutdown(), this doesn't check m_initialized since init failed

    m_spriteBatch.shutdown();
    m_entityBatch.shutdown();

    m_spriteVertexPool.shutdown();
    m_entityVertexPool.shutdown();
    m_particleVertexPool.shutdown();
    m_primitiveVertexPool.shutdown();
    m_uiVertexPool.shutdown();

    m_spriteOpaquePipeline.release();
    m_spriteAlphaPipeline.release();
    m_particlePipeline.release();
    m_primitivePipeline.release();
    m_compositePipeline.release();
    m_uiSpritePipeline.release();
    m_uiPrimitivePipeline.release();

    m_sceneTexture.reset();
    m_nearestSampler = GPUSampler();
    m_linearSampler = GPUSampler();

    GPUShaderManager::Instance().shutdown();

    m_device = nullptr;
    m_window = nullptr;
}

void GPURenderer::beginFrame() {
    if (!m_initialized) {
        return;
    }

    // Acquire command buffer
    m_commandBuffer = SDL_AcquireGPUCommandBuffer(m_device);
    if (!m_commandBuffer) {
        GAMEENGINE_ERROR(std::format("Failed to acquire GPU command buffer: {}", SDL_GetError()));
        return;
    }

    // Acquire swapchain early to get authoritative dimensions
    // Swapchain size is set by SDL3_GPU in response to resize events
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            m_commandBuffer, m_window, &m_swapchainTexture,
            &m_swapchainWidth, &m_swapchainHeight)) {
        GAMEENGINE_ERROR(std::format("Failed to acquire swapchain texture: {}", SDL_GetError()));
        m_swapchainTexture = nullptr;
        return;
    }

    if (!m_swapchainTexture) {
        // Window minimized or not visible
        return;
    }

    // Sync viewport to swapchain size (authoritative source from resize events)
    if (m_swapchainWidth != m_viewportWidth || m_swapchainHeight != m_viewportHeight) {
        GAMEENGINE_INFO(std::format("Swapchain size changed: {}x{} -> {}x{}",
                                    m_viewportWidth, m_viewportHeight,
                                    m_swapchainWidth, m_swapchainHeight));
        updateViewport(m_swapchainWidth, m_swapchainHeight);
    }

    // Begin vertex pool frames
    m_spriteVertexPool.beginFrame();
    m_entityVertexPool.beginFrame();
    m_particleVertexPool.beginFrame();
    m_primitiveVertexPool.beginFrame();
    m_uiVertexPool.beginFrame();

    // Begin copy pass for uploads
    m_copyPass = SDL_BeginGPUCopyPass(m_commandBuffer);
}

SDL_GPURenderPass* GPURenderer::beginScenePass() {
    if (!m_commandBuffer) {
        return nullptr;
    }

    // End copy pass
    if (m_copyPass) {
        // Process pending texture uploads
        TextureManager::Instance().processPendingUploads(m_copyPass);

        // End vertex pool frames (unmaps buffers for upload)
        // Use SpriteBatch count if available, otherwise use pending count from direct writes
        size_t spriteVertexCount = m_spriteBatch.getVertexCount();
        if (spriteVertexCount == 0) {
            spriteVertexCount = m_spriteVertexPool.getPendingVertexCount();
        }
        m_spriteVertexPool.endFrame(spriteVertexCount);

        // End entity vertex pool (uses entity batch count or pending count)
        size_t entityVertexCount = m_entityBatch.getVertexCount();
        if (entityVertexCount == 0) {
            entityVertexCount = m_entityVertexPool.getPendingVertexCount();
        }
        m_entityVertexPool.endFrame(entityVertexCount);

        // End particle vertex pool (uses pending count from ParticleManager writes)
        size_t particleVertexCount = m_particleVertexPool.getPendingVertexCount();
        m_particleVertexPool.endFrame(particleVertexCount);

        // End primitive vertex pool (uses pending count from UIManager writes)
        size_t primitiveVertexCount = m_primitiveVertexPool.getPendingVertexCount();
        m_primitiveVertexPool.endFrame(primitiveVertexCount);

        // End UI vertex pool (uses pending count from direct writes)
        size_t uiVertexCount = m_uiVertexPool.getPendingVertexCount();
        m_uiVertexPool.endFrame(uiVertexCount);

        // Upload vertex data
        m_spriteVertexPool.upload(m_copyPass);
        m_entityVertexPool.upload(m_copyPass);
        m_particleVertexPool.upload(m_copyPass);
        m_primitiveVertexPool.upload(m_copyPass);
        m_uiVertexPool.upload(m_copyPass);

        SDL_EndGPUCopyPass(m_copyPass);
        m_copyPass = nullptr;
    }

    // Begin scene render pass
    SDL_GPUColorTargetInfo colorTarget = m_sceneTexture->asColorTarget(
        SDL_GPU_LOADOP_CLEAR,
        {0.05f, 0.05f, 0.1f, 1.0f}  // Dark blue-ish clear color
    );

    m_currentPass = SDL_BeginGPURenderPass(m_commandBuffer, &colorTarget, 1, nullptr);

    // Set viewport to match scene texture dimensions
    // Scene texture is 3x viewport for zoom headroom
    uint32_t sceneW = m_sceneTexture->getWidth();
    uint32_t sceneH = m_sceneTexture->getHeight();

    // Debug: Log scene texture dimensions (only once or on change)
    if (sceneW != m_lastLoggedSceneW || sceneH != m_lastLoggedSceneH) {
        GAMEENGINE_DEBUG(std::format("Scene pass: texture={}x{}, viewport={}x{}",
                                     sceneW, sceneH, m_viewportWidth, m_viewportHeight));
        m_lastLoggedSceneW = sceneW;
        m_lastLoggedSceneH = sceneH;
    }

    SDL_GPUViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = static_cast<float>(sceneW);
    viewport.h = static_cast<float>(sceneH);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(m_currentPass, &viewport);

    return m_currentPass;
}

SDL_GPURenderPass* GPURenderer::beginSwapchainPass() {
    if (!m_commandBuffer) {
        return nullptr;
    }

    // End scene pass
    if (m_currentPass) {
        SDL_EndGPURenderPass(m_currentPass);
        m_currentPass = nullptr;
    }

    // Swapchain already acquired in beginFrame()
    if (!m_swapchainTexture) {
        // Window minimized or not visible
        return nullptr;
    }

    // Begin swapchain render pass
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = m_swapchainTexture;
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;
    colorTarget.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

    m_currentPass = SDL_BeginGPURenderPass(m_commandBuffer, &colorTarget, 1, nullptr);

    // Set viewport to swapchain size (synced in beginFrame)
    SDL_GPUViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = static_cast<float>(m_swapchainWidth);
    viewport.h = static_cast<float>(m_swapchainHeight);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(m_currentPass, &viewport);

    return m_currentPass;
}

void GPURenderer::endFrame() {
    if (!m_commandBuffer) {
        return;
    }

    // End active render pass
    if (m_currentPass) {
        SDL_EndGPURenderPass(m_currentPass);
        m_currentPass = nullptr;
    }

    // End copy pass if still active
    if (m_copyPass) {
        SDL_EndGPUCopyPass(m_copyPass);
        m_copyPass = nullptr;
    }

    // Submit command buffer
    SDL_SubmitGPUCommandBuffer(m_commandBuffer);
    m_commandBuffer = nullptr;
    m_swapchainTexture = nullptr;
}

SDL_GPUGraphicsPipeline* GPURenderer::getSpriteOpaquePipeline() const {
    return m_spriteOpaquePipeline.get();
}

SDL_GPUGraphicsPipeline* GPURenderer::getSpriteAlphaPipeline() const {
    return m_spriteAlphaPipeline.get();
}

SDL_GPUGraphicsPipeline* GPURenderer::getParticlePipeline() const {
    return m_particlePipeline.get();
}

SDL_GPUGraphicsPipeline* GPURenderer::getPrimitivePipeline() const {
    return m_primitivePipeline.get();
}

SDL_GPUGraphicsPipeline* GPURenderer::getCompositePipeline() const {
    return m_compositePipeline.get();
}

SDL_GPUGraphicsPipeline* GPURenderer::getUISpritePipeline() const {
    return m_uiSpritePipeline.get();
}

SDL_GPUGraphicsPipeline* GPURenderer::getUIPrimitivePipeline() const {
    return m_uiPrimitivePipeline.get();
}

void GPURenderer::updateViewport(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        GAMEENGINE_WARN(std::format("GPURenderer::updateViewport ignored invalid size: {}x{}", width, height));
        return;
    }

    if (width == m_viewportWidth && height == m_viewportHeight) {
        return;
    }

    uint32_t oldWidth = m_viewportWidth;
    uint32_t oldHeight = m_viewportHeight;

    m_viewportWidth = width;
    m_viewportHeight = height;

    // Recreate scene texture with new size
    if (!createSceneTexture()) {
        GAMEENGINE_ERROR(std::format("GPURenderer::updateViewport failed to recreate scene texture for {}x{}",
                                      width, height));
        // Revert to old dimensions to maintain consistency
        m_viewportWidth = oldWidth;
        m_viewportHeight = oldHeight;
        return;
    }

    GAMEENGINE_INFO(std::format("GPURenderer viewport updated: {}x{} -> {}x{} (scene: {}x{})",
                                 oldWidth, oldHeight, width, height,
                                 width, height));
}

void GPURenderer::pushViewProjection(SDL_GPURenderPass* pass, const float* viewProjection) {
    if (!pass || !viewProjection) {
        return;
    }

    SDL_PushGPUVertexUniformData(m_commandBuffer, 0, viewProjection, sizeof(float) * 16);
}

void GPURenderer::pushCompositeUniforms(SDL_GPURenderPass* pass,
                                         float subPixelX, float subPixelY, float zoom) {
    if (!pass) {
        return;
    }

    CompositeUBO ubo{};
    ubo.subPixelOffsetX = subPixelX;
    ubo.subPixelOffsetY = subPixelY;
    ubo.zoom = zoom;
    ubo._pad0 = 0.0f;
    // Day/night ambient lighting
    ubo.ambientR = m_dayNightR;
    ubo.ambientG = m_dayNightG;
    ubo.ambientB = m_dayNightB;
    ubo.ambientAlpha = m_dayNightAlpha;

    // Push to fragment shader (set 1, binding 0 in fragment)
    SDL_PushGPUFragmentUniformData(m_commandBuffer, 0, &ubo, sizeof(CompositeUBO));
}

void GPURenderer::setDayNightParams(float r, float g, float b, float alpha) {
    m_dayNightR = r;
    m_dayNightG = g;
    m_dayNightB = b;
    m_dayNightAlpha = alpha;
}

void GPURenderer::setCompositeParams(float zoom, float subPixelX, float subPixelY) {
    m_compositeZoom = zoom;
    m_compositeSubPixelX = subPixelX;
    m_compositeSubPixelY = subPixelY;
}

void GPURenderer::renderComposite(SDL_GPURenderPass* pass) {
    if (!pass || !m_sceneTexture || !m_sceneTexture->isValid()) {
        return;
    }

    // Bind composite pipeline
    SDL_BindGPUGraphicsPipeline(pass, m_compositePipeline.get());

    // Bind scene texture with linear sampler for smooth compositing
    SDL_GPUTextureSamplerBinding texSampler{};
    texSampler.texture = m_sceneTexture->get();
    texSampler.sampler = m_linearSampler.get();
    SDL_BindGPUFragmentSamplers(pass, 0, &texSampler, 1);

    // Push composite uniforms (using stored params)
    pushCompositeUniforms(pass, m_compositeSubPixelX, m_compositeSubPixelY, m_compositeZoom);

    // Draw fullscreen triangle (3 vertices, no vertex buffer needed)
    // The composite vertex shader uses gl_VertexIndex to generate positions
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void GPURenderer::createOrthoMatrix(float left, float right, float bottom, float top,
                                     float* out) {
    // Standard orthographic projection (OpenGL-style, but works with Vulkan clip space)
    std::memset(out, 0, sizeof(float) * 16);

    out[0] = 2.0f / (right - left);
    out[5] = 2.0f / (top - bottom);
    out[10] = -1.0f;  // Near = 0, Far = 1 for Vulkan
    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[15] = 1.0f;
}

bool GPURenderer::loadShaders() {
    auto& shaderMgr = GPUShaderManager::Instance();

    ShaderInfo spriteVertInfo{};
    spriteVertInfo.numUniformBuffers = 1;  // View-projection matrix

    ShaderInfo spriteFragInfo{};
    spriteFragInfo.numSamplers = 1;  // Texture sampler

    ShaderInfo particleVertInfo{};
    particleVertInfo.numUniformBuffers = 1;

    ShaderInfo particleFragInfo{};
    // No samplers or uniforms

    ShaderInfo primitiveVertInfo{};
    primitiveVertInfo.numUniformBuffers = 1;

    ShaderInfo primitiveFragInfo{};
    // No samplers or uniforms

    ShaderInfo compositeVertInfo{};
    // No vertex uniforms - composite uses fragment uniforms only

    ShaderInfo compositeFragInfo{};
    compositeFragInfo.numSamplers = 1;
    compositeFragInfo.numUniformBuffers = 1;  // CompositeUBO

    // Load all shaders
    if (!shaderMgr.loadShader("res/shaders/sprite.vert", SDL_GPU_SHADERSTAGE_VERTEX, spriteVertInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/sprite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, spriteFragInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/particle.vert", SDL_GPU_SHADERSTAGE_VERTEX, particleVertInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/particle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, particleFragInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/primitive.vert", SDL_GPU_SHADERSTAGE_VERTEX, primitiveVertInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/primitive.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, primitiveFragInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/composite.vert", SDL_GPU_SHADERSTAGE_VERTEX, compositeVertInfo)) {
        return false;
    }
    if (!shaderMgr.loadShader("res/shaders/composite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, compositeFragInfo)) {
        return false;
    }

    return true;
}

bool GPURenderer::createPipelines() {
    auto& shaderMgr = GPUShaderManager::Instance();

    // Scene-rendering pipelines use the scene texture format (RGBA8)
    // Composite pipeline uses the swapchain format (may be BGRA8 on some platforms)
    SDL_GPUTextureFormat sceneFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureFormat swapchainFormat = GPUDevice::Instance().getSwapchainFormat();

    // Sprite opaque pipeline (renders to scene texture)
    {
        auto config = GPUPipeline::createSpriteConfig(
            shaderMgr.getShader("res/shaders/sprite.vert"),
            shaderMgr.getShader("res/shaders/sprite.frag"),
            sceneFormat,
            false  // opaque
        );
        if (!m_spriteOpaquePipeline.create(m_device, config)) {
            return false;
        }
    }

    // Sprite alpha pipeline (renders to scene texture)
    {
        auto config = GPUPipeline::createSpriteConfig(
            shaderMgr.getShader("res/shaders/sprite.vert"),
            shaderMgr.getShader("res/shaders/sprite.frag"),
            sceneFormat,
            true  // alpha
        );
        if (!m_spriteAlphaPipeline.create(m_device, config)) {
            return false;
        }
    }

    // Particle pipeline (renders to scene texture)
    {
        auto config = GPUPipeline::createParticleConfig(
            shaderMgr.getShader("res/shaders/particle.vert"),
            shaderMgr.getShader("res/shaders/particle.frag"),
            sceneFormat
        );
        if (!m_particlePipeline.create(m_device, config)) {
            return false;
        }
    }

    // Primitive pipeline (renders to scene texture)
    {
        auto config = GPUPipeline::createPrimitiveConfig(
            shaderMgr.getShader("res/shaders/primitive.vert"),
            shaderMgr.getShader("res/shaders/primitive.frag"),
            sceneFormat
        );
        if (!m_primitivePipeline.create(m_device, config)) {
            return false;
        }
    }

    // Composite pipeline (renders to swapchain)
    {
        auto config = GPUPipeline::createCompositeConfig(
            shaderMgr.getShader("res/shaders/composite.vert"),
            shaderMgr.getShader("res/shaders/composite.frag"),
            swapchainFormat
        );
        if (!m_compositePipeline.create(m_device, config)) {
            return false;
        }
    }

    // UI sprite pipeline (renders to swapchain for text/icons)
    {
        auto config = GPUPipeline::createSpriteConfig(
            shaderMgr.getShader("res/shaders/sprite.vert"),
            shaderMgr.getShader("res/shaders/sprite.frag"),
            swapchainFormat,
            true  // alpha blending for text
        );
        if (!m_uiSpritePipeline.create(m_device, config)) {
            return false;
        }
    }

    // UI primitive pipeline (renders to swapchain for UI backgrounds)
    {
        auto config = GPUPipeline::createPrimitiveConfig(
            shaderMgr.getShader("res/shaders/primitive.vert"),
            shaderMgr.getShader("res/shaders/primitive.frag"),
            swapchainFormat
        );
        if (!m_uiPrimitivePipeline.create(m_device, config)) {
            return false;
        }
    }

    return true;
}

bool GPURenderer::createSceneTexture() {
    // Create scene texture at viewport size (matching SDL_Renderer approach)
    // Zoom is handled in the composite shader, not by rendering at larger scale
    uint32_t sceneWidth = m_viewportWidth;
    uint32_t sceneHeight = m_viewportHeight;

    m_sceneTexture = std::make_unique<GPUTexture>(
        m_device,
        sceneWidth, sceneHeight,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER
    );

    if (!m_sceneTexture->isValid()) {
        GAMEENGINE_ERROR(std::format("Failed to create scene texture {}x{}", sceneWidth, sceneHeight));
        return false;
    }

    GAMEENGINE_DEBUG(std::format("Scene texture created: {}x{}", sceneWidth, sceneHeight));
    return true;
}

} // namespace HammerEngine
