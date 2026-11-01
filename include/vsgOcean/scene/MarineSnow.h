#pragma once

/* <editor-fold desc="MIT License">

Copyright (c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>
#include <vsgOcean/core/Environment.h>
#include <vsgOcean/scene/LampLights.h>

namespace vsgOcean
{
    /// Marine snow: the drifting organic particles of the water column - small
    /// flakes that follow the water current and reflect the light that reaches
    /// them (the sun surviving to their depth, and the scene's point / spot lamps -
    /// snow sparkles in the ROV's beams at night).
    ///
    /// The particles live in a box wrapped around the camera: the box re-centres
    /// on the eye each frame, so a fixed set of particles covers the view wherever
    /// it goes; they advect along the water current (Environment::waterCurrent -
    /// the seam for a dynamics-driven current topic) and their number follows the
    /// environment's marine snow density (0 disables the field). The camera state
    /// comes through the push constants at record time, so one snow field renders
    /// from any number of cameras; it fades in with the camera's submersion.
    class VSG_OCEAN_EXPORT MarineSnow : public vsg::Inherit<vsg::Group, MarineSnow>
    {
    public:
        /// particles: the number of flakes in the camera-following box (the density
        /// scales how many of them are visible). box: the edge of the box, metres.
        explicit MarineSnow(uint32_t particles = 4096, float box = 25.0f);

        /// Refresh the snow from the environment (density, current, sun, water
        /// optics), the simulation time t in seconds, and the scene's world model
        /// (the lamp collection root, as for the composite's volumetric cones).
        /// Call every frame, before record.
        void update(const Environment& environment, double t, vsg::Node* lightsRoot);

    protected:
        const float box_;

        // uniform block: [0] params (t, density, box), [1] current + sea level,
        // [2] sun direction, [3] water colour + mean attenuation, [4] vertical
        // attenuation, [5] beam attenuation, [6..38] the packed lamp lights
        vsg::ref_ptr<vsg::vec4Array> uniform_;
    };
}
