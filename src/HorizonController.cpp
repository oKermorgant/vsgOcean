/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/HorizonController.h>

#include <algorithm>
#include <cmath>

namespace vsgOcean
{
    HorizonController::HorizonController(vsg::ref_ptr<vsg::Camera> camera) :
        camera_(camera)
    {
        if (camera_) lookAt_ = camera_->viewMatrix.cast<vsg::LookAt>();
        if (lookAt_) lookAt_->up = vsg::dvec3(0.0, 0.0, 1.0);
    }

    void HorizonController::apply(vsg::ButtonPressEvent& event)
    {
        // left drag orbits, right drag translates
        if (event.mask & (vsg::BUTTON_MASK_1 | vsg::BUTTON_MASK_2))
        {
            dragButton_ = event.mask;
            lastPosition_ = vsg::ivec2(event.x, event.y);
        }
    }

    void HorizonController::apply(vsg::ButtonReleaseEvent& /*event*/)
    {
        dragButton_ = vsg::BUTTON_MASK_OFF;
    }

    void HorizonController::apply(vsg::MoveEvent& event)
    {
        if (dragButton_ == vsg::BUTTON_MASK_OFF || !lookAt_) return;

        const auto window = event.window.get();
        if (!window) return;

        const auto extent = window->extent2D();
        const vsg::ivec2 position(event.x, event.y);
        const auto delta = position - lastPosition_;
        lastPosition_ = position;

        if (dragButton_ & vsg::BUTTON_MASK_2)
        {
            // right drag: translate, scaled by the distance to the view centre
            const double distance = vsg::length(lookAt_->eye - lookAt_->center);
            const double scale = panScale * distance / static_cast<double>(extent.width);
            translate(-delta.x * scale, delta.y * scale);
        }
        else
        {
            // left drag: dragging across the full window width orbits half a turn
            const double scale = vsg::PI / static_cast<double>(extent.width);
            orbit(-static_cast<double>(delta.x) * scale, static_cast<double>(delta.y) * scale);
        }
    }

    void HorizonController::apply(vsg::ScrollWheelEvent& event)
    {
        if (!lookAt_) return;

        // scroll up moves towards the view centre, scroll down away from it
        const auto offset = lookAt_->eye - lookAt_->center;
        lookAt_->eye = lookAt_->center + offset * std::pow(dollyScale, -static_cast<double>(event.delta.y));
    }

    void HorizonController::orbit(double yaw, double pitch)
    {
        const auto offset = lookAt_->eye - lookAt_->center;
        const double radius = vsg::length(offset);
        if (radius == 0.0) return;

        // spherical coordinates of the eye around the view centre
        const double azimuth = std::atan2(offset.y, offset.x) + yaw;
        double elevation = std::asin(std::clamp(offset.z / radius, -1.0, 1.0)) + pitch;
        elevation = std::clamp(elevation, -elevationLimit, elevationLimit);

        lookAt_->eye = lookAt_->center + radius * vsg::dvec3(std::cos(elevation) * std::cos(azimuth),
                                                              std::cos(elevation) * std::sin(azimuth),
                                                              std::sin(elevation));
        lookAt_->up = vsg::dvec3(0.0, 0.0, 1.0);
    }

    void HorizonController::translate(double dx, double dy)
    {
        const auto view = vsg::normalize(lookAt_->center - lookAt_->eye);
        const auto horizontalRight = vsg::normalize(vsg::cross(view, vsg::dvec3(0.0, 0.0, 1.0)));
        const auto inPlaneUp = vsg::cross(horizontalRight, view);

        // move the view centre and eye together, in the camera plane
        const auto move = dx * horizontalRight + dy * inPlaneUp;
        lookAt_->center += move;
        lookAt_->eye += move;
    }
}
