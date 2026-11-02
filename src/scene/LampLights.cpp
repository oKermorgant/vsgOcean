/* <editor-fold desc="MIT License">

Copyright (c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/LampLights.h>

#include <vsg/lighting/PointLight.h>
#include <vsg/lighting/SpotLight.h>

#include <cmath>

namespace vsgOcean
{
    namespace
    {
        // collects the lights with their world transforms: the accumulated ancestor
        // matrix of the transform stack carries the frame-relative light placement
        class LampCollector : public vsg::Visitor
        {
        public:
            std::vector<LampLight> lamps;
            vsg::dmat4 matrix;

            void apply(vsg::Transform& transform) override
            {
                const auto previous = matrix;
                matrix = transform.transform(matrix);
                transform.traverse(*this);
                matrix = previous;
            }

            void apply(vsg::SpotLight& light) override
            {
                if (lamps.size() >= MaxLampLights) return;

                const vsg::dvec3 position = matrix * light.position;
                const vsg::dvec4 directionH = matrix * vsg::dvec4(light.direction.x, light.direction.y, light.direction.z, 0.0);
                LampLight lamp;
                lamp.position = vsg::vec3(position.x, position.y, position.z);
                lamp.direction = vsg::normalize(vsg::vec3(static_cast<float>(directionH.x), static_cast<float>(directionH.y), static_cast<float>(directionH.z)));
                lamp.color = light.color;
                lamp.intensity = light.intensity;
                lamp.cosInner = static_cast<float>(std::cos(light.innerAngle));
                lamp.cosOuter = static_cast<float>(std::cos(light.outerAngle));
                lamp.range = static_cast<float>(light.radius);
                lamps.push_back(lamp);
            }

            void apply(vsg::PointLight& light) override
            {
                if (lamps.size() >= MaxLampLights) return;

                const vsg::dvec3 position = matrix * light.position;
                LampLight lamp;
                lamp.position = vsg::vec3(position.x, position.y, position.z);
                lamp.color = light.color;
                lamp.intensity = light.intensity;
                lamp.range = static_cast<float>(light.radius);
                lamps.push_back(lamp);
            }
        };
    }

    std::vector<LampLight> collectLampLights(vsg::Node* root)
    {
        LampCollector collector;
        if (root) root->accept(collector);
        return std::move(collector.lamps);
    }

    void packLampLights(const std::vector<LampLight>& lights, const vsg::vec3& scattering, vsg::vec4Array& out, size_t offset)
    {
        if (out.size() < offset + 1 + MaxLampLights * 4) return;

        out[offset] = vsg::vec4(static_cast<float>(lights.size()), scattering.x, scattering.y, scattering.z);
        for (size_t i = 0; i < lights.size() && i < MaxLampLights; ++i)
        {
            const auto& lamp = lights[i];
            const size_t base = offset + 1 + i * 4;
            out[base + 0] = vsg::vec4(lamp.position, lamp.intensity);
            out[base + 1] = vsg::vec4(lamp.direction, lamp.cosInner);
            out[base + 2] = vsg::vec4(lamp.cosOuter, lamp.range, 0.0f, 0.0f);
            out[base + 3] = vsg::vec4(lamp.color, 0.0f);
        }
    }
}
