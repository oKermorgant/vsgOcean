#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>

#include <vsg/all.h>

#include <string>
#include <vector>

namespace vsgOcean
{
    /// A minimal screen-space text overlay, rendered in a corner of the window on top
    /// of the scene composite: the runtime help (environment key bindings), later also
    /// live readouts (fps, depth). Owns a small orthographic view of the lines.
    ///
    /// The font is loaded through the provided vsg::Options (vsgXchange / freetype):
    /// first the VSGOCEAN_FONT environment variable, then a list of common system
    /// fonts. When no font can be found the HUD renders nothing and valid() is false.
    ///
    /// Created by vsgOcean::createEnvironmentHud for the environment bindings; add the
    /// HUD to the overlay node passed to vsgOcean::createCommandGraph. The HUD keeps
    /// its creation extent for the whole run (a window resize leaves the text at the
    /// original layout).
    class VSG_OCEAN_EXPORT Hud : public vsg::Inherit<vsg::Group, Hud>
    {
    public:
        Hud(vsg::ref_ptr<vsg::Options> fontOptions, VkExtent2D extent);

        /// Whether a font was found and the HUD renders text.
        [[nodiscard]] bool valid() const { return font_ != nullptr; }

        /// The text lines, rendered from the top-left corner; replaces the text.
        void setLines(std::vector<std::string> lines);

    protected:
        void build();

        vsg::ref_ptr<vsg::Font> font_;
        vsg::ref_ptr<vsg::Options> fontOptions_;
        vsg::ref_ptr<vsg::Camera> camera_;
        vsg::ref_ptr<vsg::View> view_;
        std::vector<std::string> lines_;
        VkExtent2D extent_{};
    };
}
