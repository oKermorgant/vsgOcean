/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/semantic/SemanticOverlay.h>

#include <vsgOcean/semantic/SemanticPalette.h>

#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/Draw.h>


#include "semantic_overlay_vert.h"
#include "semantic_overlay_frag.h"

namespace vsgOcean
{
    namespace
    {
        // the uniform palette: the model colors in ID order; model 0 (the
        // background) is never looked up, its entry stays unused
        constexpr auto paletteSize = 64u;
    }

    SemanticOverlay::SemanticOverlay(const vsg::ref_ptr<SemanticPass>& semantic, VkExtent2D windowExtent) :
        vsg::Inherit<vsg::View, SemanticOverlay>(vsg::Camera::create(
            vsg::Orthographic::create(0.0, 1.0, 0.0, 1.0, 0.0, 1.0), vsg::LookAt::create(),
            vsg::ViewportState::create(windowExtent)))
    {
        // fullscreen triangle covering the clip space
        auto vertices = vsg::vec3Array::create(
            {{-1.0f, -1.0f, 0.0f},
             {3.0f, -1.0f, 0.0f},
             {-1.0f, 3.0f, 0.0f}});

        // the uniform block: [0] the mode params (model alpha, background
        // alpha, window extent), [1..paletteSize] the per-ID palette colors
        uniform_ = vsg::vec4Array::create(1 + paletteSize);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;
        (*uniform_)[0] = vsg::vec4(0.0f, 0.0f, static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height));
        if (semantic)
        {
            const auto& names = semantic->modelNames();
            for (size_t id = 0; id < names.size() && id < paletteSize; ++id)
            {
                const auto& color = displayColor(names[id]);
                (*uniform_)[1 + id] = vsg::vec4(color.r, color.g, color.b, 1.0f);
            }
        }

        auto sampler = vsg::Sampler::create();
        sampler->magFilter = VK_FILTER_NEAREST; // the ID bytes must not be interpolated
        sampler->minFilter = VK_FILTER_NEAREST;
        sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        vsg::DescriptorSetLayoutBindings overlayBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            // binding 3, NOT 1 or 2: a layout structurally identical to a sibling
            // view's (the present pass, the underwater composite) let the sets
            // alias and the sibling's sampler read THIS pass's image - the
            // photo disappeared exactly in the states where this view existed.
            // An unusual slot keeps the layout unique.
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        auto overlayLayout = vsg::DescriptorSetLayout::create(overlayBindings);

        // the overlay mode mixes the models with the photo, the opaque mode
        // writes alpha 1 everywhere: one blended pipeline covers both modes
        auto blend = vsg::ColorBlendState::create();
        blend->configureAttachments(VK_TRUE); // src_alpha, one_minus_src_alpha

        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;

        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE;

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{overlayLayout}, vsg::PushConstantRanges{});
        auto pipeline = vsg::GraphicsPipeline::create(
            pipelineLayout,
            vsg::ShaderStages{
                vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", semantic_overlay_vert),
                vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", semantic_overlay_frag)},
            vsg::GraphicsPipelineStates{vsg::VertexInputState::create(
                                            vsg::VertexInputState::Bindings{{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}},
                                            vsg::VertexInputState::Attributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}}),
                                        vsg::InputAssemblyState::create(), rasterization, vsg::MultisampleState::create(),
                                        blend, depthStencil});

        auto imageDescriptor = vsg::DescriptorImage::create(
            vsg::ImageInfo::create(sampler, semantic ? semantic->colorView() : nullptr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        auto descriptorSet = vsg::DescriptorSet::create(overlayLayout,
            vsg::Descriptors{vsg::DescriptorBuffer::create(vsg::DataList{uniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                              imageDescriptor});

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
        stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet));

        auto drawCommands = vsg::Commands::create();
        drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices}));
        drawCommands->addChild(vsg::Draw::create(3, 1, 0, 0));
        stateGroup->addChild(drawCommands);

        addChild(stateGroup);
        // NOT setMode(0) here: the compile gates the shader stages by the view's
        // mask, so a view masked off at compile time builds a stageless pipeline
        // and the driver segfaults. The application sets the mode per frame;
        // until then the uniform defaults (alpha 0 everywhere) cull the triangle.
    }

    void SemanticOverlay::setMode(int mode)
    {
        // The view is NEVER masked: masking a view that shares a render pass
        // with sibling views corrupts their draws (their samplers end up
        // reading this pass's other images - the composite showed the black
        // semantic image whenever this view was masked off). Off (mode 0)
        // sets both alphas to 0, and the vertex stage then pushes the whole
        // triangle outside the clip space: the view records, nothing draws.
        (*uniform_)[0].x = mode == 2 ? 0.5f : (mode == 0 ? 0.0f : 1.0f); // the model pixels
        (*uniform_)[0].y = mode == 2 ? 0.0f : (mode == 0 ? 0.0f : 1.0f); // the background (ID 0)
        uniform_->dirty();
    }

    void SemanticOverlay::setWindowExtent(VkExtent2D extent)
    {
        (*uniform_)[0].z = static_cast<float>(extent.width);
        (*uniform_)[0].w = static_cast<float>(extent.height);
        uniform_->dirty();
    }
}
