/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Sky.h>

#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/Draw.h>

#include "sky_vert.h"
#include "sky_frag.h"

namespace vsgOcean
{
    namespace
    {
        // the well-known constellations of the winter sky at northern latitudes:
        // right ascension (hours), declination (degrees) and magnitude of their main stars
        struct CelestialStar
        {
            double ra;
            double dec;
            double magnitude;
        };

        // Orion, Ursa Major (the Big Dipper), Cassiopeia and Taurus
        const CelestialStar starCatalog[] = {
            {5.919, 7.407, 0.45}, {5.242, -8.202, 0.18}, {5.418, 6.350, 1.64},     // Betelgeuse, Rigel, Bellatrix
            {5.679, -9.670, 2.07}, {5.679, -1.943, 1.74}, {5.604, -1.202, 1.69},  // Saiph, Alnitak, Alnilam
            {5.533, -0.299, 2.25},                                               // Mintaka
            {11.062, 61.751, 1.79}, {11.031, 56.382, 2.37}, {11.897, 53.695, 2.44}, // Dubhe, Merak, Phecda
            {12.257, 57.033, 3.31}, {12.900, 55.959, 1.77}, {13.399, 54.925, 2.27}, // Megrez, Alioth, Mizar
            {13.789, 49.313, 1.86},                                                 // Alkaid
            {0.153, 59.150, 2.28}, {0.675, 56.537, 2.24}, {0.945, 60.717, 2.47},  // Caph, Schedar, Gamma Cas
            {1.430, 60.235, 2.68}, {1.907, 63.670, 3.38},                          // Ruchbah, Segin
            {4.599, 16.509, 0.87}, {5.438, 28.608, 1.65}, {4.330, 15.627, 3.65},  // Aldebaran, Elnath, Gamma Tau
            {5.628, 21.143, 3.00}};                                                // Zeta Tau

        constexpr size_t maxStars = 32;      // must match the stars array of the shader
        constexpr double localSiderealTime = 5.7; // hours, chosen so the constellations are well placed

        // direction of a star in the local horizontal frame (+X East, +Y North, +Z Up),
        // from its equatorial coordinates, the scene latitude and the local sidereal time
        vsg::dvec3 starDirection(double ra, double dec, double latitude)
        {
            const double hourAngle = vsg::radians((localSiderealTime - ra) * 15.0);
            const double decRad = vsg::radians(dec);
            const double latRad = vsg::radians(latitude);

            return {-std::cos(decRad) * std::sin(hourAngle),
                    std::cos(latRad) * std::sin(decRad) - std::sin(latRad) * std::cos(decRad) * std::cos(hourAngle),
                    std::sin(latRad) * std::sin(decRad) + std::cos(latRad) * std::cos(decRad) * std::cos(hourAngle)};
        }
    }

    Sky::Sky()
    {
        // fullscreen triangle covering the clip space
        auto vertices = vsg::vec3Array::create(
            {{-1.0f, -1.0f, 0.0f},
             {3.0f, -1.0f, 0.0f},
             {-1.0f, 3.0f, 0.0f}});

        // environment uniform block: [0] sun direction, [1] fog color + density, [2..5] inverse view projection,
        // [6..37] stars of the night sky, [38] Jerlov water color + mean attenuation, [39] camera submersion depth
        environmentUniform_ = vsg::vec4Array::create(6 + maxStars + 2);
        environmentUniform_->properties.dataVariance = vsg::DYNAMIC_DATA;

        vsg::DescriptorSetLayoutBindings environmentBindings{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        auto environmentLayout = vsg::DescriptorSetLayout::create(environmentBindings);

        vsg::VertexInputState::Bindings vertexBindings{{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}};
        vsg::VertexInputState::Attributes vertexAttributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}};

        // the sky is drawn first and writes no depth, so the scene renders on top of it
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

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{environmentLayout}, vsg::PushConstantRanges{});

        auto shaders = vsg::ShaderStages{
            vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", sky_vert),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", sky_frag)};

        auto pipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaders, pipelineStates);

        auto environmentDescriptor = vsg::DescriptorBuffer::create(vsg::DataList{environmentUniform_}, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        auto environmentSet = vsg::DescriptorSet::create(environmentLayout, vsg::Descriptors{environmentDescriptor});

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->add(vsg::BindGraphicsPipeline::create(pipeline));
        stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, environmentSet));

        auto drawCommands = vsg::Commands::create();
        drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices}));
        drawCommands->addChild(vsg::Draw::create(3, 1, 0, 0));
        stateGroup->addChild(drawCommands);

        addChild(stateGroup);
    }

    void Sky::update(const Environment& environment, const vsg::Camera* camera)
    {
        const auto sun = environment.sunDirection();
        (*environmentUniform_)[0] = vsg::vec4(static_cast<float>(sun.x), static_cast<float>(sun.y), static_cast<float>(sun.z), 0.0f);
        (*environmentUniform_)[1] = vsg::vec4(environment.fogColor(), static_cast<float>(environment.fogDensity()));

        if (camera && camera->projectionMatrix && camera->viewMatrix)
        {
            const auto projection = camera->projectionMatrix->transform();
            const auto view = camera->viewMatrix->transform();
            const auto inverse = vsg::mat4(vsg::inverse(projection * view));

            // rows of the inverse view projection, following the std140 layout of the uniform block
            for (uint32_t i = 0; i < 4; ++i)
            {
                (*environmentUniform_)[2 + i] = vsg::vec4(inverse[i][0], inverse[i][1], inverse[i][2], inverse[i][3]);
            }
        }

        // the water the camera is submerged in, if any (Jerlov type)
        const auto attenuation = environment.underwaterAttenuation();
        const float meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;
        (*environmentUniform_)[6 + maxStars] = vsg::vec4(environment.underwaterColor(), meanAttenuation);
        (*environmentUniform_)[6 + maxStars + 1] = vsg::vec4(static_cast<float>(environment.submersionDepth()), 0.0f, 0.0f, 0.0f);

        // the constellations, placed in the local sky at the scene geolocation
        size_t starIndex = 0;
        for (const auto& star : starCatalog)
        {
            if (starIndex >= maxStars) break;

            const auto direction = starDirection(star.ra, star.dec, environment.latitude());
            (*environmentUniform_)[6 + starIndex] = vsg::vec4(static_cast<float>(direction.x), static_cast<float>(direction.y), static_cast<float>(direction.z),
                                                             static_cast<float>(std::pow(10.0, -0.4 * star.magnitude) * 1.2));
            ++starIndex;
        }
        for (; starIndex < maxStars; ++starIndex)
        {
            (*environmentUniform_)[6 + starIndex] = vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        environmentUniform_->dirty();
    }
}
