#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Environment.h>

namespace vsgOcean
{
    /// Underwater rendering: a fullscreen pass blended over the scene when the camera is
    /// below the water surface, adding the Jerlov in-scatter towards the water colour.
    /// Drawn last, after the ocean surface; a no-op (alpha 0) when the camera is above water.
    ///
    /// Created and updated by Scene together with the sky and the ocean surface; tune the
    /// water type through the Environment Jerlov type.
    class VSG_OCEAN_EXPORT Underwater : public vsg::Inherit<vsg::Group, Underwater>
    {
    public:
        Underwater();

        /// Refresh the pass from the environment (Jerlov water colour, submersion depth,
        /// sun height). Call every frame, before record.
        void update(const Environment& environment);

    protected:
        // uniform block layout: [0] water color + mean attenuation, [1] depth + sun height
        vsg::ref_ptr<vsg::vec4Array> uniform_;
    };
}
