#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>
#include <vsgOcean/Environment.h>
#include <vsgOcean/Wavefield.h>

namespace vsgOcean
{
    /// The ocean surface: a grid mesh displaced by the same Gerstner component waves as its
    /// Wavefield (the Gazebo wave model), so that the rendered waves and the waves evaluated
    /// on the CPU for dynamics are the same. The shading uses the Environment (sun, fog,
    /// Jerlov water colour).
    ///
    /// The surface owns its Wavefield; tune it through wavefield() and call update() every
    /// frame with the current Environment, Camera and simulation time.
    class VSG_OCEAN_EXPORT OceanSurface : public vsg::Inherit<vsg::Group, OceanSurface>
    {
    public:
        /// size: extent of the surface grid in metres (centred at the origin),
        /// cells: number of grid cells per side
        explicit OceanSurface(double size = 1200.0, uint32_t cells = 256);

        /// The wave field of the surface (the Gazebo wave model), tunable at any time.
        vsg::ref_ptr<Wavefield> wavefield() const { return wavefield_; }

        /// Refresh the surface from the environment, camera and simulation time t in seconds.
        /// The wave field follows the environment wind direction. Call every frame, before record.
        void update(const Environment& environment, const vsg::Camera* camera, double t);

    protected:
        vsg::ref_ptr<Wavefield> wavefield_;

        // uniform block layout: [0] sun direction, [1] fog color + density, [2] water color,
        // [3] params (t, tau, number, unused), [4] camera eye, [5..44] component wave arrays
        vsg::ref_ptr<vsg::vec4Array> uniform_;
    };
}
