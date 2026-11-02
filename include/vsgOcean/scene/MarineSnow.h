#pragma once

/* <editor-fold desc="MIT License">

Copyright (c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>
#include <vsgOcean/core/Environment.h>
#include <vsgOcean/scene/LampLights.h>

#include <vsg/commands/Draw.h>

namespace vsgOcean
{
    /// Marine snow: the drifting organic particles of the water column - small
    /// flakes that follow the water current and reflect the light that reaches
    /// them (the sun surviving to their depth, and the scene's point / spot lamps -
    /// snow sparkles in the ROV's beams at night).
    ///
    /// The particles live in a box wrapped around the camera: the box re-centres
    /// on the eye each frame, so a fixed set of particles covers the view wherever
    /// it goes. Each flake wanders slowly and smoothly even in still water; the
    /// water current (Environment::waterCurrent, the seam for a dynamics-driven
    /// current topic) adds bulk drift. Their number follows the environment's
    /// marine snow density (0 disables the field). The camera state comes through
    /// the push constants at record time, so one snow field renders from any
    /// number of cameras; it fades in with the camera's submersion.
    class VSG_OCEAN_EXPORT MarineSnow : public vsg::Inherit<vsg::Group, MarineSnow>
    {
    public:
        /// particles: the size of the flake pool in the camera-following box - the
        /// density (Environment::marineSnowDensity, 1.0 = the unit field of 4096
        /// flakes, up to 100.0) scales how many of them are visible. box: the edge of
        /// the box, metres. The default pool is 100x the unit field; only the
        /// visible fraction is drawn, so the vertex cost follows the density.
        explicit MarineSnow(uint32_t particles = 100 * 4096, float box = 25.0f);

        /// Refresh the snow from the environment (density, current, sun, water
        /// optics), the simulation time t in seconds, and the scene's point and spot
        /// lights ALREADY COLLECTED for this frame (see Scene::update, which collects
        /// them once per frame and shares them with the composites). Call every
        /// frame, before record.
        void update(const Environment& environment, double t, const std::vector<LampLight>& lamps);

    protected:
        const float box_;
        const uint32_t particles_;

        // the draw command, so the visible flake count can follow the density
        vsg::ref_ptr<vsg::Draw> draw_;

        // uniform block: [0] params (t, enabled 0/1, box), [1] current + sea
        // level, [2] sun direction, [3] water colour + mean attenuation, [4]
        // vertical attenuation, [5] beam attenuation, [6..38] the packed lamps
        vsg::ref_ptr<vsg::vec4Array> uniform_;
    };
}
