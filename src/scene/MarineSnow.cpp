/* <editor-fold desc="MIT License">

Copyright (c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/MarineSnow.h>

#include <vsg/commands/Draw.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/vk/Device.h>

#include <random>

#include "snow_vert.h"
#include "snow_frag.h"

namespace vsgOcean
{
    MarineSnow::MarineSnow(uint32_t particles, float box) :
        box_(box),
        particles_(particles)
    {
        // the particle base positions: random points of a unit cube, seeded so the
        // field is reproducible; w carries the per-particle random seed (the
        // density draw and the flake radius come from it in-shader)
        auto bases = vsg::vec4Array::create(particles);
        {
            std::mt19937 generator(42);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            for (uint32_t i = 0; i < particles; ++i)
            {
                (*bases)[i] = vsg::vec4(unit(generator), unit(generator), unit(generator), unit(generator));
            }
        }
        bases->properties.dataVariance = vsg::STATIC_DATA;

        uniform_ = vsg::vec4Array::create(6 + 1 + MaxLampLights * 4);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        vsg::DescriptorSetLayoutBindings snowBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
        auto snowLayout = vsg::DescriptorSetLayout::create(snowBindings);

        // the camera state (projection + modelview) comes through the push
        // constants, provided by the vsg record traversal - one snow field renders
        // from any number of cameras
        vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(vsg::mat4)}};

        // the flakes are camera-facing quads with no vertex input: the vertex
        // shader pulls the base positions from the storage buffer, two triangles
        // (six vertices) per particle
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_TRUE;
        depthStencil->depthWriteEnable = VK_FALSE; // the flakes never occlude
        depthStencil->depthCompareOp = VK_COMPARE_OP_GREATER; // vsg's reversed depth convention

        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE;

        auto blend = vsg::ColorBlendState::create();
        blend->configureAttachments(true);

        vsg::GraphicsPipelineStates pipelineStates{
            vsg::VertexInputState::create(),
            vsg::InputAssemblyState::create(),
            rasterization,
            vsg::MultisampleState::create(),
            blend,
            depthStencil};

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{snowLayout}, pushConstantRanges);

        auto shaders = vsg::ShaderStages{
            vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", snow_vert),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", snow_frag)};

        auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

        auto snowDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{uniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        auto particleDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{bases}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto snowSet = vsg::DescriptorSet::create(snowLayout, vsg::Descriptors{snowDescriptor, particleDescriptor});

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
        stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, snowSet));

        auto drawCommands = vsg::Commands::create();
        drawCommands->addChild(vsg::Draw::create(6 * particles, 1, 0, 0));
        stateGroup->addChild(drawCommands);

        addChild(stateGroup);
    }

    void MarineSnow::update(const Environment& environment, double t, const std::vector<LampLight>& lamps)
    {
        const auto attenuation = environment.underwaterAttenuation();
        const auto meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;

        // the density as a fraction of the particle pool: 1.0 density unit is the
        // unit field (4096 flakes), the pool is 10x that - so the density range
        // 0..10 maps to the fraction 0..1
        const float fraction = static_cast<float>(environment.marineSnowDensity()) * (4096.0f / static_cast<float>(particles_));
        (*uniform_)[0] = vsg::vec4(static_cast<float>(t), fraction, box_, static_cast<float>(environment.cloudiness() / 8.0));
        (*uniform_)[1] = vsg::vec4(environment.waterCurrent(), static_cast<float>(environment.seaLevel()));
        (*uniform_)[2] = vsg::vec4(environment.sunDirection(), 0.0f);
        (*uniform_)[3] = vsg::vec4(environment.underwaterColor(), meanAttenuation);
        (*uniform_)[4] = vsg::vec4(attenuation, 0.0f);
        (*uniform_)[5] = vsg::vec4(attenuation.x / 3.0f, attenuation.y / 3.0f, attenuation.z / 3.0f, 0.0f); // the horizontal beam

        packLampLights(lamps, environment.underwaterScattering(), *uniform_, 6);

        uniform_->dirty();
    }
}
