#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/Scene.h>

#include <array>
#include <string_view>
#include <utility>

namespace vsgOcean
{
    /// Runtime control of the environment through the keyboard: sun pose, sea level,
    /// water type, waves, wind and the ocean surface itself. A library feature - the
    /// same bindings work in any application, with or without the GUI panel
    /// (see vsgOcean::createEnvironmentGui).
    ///
    /// Add to the viewer with viewer->addEventHandler(EnvironmentControls::create(scene)).
    class VSG_OCEAN_EXPORT EnvironmentControls : public vsg::Inherit<vsg::Visitor, EnvironmentControls>
    {
    public:
        /// key / action pairs of the bindings, rendered by the GUI panel
        using Binding = std::pair<std::string_view, std::string_view>;
        inline static constexpr std::array<Binding, 16> bindings{{
            {"mouse left drag", "orbit the view (stable horizon)"},
            {"mouse right drag", "translate the camera"},
            {"mouse wheel", "dolly in / out"},
            {"Up / Down", "sun elevation"},
            {"Left / Right", "sun azimuth"},
            {"Page Up / Down", "sea level"},
            {"A / D", "wave amplitude"},
            {"Q / E", "wave period"},
            {"W / S", "wind speed"},
            {"Z / C", "Jerlov water type"},
            {"P", "ocean surface on / off"},
            {"Space", "day cycle animation on / off"},
            {"Tab", "cycle the tracked model"},
            {"V", "camera previews on / off"},
            {"H", "sliders panel on / off"},
            {"Esc", "quit"},
        }};

        explicit EnvironmentControls(const vsg::ref_ptr<Scene>& scene);

        /// The controlled scene.
        const vsg::ref_ptr<Scene>& scene() const { return scene_; }

        /// Whether the help / tuning panel is visible (toggled with the H key).
        /// Hidden by default: the bindings stay visible in the HUD, the sliders come
        /// with the panel.
        bool panelVisible = false;

        /// Whether the camera preview widget is visible (toggled with the V key).
        /// Independent of the tuning panel; shown by default.
        bool camerasVisible = true;

        /// Whether a GUI currently captures the input (set by the environment GUI each
        /// frame from the ImGui state). The keyboard controls ignore keys while it is
        /// true, so typing / adjusting in the panel does not move the environment.
        bool guiCapturesInput = false;

        /// Whether the host application should keep animating the sun through the day.
        /// Set to false whenever the sun pose is adjusted through the keys or the GUI.
        bool sunCycleEnabled = true;

        void apply(vsg::KeyPressEvent& keyPress) override;

    protected:
        vsg::ref_ptr<Scene> scene_;
    };
}
