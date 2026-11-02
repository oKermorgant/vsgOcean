#pragma once

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

#include <vsgOcean/core/Export.h>

#include <vsg/all.h>

#include <functional>

namespace vsgOcean
{
    /// A camera driven by an external (programmatic) view - e.g. a robotics
    /// frame or a presentation script - that the user can still look around
    /// in:
    ///
    /// - setProgrammaticView() takes over whenever the driven view actually
    ///   moves (a new target, a smooth transition), resetting the manual
    ///   offset; a view that is merely re-published unchanged (the usual
    ///   constant TF stream) leaves the manual offset in place
    /// - drag (any button) yaws / pitches the view about the eye; the wheel
    ///   dollies along the view direction
    /// - Space resets to the programmatic view, undoing every manual change
    ///
    /// While a programmatic view is active the free camera controllers
    /// (e.g. HorizonController) should defer to this one, typically through
    /// their inputBlocked hook.
    class VSG_OCEAN_EXPORT CameraFrame : public vsg::Inherit<vsg::Visitor, CameraFrame>
    {
    public:
        /// drag sensitivity: dragging across the full window width yaws half a turn
        double rotateScale = vsg::PI;

        /// wheel dolly factor per notch
        double dollyScale = 1.1;

        /// pitch limit of the manual look-around, in radians from the
        /// programmatic view direction
        double pitchLimit = 1.3;

        /// consulted on mouse / wheel / key input; the frame ignores events
        /// while it returns true (e.g. while a GUI captures the mouse)
        std::function<bool()> inputBlocked;

        explicit CameraFrame(vsg::ref_ptr<vsg::Camera> camera);

        /// whether a programmatic view is currently driving the camera
        bool active() const { return static_cast<bool>(lookAt_); }

        /// the externally driven view; a real move resets the manual offset
        void setProgrammaticView(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up);

        /// manual look-around (radians): yaw about the programmatic up,
        /// pitch clamped to +/- pitchLimit; dolly factor 1 = unchanged
        void addLookAround(double yaw, double pitch);
        void addDolly(double factor);

        /// back to the programmatic view (also bound to the Space key)
        void reset();

        // event handling (visited by vsg::Viewer::handleEvents)
        void apply(vsg::ButtonPressEvent& event) override;
        void apply(vsg::ButtonReleaseEvent& event) override;
        void apply(vsg::MoveEvent& event) override;
        void apply(vsg::ScrollWheelEvent& event) override;
        void apply(vsg::KeyPressEvent& event) override;

    protected:
        void applyView();

        vsg::ref_ptr<vsg::LookAt> lookAt_;

        // the programmatic view and the manual offsets on top of it
        vsg::dvec3 eye_, up_;
        vsg::dvec3 direction_; // programmatic view direction (center - eye, normalized)
        double distance_;
        double yaw_ = 0.0, pitch_ = 0.0, dolly_ = 1.0;
        bool hasView_ = false;

        // drag state
        vsg::ButtonMask dragButton_ = vsg::BUTTON_MASK_OFF;
        vsg::ivec2 lastPosition_;
    };
}
