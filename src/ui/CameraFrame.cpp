/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

</editor-fold> */

#include <vsgOcean/ui/CameraFrame.h>

#include <algorithm>

namespace
{
    // views re-published without a real move jitter in the last digits: only a
    // move beyond this resets the manual offset
    constexpr double moveEpsilon{1e-4};
}

namespace vsgOcean
{
    CameraFrame::CameraFrame(vsg::ref_ptr<vsg::Camera> camera)
    {
        if (camera && camera->viewMatrix) lookAt_ = camera->viewMatrix.cast<vsg::LookAt>();
    }

    void CameraFrame::setProgrammaticView(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        if (!lookAt_) return;

        const vsg::dvec3 direction{vsg::normalize(center - eye)};
        const double distance{vsg::length(center - eye)};

        const bool moved{!hasView_ || vsg::length(eye - eye_) > moveEpsilon
                             || vsg::length(direction - direction_) > moveEpsilon};

        eye_ = eye;
        up_ = up;
        direction_ = direction;
        distance_ = distance;
        hasView_ = true;

        // a real move of the driven view takes over: back to it, offsets cleared
        if (moved) reset();
    }

    void CameraFrame::addLookAround(double yaw, double pitch)
    {
        if (!lookAt_) return;
        yaw_ += yaw;
        pitch_ = std::clamp(pitch_ + pitch, -pitchLimit, pitchLimit);
        applyView();
    }

    void CameraFrame::addDolly(double factor)
    {
        if (!lookAt_) return;
        dolly_ = std::clamp(dolly_ * factor, 0.1, 10.0);
        applyView();
    }

    void CameraFrame::reset()
    {
        yaw_ = 0.0;
        pitch_ = 0.0;
        dolly_ = 1.0;
        applyView();
    }

    void CameraFrame::applyView()
    {
        if (!lookAt_ || !hasView_) return;

        // the manual rotation composes on the programmatic direction: yaw about
        // the programmatic up, then pitch about the (yawed) right axis
        const auto up{vsg::normalize(up_)};
        auto direction{vsg::normalize(direction_)};
        if (std::abs(yaw_) > 0.0)
        {
            const auto rotation{vsg::rotate(yaw_, up)};
            direction = rotation * direction;
        }
        if (std::abs(pitch_) > 0.0)
        {
            auto right{vsg::normalize(vsg::cross(direction, up))};
            const auto rotation{vsg::rotate(pitch_, right)};
            direction = rotation * direction;
        }

        // dolly moves the eye along the view direction (center kept at the
        // programmatic distance along the new direction)
        const auto distance{dolly_ * distance_};
        lookAt_->eye = eye_ - (dolly_ - 1.0) * distance_ * direction;
        lookAt_->center = lookAt_->eye + distance * direction;
        lookAt_->up = up;
    }

    void CameraFrame::apply(vsg::ButtonPressEvent& event)
    {
        if (!lookAt_ || (inputBlocked && inputBlocked())) return;
        dragButton_ = event.mask;
        lastPosition_ = vsg::ivec2(event.x, event.y);
    }

    void CameraFrame::apply(vsg::ButtonReleaseEvent& /*event*/)
    {
        dragButton_ = vsg::BUTTON_MASK_OFF;
    }

    void CameraFrame::apply(vsg::MoveEvent& event)
    {
        if (!lookAt_ || dragButton_ == vsg::BUTTON_MASK_OFF) return;
        if (inputBlocked && inputBlocked()) return;

        const auto window = event.window.get();
        if (!window) return;

        const auto extent = window->extent2D();
        const vsg::ivec2 position(event.x, event.y);
        const auto delta = position - lastPosition_;
        lastPosition_ = position;

        // dragging across the full window width yaws half a turn
        const auto scale = rotateScale / static_cast<double>(extent.width);
        addLookAround(-static_cast<double>(delta.x) * scale, -static_cast<double>(delta.y) * scale);
    }

    void CameraFrame::apply(vsg::ScrollWheelEvent& event)
    {
        if (!lookAt_ || (inputBlocked && inputBlocked())) return;
        addDolly(std::pow(dollyScale, -static_cast<double>(event.delta.y)));
    }

    void CameraFrame::apply(vsg::KeyPressEvent& event)
    {
        if (!lookAt_ || (inputBlocked && inputBlocked())) return;
        if (event.keyBase == vsg::KEY_Space) reset();
    }
}
