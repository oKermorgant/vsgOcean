/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/OceanSurface.h>

#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/DrawIndexed.h>

#include "ocean_vert.h"
#include "ocean_frag.h"

namespace vsgOcean
{
    OceanSurface::OceanSurface(uint32_t rings, uint32_t segments, uint32_t fftCells) :
        wavefield_(Wavefield::create(fftCells))
    {
        // a camera-following disc of concentric rings with an exponentially growing
        // radius: sub-metre cells around the camera resolve the shortest component
        // waves, horizon-scale cells far away are sub-pixel. The vertex shader
        // re-centres the disc on each view's camera; the wave field is sampled at
        // world positions, so the waves stay fixed in the world (an endless ocean)
        constexpr double innerRadius{0.25};     // metres; the disc centre is a fan
        constexpr double outerRadius{16000.0}; // metres; beyond any far plane
        const auto growth = std::pow(outerRadius / innerRadius, 1.0 / static_cast<double>(rings - 1));

        // vertices: the disc centre, then one ring per radius step
        auto vertices = vsg::vec3Array::create(1 + static_cast<size_t>(rings) * segments);
        (*vertices)[0] = vsg::vec3(0.0f, 0.0f, 0.0f);
        for (uint32_t r = 0; r < rings; ++r)
        {
            const auto radius = static_cast<float>(innerRadius * std::pow(growth, static_cast<double>(r)));
            for (uint32_t s = 0; s < segments; ++s)
            {
                const auto theta = 2.0 * vsg::PI * static_cast<double>(s) / static_cast<double>(segments);
                (*vertices)[1 + static_cast<size_t>(r) * segments + s] = vsg::vec3(radius * static_cast<float>(std::cos(theta)),
                                                                                   radius * static_cast<float>(std::sin(theta)), 0.0f);
            }
        }

        // indices: a fan at the centre, then quads between the rings (wrapping around)
        auto indices = vsg::uintArray::create(static_cast<size_t>(segments) * 3 + static_cast<size_t>(rings - 1) * segments * 6);
        size_t index = 0;
        for (uint32_t s = 0; s < segments; ++s)
        {
            const auto next = (s + 1) % segments;
            (*indices)[index++] = 0;
            (*indices)[index++] = 1 + s;
            (*indices)[index++] = 1 + next;
        }
        for (uint32_t r = 0; r + 1 < rings; ++r)
        {
            const auto inner = 1 + r * segments;
            const auto outer = 1 + (r + 1) * segments;
            for (uint32_t s = 0; s < segments; ++s)
            {
                const auto next = (s + 1) % segments;
                (*indices)[index++] = inner + s;
                (*indices)[index++] = outer + s;
                (*indices)[index++] = inner + next;
                (*indices)[index++] = inner + next;
                (*indices)[index++] = outer + s;
                (*indices)[index++] = outer + next;
            }
        }

        // uniform block: [0] sun direction, [1] fog color + density, [2] water color,
        // [3] params (t, tau, number, water depth; the component count is 0 on the
        // FFT sea - it replaces the component waves), [4] per-channel attenuation + sea
        // level, [5..44] component wave arrays, [45] FFT sea (enabled, tile size, detail tile size, foam)
        uniform_ = vsg::vec4Array::create(6 + 5 * Wavefield::maxWaves);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        vsg::DescriptorSetLayoutBindings oceanBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},  // FFT displacement
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},  // FFT slopes (foam)
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},  // detail cascade displacement
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}; // detail cascade slopes
        auto oceanLayout = vsg::DescriptorSetLayout::create(oceanBindings);

        // the projection and modelview matrices are pushed by the vsg record traversal
        vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(vsg::mat4)}};

        vsg::VertexInputState::Bindings vertexBindings{{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}};
        vsg::VertexInputState::Attributes vertexAttributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};

        // visible from above and from below
        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE;

        // a transparent pass: blended over the scene beneath the surface, drawn after the
        // opaque scene so nothing renders after it. It writes depth (reversed-Z, like the
        // rest of the scene): the composite pass then attenuates and fogs it by its true
        // view distance, like every other scene object - also seen from below. What the
        // water lets through to the surface's own alpha (the scene beneath it) is the
        // assumed water column of ocean.frag, a deep-column default
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_TRUE;
        depthStencil->depthWriteEnable = VK_TRUE;
        depthStencil->depthCompareOp = VK_COMPARE_OP_GREATER; // vsg's reversed depth convention

        auto blend = vsg::ColorBlendState::create();
        blend->configureAttachments(true);

        vsg::GraphicsPipelineStates pipelineStates{
            vsg::VertexInputState::create(vertexBindings, vertexAttributes),
            vsg::InputAssemblyState::create(),
            rasterization,
            vsg::MultisampleState::create(),
            blend,
            depthStencil};

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{oceanLayout}, pushConstantRanges);

        auto shaders = vsg::ShaderStages{
            vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", ocean_vert),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", ocean_frag)};

        auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

        auto oceanDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{uniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        // the FFT sea maps tile over the world (periodic, bilinear)
        auto sampler = vsg::Sampler::create();
        sampler->addressModeU = sampler->addressModeV = sampler->addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler->magFilter = sampler->minFilter = VK_FILTER_LINEAR;
        sampler->maxLod = 0.0f;
        sampler->anisotropyEnable = VK_FALSE;
        auto displacementDescriptor = vsg::DescriptorImage::create(sampler, wavefield_->fft()->displacement(), 1, 0);
        auto slopesDescriptor = vsg::DescriptorImage::create(sampler, wavefield_->fft()->slopes(), 2, 0);
        auto detailDisplacementDescriptor = vsg::DescriptorImage::create(sampler, wavefield_->detailFft()->displacement(), 3, 0);
        auto detailSlopesDescriptor = vsg::DescriptorImage::create(sampler, wavefield_->detailFft()->slopes(), 4, 0);
        auto oceanSet = vsg::DescriptorSet::create(oceanLayout, vsg::Descriptors{oceanDescriptor, displacementDescriptor, slopesDescriptor,
                                                                                  detailDisplacementDescriptor, detailSlopesDescriptor});

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
        stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, oceanSet));

        auto drawCommands = vsg::Commands::create();
        drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices}));
        drawCommands->addChild(vsg::BindIndexBuffer::create(indices));
        drawCommands->addChild(vsg::DrawIndexed::create(static_cast<uint32_t>(indices->size()), 1, 0, 0, 0));
        stateGroup->addChild(drawCommands);

        addChild(stateGroup);
    }

    void OceanSurface::update(const Environment& environment, double t)
    {
        // the wave field follows the wind direction
        wavefield_->setDirection(vsg::radians(environment.windDirection()));
        wavefield_->setWindSpeed(environment.windSpeed());
        wavefield_->recalculate();
        wavefield_->updateFft(t);

        const auto sun = environment.sunDirection();
        const auto attenuation = environment.underwaterAttenuation();
        const auto meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;

        (*uniform_)[0] = vsg::vec4(static_cast<float>(sun.x), static_cast<float>(sun.y), static_cast<float>(sun.z), static_cast<float>(environment.cloudiness() / 8.0));
        (*uniform_)[1] = vsg::vec4(environment.fogColor(), static_cast<float>(environment.fogExtinction()));
        (*uniform_)[2] = vsg::vec4(environment.underwaterColor(), meanAttenuation);
        // the FFT sea REPLACES the component waves (the ECKV spectrum already
        // carries the full wind sea - adding the components on top would double
        // it, and Wavefield::height computes the FFT displacement only, so the
        // CPU and GPU surfaces must agree): the vertex stage loops over
        // ocean.params.z components, so 0 in that mode leaves the displacement
        // to the FFT tile alone
        const auto componentCount = wavefield_->model() == Wavefield::Model::FFT
                                        ? size_t{0}
                                        : wavefield_->amplitudes().size();
        (*uniform_)[3] = vsg::vec4(static_cast<float>(t), static_cast<float>(wavefield_->tau()),
                                  static_cast<float>(componentCount), 5.0f); // 5 m assumed water depth
        (*uniform_)[4] = vsg::vec4(attenuation, static_cast<float>(environment.seaLevel()));

        // component wave arrays: amplitude, wavenumber, omega, steepness, direction
        size_t slot = 0;
        for (size_t i = 0; i < Wavefield::maxWaves; ++i)
        {
            const bool valid = i < wavefield_->amplitudes().size();
            const auto a = valid ? wavefield_->amplitudes()[i] : 0.0;
            const auto k = valid ? wavefield_->wavenumbers()[i] : 0.0;
            const auto w = valid ? wavefield_->angularFrequencies()[i] : 0.0;
            const auto q = valid ? wavefield_->steepnesses()[i] : 0.0;
            const auto d = valid ? wavefield_->directions()[i] : vsg::dvec2(1.0, 0.0);

            // amplitude[0].y carries the sun visibility (see setSunVisibility);
            // amplitude[1].y carries the rain rate (the surface response: the
            // rain roughens the ripples and flattens the glitter)
            (*uniform_)[5 + slot] = vsg::vec4(static_cast<float>(a),
                                             slot == 0 ? sunVisibility_ : 0.0f,
                                             slot == 1 ? rainRate_ : 0.0f, 0.0f);
            (*uniform_)[5 + Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(k), 0.0f, 0.0f, 0.0f);
            (*uniform_)[5 + 2 * Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(w), 0.0f, 0.0f, 0.0f);
            (*uniform_)[5 + 3 * Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(q), 0.0f, 0.0f, 0.0f);
            // the wind speed rides in the first direction slot's unused z: the
            // ripple detail scales with it (less wind = fewer capillary ripples)
            (*uniform_)[5 + 4 * Wavefield::maxWaves + slot] = vsg::vec4(
                static_cast<float>(d.x), static_cast<float>(d.y),
                slot == 0 ? static_cast<float>(environment.windSpeed()) : 0.0f, 0.0f);
            ++slot;
        }

        (*uniform_)[5 + 5 * Wavefield::maxWaves] = vsg::vec4(wavefield_->model() == Wavefield::Model::FFT ? 1.0f : 0.0f,
                                                            static_cast<float>(wavefield_->fftParameters().tileSize),
                                                            wavefield_->fftDetail() ? static_cast<float>(wavefield_->detailFft()->parameters().tileSize) : 0.0f,
                                                            static_cast<float>(wavefield_->foam()));

        uniform_->dirty();
    }
}
