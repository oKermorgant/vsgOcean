/* <editor-fold desc="MIT License">

Copyright (c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/Rain.h>

#include <vsg/commands/Draw.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/io/Logger.h>
#include <vsg/vk/Device.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "rain_vert.h"
#include "rain_ring_vert.h"
#include "rain_frag.h"

namespace vsgOcean
{
    namespace
    {
        // the particle base positions: random points of a unit cube with a
        // per-particle seed, like the marine snow pool (a shuffled set, so the
        // first count particles are an unbiased stable subset and the drawn
        // vertex count can follow the rain rate directly)
        vsg::ref_ptr<vsg::vec4Array> makePool(uint32_t count)
        {
            auto pool = vsg::vec4Array::create(count);
            {
                std::mt19937 generator(7);
                std::uniform_real_distribution<float> unit(0.0f, 1.0f);
                for (uint32_t i = 0; i < count; ++i)
                {
                    (*pool)[i] = vsg::vec4(unit(generator), unit(generator), unit(generator), unit(generator));
                }
            }
            pool->properties.dataVariance = vsg::STATIC_DATA;
            return pool;
        }

        // the shared pipeline states: the particles blend additively-ish over
        // the scene and never occlude (depth test on, no depth write, the
        // reversed-depth GREATER compare like the snow)
        void configurePipelineStates(vsg::GraphicsPipelineStates& pipelineStates)
        {
            auto depthStencil = vsg::DepthStencilState::create();
            depthStencil->depthTestEnable = VK_TRUE;
            depthStencil->depthWriteEnable = VK_FALSE;
            depthStencil->depthCompareOp = VK_COMPARE_OP_GREATER;

            auto rasterization = vsg::RasterizationState::create();
            rasterization->cullMode = VK_CULL_MODE_NONE;

            auto blend = vsg::ColorBlendState::create();
            blend->configureAttachments(true);

            pipelineStates = {vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
                              rasterization, vsg::MultisampleState::create(), blend, depthStencil};
        }
    }

    Rain::Rain(uint32_t streaks, uint32_t rings, float box) :
        box_(box),
        streaks_(streaks),
        rings_(rings)
    {
        uniform_ = vsg::vec4Array::create(3);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        // bindings: 0 the uniform block, 1 the streak pool, 2 the ring pool;
        // the fragment stage reads the uniform block too (the veiling tone)
        vsg::DescriptorSetLayoutBindings rainBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
        auto rainLayout = vsg::DescriptorSetLayout::create(rainBindings);

        // the camera state (projection + modelview) comes through the push
        // constants, provided by the vsg record traversal - one rain field
        // renders from any number of cameras
        vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(vsg::mat4)}};

        vsg::GraphicsPipelineStates pipelineStates;
        configurePipelineStates(pipelineStates);
        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{rainLayout}, pushConstantRanges);

        // the falling streaks
        {
            auto shaders = vsg::ShaderStages{
                vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", rain_vert),
                vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", rain_frag)};
            auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

            auto stateGroup = vsg::StateGroup::create();
            stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
            auto drawCommands = vsg::Commands::create();
            streakDraw_ = vsg::Draw::create(6 * streaks_, 1, 0, 0);
            drawCommands->addChild(streakDraw_);
            stateGroup->addChild(drawCommands);
            addChild(stateGroup);
        }

        // the splash rings on the sea level
        {
            auto shaders = vsg::ShaderStages{
                vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", rain_ring_vert),
                vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", rain_frag)};
            auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

            auto stateGroup = vsg::StateGroup::create();
            stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
            auto drawCommands = vsg::Commands::create();
            ringDraw_ = vsg::Draw::create(6 * rings_, 1, 0, 0);
            drawCommands->addChild(ringDraw_);
            stateGroup->addChild(drawCommands);
            addChild(stateGroup);
        }

        // the pools and the uniform block, bound once for both pipelines
        auto uniformDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{uniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        auto streakDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{makePool(streaks_)}, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto ringDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{makePool(rings_)}, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto rainSet = vsg::DescriptorSet::create(rainLayout, vsg::Descriptors{uniformDescriptor, streakDescriptor, ringDescriptor});

        // rebind the same descriptor set on both state groups (the pools are
        // shared; each pipeline only reads the bindings it uses)
        for (auto& child : children)
        {
            if (auto stateGroup = child->cast<vsg::StateGroup>())
            {
                stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, rainSet));
            }
        }
    }

    void Rain::update(const Environment& environment, double t)
    {
        const auto rate = environment.rainRate();
        const auto sun = environment.sunDirection();

        // the veiling tone follows the sky: full in clear daylight, dimmed by
        // the cloud cover, near-black at night (no ambient floors anywhere)
        const auto day = std::clamp((sun.z - -0.05) / (0.25 - -0.05), 0.0, 1.0);
        const double dayFactor = day * day * (3.0 - 2.0 * day);
        const auto brightness = dayFactor * (1.0 - 0.6 * environment.cloudiness() / 8.0);

        // the visible counts follow the rate (the shuffled pools make the
        // first count particles a stable subset, like the marine snow); the
        // units are HALF the pools - even a downpour reads as a veil, and the
        // splash rings stay sparse hints on the water
        const auto visibleStreaks = static_cast<uint32_t>(std::lround(std::clamp(rate * 4096.0, 0.0, static_cast<double>(streaks_))));
        const auto visibleRings = static_cast<uint32_t>(std::lround(std::clamp(rate * 256.0, 0.0, static_cast<double>(rings_))));
        streakDraw_->vertexCount = rate > 0.0 ? 6 * visibleStreaks : 0;
        ringDraw_->vertexCount = rate > 0.0 ? 6 * visibleRings : 0;

        // the wind vector (the direction is the meteorological angle in degrees)
        const auto windAngle = vsg::radians(environment.windDirection());
        const vsg::vec2 wind(static_cast<float>(environment.windSpeed() * std::cos(windAngle)),
                              static_cast<float>(environment.windSpeed() * std::sin(windAngle)));
        (*uniform_)[0] = vsg::vec4(static_cast<float>(t), static_cast<float>(rate), box_,
                                   static_cast<float>(environment.cloudiness() / 8.0));
        (*uniform_)[1] = vsg::vec4(static_cast<float>(wind.x), static_cast<float>(wind.y),
                                   static_cast<float>(environment.seaLevel()), static_cast<float>(brightness));
        uniform_->dirty();
    }
}
