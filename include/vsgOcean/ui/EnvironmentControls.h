#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

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
        inline static constexpr std::array<Binding, 13> bindings{{
            {"mouse left drag", "Translate the camera"},
            {"mouse right drag", "Orbit the view (stable horizon)"},
            {"mouse wheel", "Dolly in / out"},
            {"H", "Help window on / off"},
            {"C", "Camera previews on / off"},
            {"S", "Sky widget on / off"},
            {"O", "Ocean surface widget on / off"},
            {"U", "Underwater widget on / off"},
            {"L", "Lights widget on / off"},
            {"R", "Rendering view: photo, annotation, 50% overlay"},
            {"E", "Export the environment (the YAML target is picked in the help window)"},
            {"M", "Model tracking: cycle the followed model"},
            {"Esc", "Quit"},
        }};

        explicit EnvironmentControls(const vsg::ref_ptr<Scene>& scene);

        /// The controlled scene.
        const vsg::ref_ptr<Scene>& scene() const { return scene_; }

        /// Whether the help window is visible (the H key: the basic bindings and the
        /// common controls - model tracking). Hidden by default: the bindings stay
        /// visible in the HUD.
        bool helpVisible = false;

        /// Whether the camera preview widget is visible (the C key). Hidden by default;
        /// with no camera rendering it only says so.
        bool camerasVisible = false;

        /// Whether the sky widget is visible (the S key): sun, day cycle, fog, clouds.
        bool skyVisible = false;

        /// Whether the ocean surface widget is visible (the O key): wind, waves.
        bool surfaceVisible = false;

        /// Whether the underwater widget is visible (the U key): water optics,
        /// current, marine snow. Hidden by default.
        bool underwaterVisible = false;

        /// Whether the lights widget is visible (the L key): one intensity slider
        /// per scene lamp (the SDF <light> intensity is each lamp's ceiling).
        /// Hidden by default.
        bool lightsVisible = false;

        /// The semantic annotation view (the R key, cycles): 0 the photo alone,
        /// 1 the annotation as the full view, 2 the annotation at 50% over the
        /// photo. The semantic pass renders only when this is not 0 (see the
        /// example's SemanticPass / SemanticOverlay wiring).
        int semanticMode = 0;

        /// The file the E key exports the environment to (the environment-related
        /// state only - sky and weather, wind, water optics, waves; never the
        /// lights). Reload it with the example's --env flag.
        vsg::Path environmentExportPath = "vsgOcean-environment.yaml";

        /// Whether a GUI currently captures the input (set by the environment GUI each
        /// frame from the ImGui state). The keyboard controls ignore keys while it is
        /// true, so typing / adjusting in the panel does not move the environment.
        bool guiCapturesInput = false;

        /// Whether the host application should keep animating the sun through the day.
        /// Set to false whenever the sun pose is adjusted through the GUI widgets.
        bool sunCycleEnabled = true;

        void apply(vsg::KeyPressEvent& keyPress) override;

    protected:
        vsg::ref_ptr<Scene> scene_;
    };
}
