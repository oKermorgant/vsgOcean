#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>

#include <vsg/all.h>

namespace vsgOcean
{
    /// Camera controller with a stable horizon, intended as the default for vsgOcean scenes:
    ///
    /// - left drag orbits around the view centre - yaw about the world up axis (+Z) and
    ///   pitch about the horizontal axis - so the view never rolls and the horizon stays
    ///   level, FPS-like
    /// - right drag translates the view (pan in the camera plane, scaled by the distance
    ///   to the view centre)
    /// - the wheel dollies towards / away from the view centre
    class VSG_OCEAN_EXPORT HorizonController : public vsg::Inherit<vsg::Visitor, HorizonController>
    {
    public:
        /// elevation limit of the orbit in radians; keeps the camera from flipping over the poles
        double elevationLimit = 1.5;

        /// pan speed: fraction of the distance to the view centre per full window drag
        double panScale = 1.0;

        /// dolly speed: zoom factor per wheel notch
        double dollyScale = 1.1;

        explicit HorizonController(vsg::ref_ptr<vsg::Camera> camera);

        void apply(vsg::ButtonPressEvent& event) override;
        void apply(vsg::ButtonReleaseEvent& event) override;
        void apply(vsg::MoveEvent& event) override;
        void apply(vsg::ScrollWheelEvent& event) override;

    protected:
        void orbit(double yaw, double pitch);
        void translate(double dx, double dy);

        vsg::ref_ptr<vsg::Camera> camera_;
        vsg::ref_ptr<vsg::LookAt> lookAt_;
        vsg::ButtonMask dragButton_ = vsg::BUTTON_MASK_OFF;
        vsg::ivec2 lastPosition_{0, 0};
    };
}
