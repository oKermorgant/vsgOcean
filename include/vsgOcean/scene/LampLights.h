#pragma once

/* <editor-fold desc="MIT License">

Copyright (c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsg/core/Array.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Transform.h>

#include <vector>

namespace vsgOcean
{
    /// The maximum number of lights the volumetric light transport (lamp cones in the
    /// composite, marine snow reflections) marches / evaluates per frame.
    inline constexpr size_t MaxLampLights = 8;

    /// A point or spot light in WORLD space, collected from the scene graph each
    /// frame for the volumetric light transport. Positions and directions are
    /// frame-relative (set by the SDF loader); the collector carries them through
    /// the ancestor transforms.
    struct LampLight
    {
        vsg::vec3 position;  // world
        vsg::vec3 direction; // world, unit (spot)
        vsg::vec3 color;
        float intensity = 0.0f;
        float cosInner = -1.0f; // point lights carry no cone
        float cosOuter = -1.0f;
        float range = 0.0f; // 0: no cutoff
    };

    /// Collect the point and spot lights under a scene graph root (the scene's
    /// world model), up to MaxLampLights, with their world transforms.
    [[nodiscard]] std::vector<LampLight> collectLampLights(vsg::Node* root);

    /// The per-light data packed for the shaders: a std140 vec4 array laid out as
    /// [offset] light count + the per-channel scattering coefficient b, then 4
    /// slots per light (position + intensity, direction + cos(inner), cos(outer) +
    /// range, colour). Writes 1 + 4*count slots from `offset` (sized by the caller).
    void packLampLights(const std::vector<LampLight>& lights, const vsg::vec3& scattering, vsg::vec4Array& out, size_t offset = 0);
}
