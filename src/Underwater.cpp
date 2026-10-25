/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OF OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Underwater.h>

#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/Draw.h>

#include "underwater_vert.h"
#include "underwater_frag.h"

namespace vsgOcean
{
    Underwater::Underwater()
    {
        // fullscreen triangle covering the clip space
        auto vertices = vsg::vec3Array::create(
            {{-1.0f, -1.0f, 0.0f},
             {3.0f, -1.0f, 0.0f},
             {-1.0f, 3.0f, 0.0f}});

        // uniform block: [0] water color + mean attenuation, [1] submersion depth + sun height
        uniform_ = vsg::vec4Array::create(2);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        vsg::DescriptorSetLayoutBindings underwaterBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        auto underwaterLayout = vsg::DescriptorSetLayout::create(underwaterBindings);

        vsg::VertexInputState::Bindings vertexBindings{{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}};
        vsg::VertexInputState::Attributes vertexAttributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};

        // drawn last, over the whole scene, blended towards the water colour
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;

        auto blend = vsg::ColorBlendState::create();
        blend->configureAttachments(true);

        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE;

        vsg::GraphicsPipelineStates pipelineStates{
            vsg::VertexInputState::create(vertexBindings, vertexAttributes),
            vsg::InputAssemblyState::create(),
            rasterization,
            vsg::MultisampleState::create(),
            blend,
            depthStencil};

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{underwaterLayout}, vsg::PushConstantRanges{});

        auto shaders = vsg::ShaderStages{
            vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", underwater_vert),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", underwater_frag)};

        auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

        auto underwaterDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{uniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        auto underwaterSet = vsg::DescriptorSet::create(underwaterLayout, vsg::Descriptors{underwaterDescriptor});

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
        stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, underwaterSet));

        auto drawCommands = vsg::Commands::create();
        drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices}));
        drawCommands->addChild(vsg::Draw::create(3, 1, 0, 0));
        stateGroup->addChild(drawCommands);

        addChild(stateGroup);
    }

    void Underwater::update(const Environment& environment)
    {
        const auto attenuation = environment.underwaterAttenuation();
        const float meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;
        const auto sun = environment.sunDirection();

        (*uniform_)[0] = vsg::vec4(environment.underwaterColor(), meanAttenuation);
        (*uniform_)[1] = vsg::vec4(static_cast<float>(environment.submersionDepth()), static_cast<float>(sun.z), 0.0f, 0.0f);

        uniform_->dirty();
    }
}
