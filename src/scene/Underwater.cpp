/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/Underwater.h>

#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/Draw.h>

#include <vsgOcean/scene/LampLights.h>

#include "underwater_vert.h"
#include "underwater_frag.h"

namespace vsgOcean
{
    Underwater::Underwater(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> depth)
    {
        // fullscreen triangle covering the clip space
        auto vertices = vsg::vec3Array::create(
            {{-1.0f, -1.0f, 0.0f},
             {3.0f, -1.0f, 0.0f},
             {-1.0f, 3.0f, 0.0f}});

        // uniform block: [0..3] inverse view projection, [4] eye + submersion,
        // [5] water, [6] attenuation + sun height, [7] extent + sun azimuth + sea level,
        // [8] fog
        uniform_ = vsg::vec4Array::create(9);
        uniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        // the lamp light block: [0] count + per-channel scattering, [1 + 4i..] light i
        lamps_ = vsg::vec4Array::create(1 + MaxLampLights * 4);
        lamps_->properties.dataVariance = vsg::DYNAMIC_DATA;

        // the scene attachments are sampled 1:1, so nearest filtering
        auto sampler = vsg::Sampler::create();
        sampler->magFilter = VK_FILTER_NEAREST;
        sampler->minFilter = VK_FILTER_NEAREST;
        sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        vsg::DescriptorSetLayoutBindings underwaterBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        auto underwaterLayout = vsg::DescriptorSetLayout::create(underwaterBindings);

        vsg::VertexInputState::Bindings vertexBindings{{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}};
        vsg::VertexInputState::Attributes vertexAttributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};

        // the composite replaces the whole image: no depth test, no blending
        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_FALSE;
        depthStencil->depthWriteEnable = VK_FALSE;

        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE;

        vsg::GraphicsPipelineStates pipelineStates{
            vsg::VertexInputState::create(vertexBindings, vertexAttributes),
            vsg::InputAssemblyState::create(),
            rasterization,
            vsg::MultisampleState::create(),
            vsg::ColorBlendState::create(),
            depthStencil};

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{underwaterLayout}, vsg::PushConstantRanges{});

        auto shaders = vsg::ShaderStages{
            vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", underwater_vert),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", underwater_frag)};

        auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

        auto uniformDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{uniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        auto lampsDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{lamps_}, 3, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        colorDescriptor_ = vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        depthDescriptor_ = vsg::DescriptorImage::create(vsg::ImageInfo::create(sampler, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        descriptorSet_ = vsg::DescriptorSet::create(underwaterLayout, vsg::Descriptors{uniformDescriptor, colorDescriptor_, depthDescriptor_, lampsDescriptor});

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
        stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet_));

        auto drawCommands = vsg::Commands::create();
        drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices}));
        drawCommands->addChild(vsg::Draw::create(3, 1, 0, 0));
        stateGroup->addChild(drawCommands);

        addChild(stateGroup);
    }

    void Underwater::setSceneTextures(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> depth)
    {
        if (!colorDescriptor_ || !depthDescriptor_ || colorDescriptor_->imageInfoList.empty() || depthDescriptor_->imageInfoList.empty()) return;

        colorDescriptor_->imageInfoList[0]->imageView = std::move(color);
        depthDescriptor_->imageInfoList[0]->imageView = std::move(depth);

        // force the next compile (vsg::CompileManager) to rebind the new views
        descriptorSet_->release();
    }

    void Underwater::update(const Environment& environment, const vsg::Camera* camera, double submersion,
                            const std::vector<LampLight>* lamps)
    {
        const auto attenuation = environment.underwaterAttenuation();
        const auto meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;
        const auto sun = environment.sunDirection();

        // horizontal beam attenuation: roughly a third of the vertical diffuse
        // attenuation of the Jerlov classification (scattering dominates the beam)
        const vsg::vec3 beam(attenuation.x / 3.0f, attenuation.y / 3.0f, attenuation.z / 3.0f);

        // atmospheric fog color, near-black at night like the sky (a moonless night
        // has no haze light source)
        const auto day = std::clamp((sun.z - -0.05) / (0.25 - -0.05), 0.0, 1.0);
        const double dayFactor = day * day * (3.0 - 2.0 * day); // smoothstep
        const vsg::vec3 fogColor = vsg::mix(vsg::vec3(0.002f, 0.003f, 0.006f), environment.fogColor(), static_cast<float>(dayFactor));

        (*uniform_)[4] = vsg::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(submersion));
        (*uniform_)[5] = vsg::vec4(environment.underwaterColor(), meanAttenuation);
        (*uniform_)[6] = vsg::vec4(beam, static_cast<float>(sun.z));
        (*uniform_)[7] = vsg::vec4(1.0f, 1.0f, 0.0f, static_cast<float>(environment.seaLevel()));
        (*uniform_)[8] = vsg::vec4(fogColor, static_cast<float>(environment.fogExtinction()));

        if (camera && camera->projectionMatrix && camera->viewMatrix)
        {
            const auto projection = camera->projectionMatrix->transform();
            const auto view = camera->viewMatrix->transform();
            // vsg matrices are standard column-convention: the composite is
            // projection * view, and vsg's columns upload as GLSL columns as is
            const auto inverse = vsg::mat4(vsg::inverse(projection * view));

            for (uint32_t i = 0; i < 4; ++i)
            {
                (*uniform_)[i] = vsg::vec4(inverse[i][0], inverse[i][1], inverse[i][2], inverse[i][3]);
            }

            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                (*uniform_)[4] = vsg::vec4(static_cast<float>(lookAt->eye.x), static_cast<float>(lookAt->eye.y),
                                          static_cast<float>(lookAt->eye.z), static_cast<float>(submersion));
            }

            // [7].z packs the sun azimuth: with [6].w (the sun height) the shader can
            // reconstruct the full sun direction for the directional in-scattering.
            // [7].xy is the extent of the SAMPLED SCENE TARGET, NOT the camera's
            // viewportState: vsg's WindowResizeHandler rescales the camera viewport
            // to the window while the offscreen target stays fixed, and normalizing
            // the composite's uv by the window extent would sample the wrong
            // sub-rectangle after a resize (the image shifts and smears)
            const auto sunAzimuth = static_cast<float>(environment.sunAzimuth() * vsg::radians(1.0));
            uint32_t extentX = 1, extentY = 1;
            if (colorDescriptor_ && !colorDescriptor_->imageInfoList.empty())
            {
                if (const auto& colorView = colorDescriptor_->imageInfoList[0]->imageView)
                {
                    extentX = colorView->image->extent.width;
                    extentY = colorView->image->extent.height;
                }
            }
            (*uniform_)[7] = vsg::vec4(static_cast<float>(extentX), static_cast<float>(extentY), sunAzimuth,
                                      static_cast<float>(environment.seaLevel()));
        }

        uniform_->dirty();

        // the volumetric lamp cones: the scene's point and spot lights, in world
        // space (collected once per frame by the Scene and shared with the marine
        // snow and every pipeline), with the water's per-channel scattering
        // coefficient the march scatters with (the composite uniform block is full -
        // this second block carries both)
        if (lamps)
        {
            packLampLights(*lamps, environment.underwaterScattering(), *lamps_);

            // the composite's settings ride in the spare components of the first
            // light's cone slot (GLSL lamp[2].zw - the shader never reads them):
            // .z carries the underwater DOF blur toggle. The composite's own block
            // is full (see lessons.md - its layout must not change size)
            if (lamps_->size() > 4) (*lamps_)[3].z = environment.underwaterBlur() ? 1.0f : 0.0f;
            lamps_->dirty();
        }
    }
}
