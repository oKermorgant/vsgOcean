/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/Sky.h>

#include <array>
#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/commands/Draw.h>

#include "sky_vert.h"
#include "sky_frag.h"

namespace vsgOcean
{
    namespace
    {
        // the classical constellations: right ascension (hours), declination
        // (degrees) and magnitude of their main stars. The catalog follows the
        // hemisphere: the northern sky carries Orion, Ursa Major (the Big Dipper),
        // Ursa Minor (the Little Dipper), Cassiopeia and Taurus; the southern sky
        // carries the southern classics - Crux (the Southern Cross), Centaurus,
        // Carina, Scorpius - plus Sirius, the brightest star of either sky.
        struct CelestialStar
        {
            double ra;
            double dec;
            double magnitude;
        };

        // Orion, Ursa Major, Cassiopeia and Taurus
        constexpr std::array<CelestialStar, 23> northernCatalog{{
            {5.919, 7.407, 0.45}, {5.242, -8.202, 0.18}, {5.418, 6.350, 1.64},     // Betelgeuse, Rigel, Bellatrix
            {5.679, -9.670, 2.07}, {5.679, -1.943, 1.74}, {5.604, -1.202, 1.69},  // Saiph, Alnitak, Alnilam
            {5.533, -0.299, 2.25},                                               // Mintaka
            {11.062, 61.751, 1.79}, {11.031, 56.382, 2.37}, {11.897, 53.695, 2.44}, // Dubhe, Merak, Phecda
            {12.257, 57.033, 3.31}, {12.900, 55.959, 1.77}, {13.399, 54.925, 2.27}, // Megrez, Alioth, Mizar
            {13.789, 49.313, 1.86},                                                 // Alkaid
            {0.153, 59.150, 2.28}, {0.675, 56.537, 2.24}, {0.945, 60.717, 2.47},  // Caph, Schedar, Gamma Cas
            {1.430, 60.235, 2.68}, {1.907, 63.670, 3.38},                          // Ruchbah, Segin
            {4.599, 16.509, 0.87}, {5.438, 28.608, 1.65}, {4.330, 15.627, 3.65},  // Aldebaran, Elnath, Gamma Tau
            {5.628, 21.143, 3.00}}};                                               // Zeta Tau

        // Ursa Minor (the Little Dipper): Polaris at the tip of the handle
        constexpr std::array<CelestialStar, 7> minorCatalog{{
            {2.530, 89.264, 1.98},   // Polaris
            {14.845, 74.155, 2.08},  // Kochab
            {15.345, 71.834, 3.05},  // Pherkad
            {17.537, 86.586, 4.36},  // Yildun
            {16.766, 82.037, 4.23},  // Epsilon UMi
            {15.734, 77.794, 4.32},  // Zeta UMi
            {16.291, 75.755, 4.95}}}; // Eta UMi

        // Crux, Centaurus, Carina, Scorpius and Sirius: the classical southern sky
        constexpr std::array<CelestialStar, 18> southernCatalog{{
            {12.443, -63.099, 0.77}, {12.795, -59.689, 1.25},                       // Acrux, Mimosa (Crux)
            {12.519, -57.113, 1.63}, {12.253, -58.749, 2.79},                        // Gacrux, Imai (Crux)
            {14.660, -60.834, -0.27}, {14.064, -60.373, 0.61}, {14.111, -36.370, 2.06}, // Rigil Kentaurus, Hadar, Menkent (Centaurus)
            {6.399, -52.696, -0.74}, {9.220, -69.717, 1.67}, {8.375, -59.510, 1.86}, // Canopus, Miaplacidus, Avior (Carina)
            {9.285, -59.275, 2.21},                                                   // Aspidiske (Carina)
            {16.490, -26.432, 1.09}, {16.006, -22.622, 2.32}, {17.560, -37.104, 1.63}, // Antares, Dschubba, Shaula (Scorpius)
            {17.622, -42.998, 1.87}, {16.091, -19.805, 2.62}, {17.512, -37.296, 2.69}, // Sargas, Graffias, Lesath (Scorpius)
            {6.752, -16.716, -1.46}}};                                               // Sirius (Canis Major)

        constexpr size_t maxStars = 32;      // must match the stars array of the shader
        // hours, chosen so each hemisphere's constellations are well placed: the
        // winter stars for the north (Orion and the Dipper high), Crux and Scorpius
        // near the meridian for the south
        constexpr double northernSiderealTime = 5.7;
        constexpr double southernSiderealTime = 12.6;

        // direction of a star in the local horizontal frame (+X East, +Y North, +Z Up),
        // from its equatorial coordinates, the scene latitude and the local sidereal time
        vsg::dvec3 starDirection(double ra, double dec, double latitude, double siderealTime)
        {
            const auto hourAngle = vsg::radians((siderealTime - ra) * 15.0);
            const auto decRad = vsg::radians(dec);
            const auto latRad = vsg::radians(latitude);

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

        // environment uniform block: [0] sun direction, [1] fog color + density,
        // [2..33] stars of the night sky, [34] Jerlov water color + mean attenuation,
        // [35] wave params, [36..67] component waves. The camera state comes
        // through push constants, so one sky node renders correctly from any camera
        environmentUniform_ = vsg::vec4Array::create(2 + maxStars + 2 + 4 * Wavefield::maxWaves);
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

        // the projection and modelview of the enclosing view are pushed by the record
        // traversal, giving the sky its camera
        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{environmentLayout},
                                                          vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(vsg::mat4)}});

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

    void Sky::update(const Environment& environment, double t, const Wavefield* wavefield)
    {
        const auto sun = environment.sunDirection();
        (*environmentUniform_)[0] = vsg::vec4(static_cast<float>(sun.x), static_cast<float>(sun.y), static_cast<float>(sun.z), static_cast<float>(environment.cloudiness() / 8.0));
        (*environmentUniform_)[1] = vsg::vec4(environment.fogColor(), static_cast<float>(environment.fogExtinction()));

        // the water (Jerlov type); the underwater mode is evaluated per camera, in-shader
        const auto attenuation = environment.underwaterAttenuation();
        const auto meanAttenuation = (attenuation.x + attenuation.y + attenuation.z) / 3.0f;
        (*environmentUniform_)[2 + maxStars] = vsg::vec4(environment.underwaterColor(), meanAttenuation);

        // the wave field, so each camera evaluates its own submersion below the waves;
        // a null wave field (no ocean surface) keeps every camera above the water
        const bool waves = wavefield != nullptr;
        const auto number = waves ? std::min(wavefield->amplitudes().size(), Wavefield::maxWaves) : 0;
        (*environmentUniform_)[2 + maxStars + 1] = vsg::vec4(static_cast<float>(t),
                                                            static_cast<float>(waves ? wavefield->tau() : 1.0),
                                                            static_cast<float>(waves ? environment.seaLevel() : -1e6),
                                                            static_cast<float>(number));

        for (size_t i = 0; i < Wavefield::maxWaves; ++i)
        {
            const bool valid = i < number;
            (*environmentUniform_)[2 + maxStars + 2 + i] = vsg::vec4(static_cast<float>(valid ? wavefield->amplitudes()[i] : 0.0), 0.0f, 0.0f, 0.0f);
            (*environmentUniform_)[2 + maxStars + 2 + Wavefield::maxWaves + i] = vsg::vec4(static_cast<float>(valid ? wavefield->wavenumbers()[i] : 0.0), 0.0f, 0.0f, 0.0f);
            (*environmentUniform_)[2 + maxStars + 2 + 2 * Wavefield::maxWaves + i] = vsg::vec4(static_cast<float>(valid ? wavefield->angularFrequencies()[i] : 0.0), 0.0f, 0.0f, 0.0f);
            const auto waveDirection = valid ? wavefield->directions()[i] : vsg::dvec2(0.0, 0.0);
            (*environmentUniform_)[2 + maxStars + 2 + 3 * Wavefield::maxWaves + i] = vsg::vec4(static_cast<float>(waveDirection.x), static_cast<float>(waveDirection.y), 0.0f, 0.0f);
        }

        // the constellations, placed in the local sky at the scene geolocation: the
        // catalog follows the hemisphere (the northern Dipper never rises from the
        // southern ocean; Crux never rises from the north)
        const bool northern = environment.latitude() >= 0.0;
        const double siderealTime = northern ? northernSiderealTime : southernSiderealTime;
        size_t starIndex = 0;
        const auto place = [&](const auto& catalog)
        {
            for (const auto& star : catalog)
            {
                if (starIndex >= maxStars) break;

                const auto direction = starDirection(star.ra, star.dec, environment.latitude(), siderealTime);
                (*environmentUniform_)[2 + starIndex] = vsg::vec4(static_cast<float>(direction.x), static_cast<float>(direction.y), static_cast<float>(direction.z),
                                                                  static_cast<float>(std::pow(10.0, -0.4 * star.magnitude) * 1.2));
                ++starIndex;
            }
        };
        if (northern)
        {
            place(northernCatalog);
            place(minorCatalog); // Ursa Minor circles the north celestial pole
        }
        else
        {
            place(southernCatalog);
        }
        for (; starIndex < maxStars; ++starIndex)
        {
            (*environmentUniform_)[2 + starIndex] = vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        environmentUniform_->dirty();
    }
}
