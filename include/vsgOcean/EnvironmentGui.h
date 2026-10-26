#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>
#include <vsgOcean/EnvironmentControls.h>
#include <vsgOcean/Hud.h>

namespace vsg
{
    class Node;
    class Viewer;
    class Window;
}

namespace vsgOcean
{
    /// The environment tuning panel (ImGui): the key bindings, and sliders for the sun,
    /// water, waves and fog. Rendered on top of the scene, next to the underwater
    /// composite; the panel visibility follows EnvironmentControls::panelVisible (H key).
    ///
    /// Requires vsgOcean to be built with GUI support (VSGOCEAN_WITH_GUI, on when vsgImGui
    /// is found); returns a null node otherwise, with a warning - the key bindings of
    /// EnvironmentControls work in all cases.
    [[nodiscard]] VSG_OCEAN_EXPORT vsg::ref_ptr<vsg::Node> createEnvironmentGui(vsg::ref_ptr<vsg::Window> window,
                                                                  vsg::ref_ptr<vsg::Viewer> viewer,
                                                                  const vsg::ref_ptr<EnvironmentControls>& controls);

    /// The runtime help HUD: the environment key bindings rendered in a corner of the
    /// window, always available - including the key that brings up the sliders panel.
    /// Needs a loadable font (see Hud); returns a null node when there is none.
    [[nodiscard]] VSG_OCEAN_EXPORT vsg::ref_ptr<Hud> createEnvironmentHud(const vsg::ref_ptr<EnvironmentControls>& controls,
                                                                         vsg::ref_ptr<vsg::Options> fontOptions,
                                                                         VkExtent2D extent);
}
