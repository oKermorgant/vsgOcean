#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>
#include <vsgOcean/Environment.h>

namespace vsgOcean
{
    /// Baseline shader sky, rendered as a fullscreen pass behind the scene (no depth writes).
    ///
    /// The sky is an analytic gradient driven by the Environment: day/night colors follow the
    /// sun elevation, with a sun disc and glow, a dusk tint near the horizon, stars at night,
    /// and haze blending to the fog color. Add the Sky to a vsg::View before the scene content,
    /// and call update() once per frame with the current Environment and Camera.
    class VSG_OCEAN_EXPORT Sky : public vsg::Inherit<vsg::Group, Sky>
    {
    public:
        Sky();

        /// Refresh the sky state from the environment and camera. Call every frame, before record.
        void update(const Environment& environment, const vsg::Camera* camera);

    protected:
        // uniform block: [0] sun direction, [1] fog color + density, [2..5] inverse view projection,
        // [6..37] stars of the night sky (xyz local direction, w brightness), [38] Jerlov water
        // color + mean attenuation, [39] camera submersion depth (underwater mode when > 0)
        vsg::ref_ptr<vsg::vec4Array> environmentUniform_;
    };
}
