/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/OceanSurface.h>

#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/DrawIndexed.h>

#include "ocean_vert.h"
#include "ocean_frag.h"

namespace vsgOcean
{
    OceanSurface::OceanSurface(double size, uint32_t cells) :
        wavefield_(Wavefield::create())
    {
        // flat grid on the mean water level (z = 0), centred at the origin
        const uint32_t verticesPerSide = cells + 1;
        auto vertices = vsg::vec3Array::create(static_cast<size_t>(verticesPerSide) * verticesPerSide);
        auto indices = vsg::uintArray::create(static_cast<size_t>(cells) * cells * 6);

        const float step = static_cast<float>(size / cells);
        const float halfSize = static_cast<float>(size) * 0.5f;

        for (uint32_t j = 0; j < verticesPerSide; ++j)
        {
            for (uint32_t i = 0; i < verticesPerSide; ++i)
            {
                (*vertices)[j * verticesPerSide + i] = vsg::vec3(-halfSize + i * step, -halfSize + j * step, 0.0f);
            }
        }

        size_t index = 0;
        for (uint32_t j = 0; j < cells; ++j)
        {
            for (uint32_t i = 0; i < cells; ++i)
            {
                const uint32_t base = j * verticesPerSide + i;
                (*indices)[index++] = base;
                (*indices)[index++] = base + 1;
                (*indices)[index++] = base + verticesPerSide;
                (*indices)[index++] = base + 1;
                (*indices)[index++] = base + verticesPerSide + 1;
                (*indices)[index++] = base + verticesPerSide;
            }
        }

        // uniform block: [0] sun direction, [1] fog color + density, [2] water color,
        // [3] params (t, tau, number, unused), [4] camera eye, [5..44] component wave arrays
        uniform_ = vsg::vec4Array::create(5 + 5 * Wavefield::maxWaves);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        vsg::DescriptorSetLayoutBindings oceanBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        auto oceanLayout = vsg::DescriptorSetLayout::create(oceanBindings);

        // the projection and modelview matrices are pushed by the vsg record traversal
        vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(vsg::mat4)}};

        vsg::VertexInputState::Bindings vertexBindings{{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}};
        vsg::VertexInputState::Attributes vertexAttributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};

        // visible from above and from below
        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE;

        // a transparent pass: blended over the scene beneath the surface, no depth writes,
        // so it must be drawn after the opaque scene content
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthWriteEnable = VK_FALSE;

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
        auto oceanSet = vsg::DescriptorSet::create(oceanLayout, vsg::Descriptors{oceanDescriptor});

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

    void OceanSurface::update(const Environment& environment, const vsg::Camera* camera, double t)
    {
        // the wave field follows the wind direction
        wavefield_->setDirection(vsg::radians(environment.windDirection()));
        wavefield_->recalculate();

        const auto sun = environment.sunDirection();
        const auto attenuation = environment.underwaterAttenuation();
        const float meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;

        (*uniform_)[0] = vsg::vec4(static_cast<float>(sun.x), static_cast<float>(sun.y), static_cast<float>(sun.z), 0.0f);
        (*uniform_)[1] = vsg::vec4(environment.fogColor(), static_cast<float>(environment.fogDensity()));
        (*uniform_)[2] = vsg::vec4(environment.underwaterColor(), meanAttenuation);
        (*uniform_)[3] = vsg::vec4(static_cast<float>(t), static_cast<float>(wavefield_->tau()),
                                  static_cast<float>(wavefield_->amplitudes().size()), 5.0f); // 5 m assumed water depth

        vsg::vec4 eye(0.0f, 0.0f, 0.0f, 1.0f);
        if (camera)
        {
            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                eye = vsg::vec4(static_cast<float>(lookAt->eye.x), static_cast<float>(lookAt->eye.y), static_cast<float>(lookAt->eye.z), 1.0f);
            }
        }
        (*uniform_)[4] = eye;

        // component wave arrays: amplitude, wavenumber, omega, steepness, direction
        size_t slot = 0;
        for (size_t i = 0; i < Wavefield::maxWaves; ++i)
        {
            const bool valid = i < wavefield_->amplitudes().size();
            const double a = valid ? wavefield_->amplitudes()[i] : 0.0;
            const double k = valid ? wavefield_->wavenumbers()[i] : 0.0;
            const double w = valid ? wavefield_->angularFrequencies()[i] : 0.0;
            const double q = valid ? wavefield_->steepnesses()[i] : 0.0;
            const auto d = valid ? wavefield_->directions()[i] : vsg::dvec2(1.0, 0.0);

            (*uniform_)[5 + slot] = vsg::vec4(static_cast<float>(a), 0.0f, 0.0f, 0.0f);
            (*uniform_)[5 + Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(k), 0.0f, 0.0f, 0.0f);
            (*uniform_)[5 + 2 * Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(w), 0.0f, 0.0f, 0.0f);
            (*uniform_)[5 + 3 * Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(q), 0.0f, 0.0f, 0.0f);
            (*uniform_)[5 + 4 * Wavefield::maxWaves + slot] = vsg::vec4(static_cast<float>(d.x), static_cast<float>(d.y), 0.0f, 0.0f);
            ++slot;
        }

        uniform_->dirty();
    }
}
