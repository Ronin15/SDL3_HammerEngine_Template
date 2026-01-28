/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#include "gpu/GPUPipeline.hpp"
#include "core/Logger.hpp"
#include <format>

namespace HammerEngine {

GPUPipeline::~GPUPipeline() {
    release();
}

GPUPipeline::GPUPipeline(GPUPipeline&& other) noexcept
    : m_pipeline(other.m_pipeline)
    , m_device(other.m_device)
{
    other.m_pipeline = nullptr;
    other.m_device = nullptr;
}

GPUPipeline& GPUPipeline::operator=(GPUPipeline&& other) noexcept {
    if (this != &other) {
        release();

        m_pipeline = other.m_pipeline;
        m_device = other.m_device;

        other.m_pipeline = nullptr;
        other.m_device = nullptr;
    }
    return *this;
}

bool GPUPipeline::create(SDL_GPUDevice* device, const PipelineConfig& config) {
    if (!device) {
        GAMEENGINE_ERROR("GPUPipeline::create: null device");
        return false;
    }

    if (!config.vertexShader || !config.fragmentShader) {
        GAMEENGINE_ERROR("GPUPipeline::create: missing shaders");
        return false;
    }

    release();
    m_device = device;

    // Build color target description
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = config.colorFormat;

    if (config.enableBlend) {
        colorTarget.blend_state.enable_blend = true;
        colorTarget.blend_state.src_color_blendfactor = config.srcColorFactor;
        colorTarget.blend_state.dst_color_blendfactor = config.dstColorFactor;
        colorTarget.blend_state.color_blend_op = config.colorBlendOp;
        colorTarget.blend_state.src_alpha_blendfactor = config.srcAlphaFactor;
        colorTarget.blend_state.dst_alpha_blendfactor = config.dstAlphaFactor;
        colorTarget.blend_state.alpha_blend_op = config.alphaBlendOp;
        colorTarget.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                                                    SDL_GPU_COLORCOMPONENT_G |
                                                    SDL_GPU_COLORCOMPONENT_B |
                                                    SDL_GPU_COLORCOMPONENT_A;
    } else {
        colorTarget.blend_state.enable_blend = false;
        colorTarget.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                                                    SDL_GPU_COLORCOMPONENT_G |
                                                    SDL_GPU_COLORCOMPONENT_B |
                                                    SDL_GPU_COLORCOMPONENT_A;
    }

    // Build rasterizer state
    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = config.fillMode;
    rasterizer.cull_mode = config.cullMode;
    rasterizer.front_face = config.frontFace;
    rasterizer.enable_depth_bias = false;
    rasterizer.enable_depth_clip = true;

    // Build depth/stencil state
    SDL_GPUDepthStencilState depthStencil{};
    depthStencil.enable_depth_test = config.enableDepthTest;
    depthStencil.enable_depth_write = config.enableDepthWrite;
    depthStencil.compare_op = config.depthCompareOp;
    depthStencil.enable_stencil_test = false;

    // Build pipeline create info
    SDL_GPUGraphicsPipelineCreateInfo createInfo{};
    createInfo.vertex_shader = config.vertexShader;
    createInfo.fragment_shader = config.fragmentShader;
    createInfo.vertex_input_state = config.vertexInput;
    createInfo.primitive_type = config.primitiveType;
    createInfo.rasterizer_state = rasterizer;
    createInfo.depth_stencil_state = depthStencil;
    createInfo.target_info.num_color_targets = 1;
    createInfo.target_info.color_target_descriptions = &colorTarget;
    createInfo.target_info.has_depth_stencil_target = false;

    m_pipeline = SDL_CreateGPUGraphicsPipeline(device, &createInfo);

    if (!m_pipeline) {
        GAMEENGINE_ERROR(std::format("Failed to create GPU pipeline: {}", SDL_GetError()));
        return false;
    }

    return true;
}

void GPUPipeline::release() {
    if (m_pipeline && m_device) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
}

PipelineConfig GPUPipeline::createSpriteConfig(SDL_GPUShader* vertShader,
                                                SDL_GPUShader* fragShader,
                                                SDL_GPUTextureFormat colorFormat,
                                                bool alpha) {
    PipelineConfig config{};
    config.vertexShader = vertShader;
    config.fragmentShader = fragShader;
    config.colorFormat = colorFormat;
    config.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // Vertex input: position (vec2), texcoord (vec2), color (rgba8)
    static SDL_GPUVertexBufferDescription bufferDesc{};
    bufferDesc.slot = 0;
    bufferDesc.pitch = sizeof(float) * 4 + sizeof(uint8_t) * 4;  // 20 bytes
    bufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    bufferDesc.instance_step_rate = 0;

    static SDL_GPUVertexAttribute attributes[3]{};
    // Position
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[0].offset = 0;
    // TexCoord
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = sizeof(float) * 2;
    // Color
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[2].offset = sizeof(float) * 4;

    config.vertexInput.num_vertex_buffers = 1;
    config.vertexInput.vertex_buffer_descriptions = &bufferDesc;
    config.vertexInput.num_vertex_attributes = 3;
    config.vertexInput.vertex_attributes = attributes;

    if (alpha) {
        config.enableBlend = true;
        // Premultiplied alpha blending (textures have RGB * A pre-applied)
        config.srcColorFactor = SDL_GPU_BLENDFACTOR_ONE;
        config.dstColorFactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        config.colorBlendOp = SDL_GPU_BLENDOP_ADD;
        config.srcAlphaFactor = SDL_GPU_BLENDFACTOR_ONE;
        config.dstAlphaFactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        config.alphaBlendOp = SDL_GPU_BLENDOP_ADD;
    } else {
        config.enableBlend = false;
    }

    return config;
}

PipelineConfig GPUPipeline::createParticleConfig(SDL_GPUShader* vertShader,
                                                  SDL_GPUShader* fragShader,
                                                  SDL_GPUTextureFormat colorFormat) {
    PipelineConfig config{};
    config.vertexShader = vertShader;
    config.fragmentShader = fragShader;
    config.colorFormat = colorFormat;
    config.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // Vertex input: position (vec2), color (rgba8)
    static SDL_GPUVertexBufferDescription bufferDesc{};
    bufferDesc.slot = 0;
    bufferDesc.pitch = sizeof(float) * 2 + sizeof(uint8_t) * 4;  // 12 bytes
    bufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    bufferDesc.instance_step_rate = 0;

    static SDL_GPUVertexAttribute attributes[2]{};
    // Position
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[0].offset = 0;
    // Color
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[1].offset = sizeof(float) * 2;

    config.vertexInput.num_vertex_buffers = 1;
    config.vertexInput.vertex_buffer_descriptions = &bufferDesc;
    config.vertexInput.num_vertex_attributes = 2;
    config.vertexInput.vertex_attributes = attributes;

    // Additive blending for particles
    config.enableBlend = true;
    config.srcColorFactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    config.dstColorFactor = SDL_GPU_BLENDFACTOR_ONE;

    return config;
}

PipelineConfig GPUPipeline::createPrimitiveConfig(SDL_GPUShader* vertShader,
                                                   SDL_GPUShader* fragShader,
                                                   SDL_GPUTextureFormat colorFormat) {
    PipelineConfig config{};
    config.vertexShader = vertShader;
    config.fragmentShader = fragShader;
    config.colorFormat = colorFormat;
    config.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // Vertex input: position (vec2), color (rgba8)
    static SDL_GPUVertexBufferDescription bufferDesc{};
    bufferDesc.slot = 0;
    bufferDesc.pitch = sizeof(float) * 2 + sizeof(uint8_t) * 4;  // 12 bytes
    bufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    bufferDesc.instance_step_rate = 0;

    static SDL_GPUVertexAttribute attributes[2]{};
    // Position
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[0].offset = 0;
    // Color
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[1].offset = sizeof(float) * 2;

    config.vertexInput.num_vertex_buffers = 1;
    config.vertexInput.vertex_buffer_descriptions = &bufferDesc;
    config.vertexInput.num_vertex_attributes = 2;
    config.vertexInput.vertex_attributes = attributes;

    // Alpha blending
    config.enableBlend = true;
    config.srcColorFactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    config.dstColorFactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

    return config;
}

PipelineConfig GPUPipeline::createCompositeConfig(SDL_GPUShader* vertShader,
                                                   SDL_GPUShader* fragShader,
                                                   SDL_GPUTextureFormat colorFormat) {
    PipelineConfig config{};
    config.vertexShader = vertShader;
    config.fragmentShader = fragShader;
    config.colorFormat = colorFormat;
    config.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // No vertex input - fullscreen triangle uses gl_VertexIndex
    config.vertexInput.num_vertex_buffers = 0;
    config.vertexInput.vertex_buffer_descriptions = nullptr;
    config.vertexInput.num_vertex_attributes = 0;
    config.vertexInput.vertex_attributes = nullptr;

    // No blending for composite
    config.enableBlend = false;

    return config;
}

} // namespace HammerEngine
