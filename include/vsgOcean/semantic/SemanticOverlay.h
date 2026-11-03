#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/semantic/SemanticPass.h>

#include <vsg/app/View.h>

namespace vsgOcean
{
    /// The on-screen semantic annotation view: a fullscreen draw over the
    /// presented photo that paints every model with the palette color of its
    /// name (see SemanticPalette), by looking up the per-pixel model ID from
    /// SemanticPass's image. Presentation only - the ID image itself is never
    /// touched by the colors.
    ///
    /// Modes (EnvironmentControls::semanticMode in the example):
    ///   0 - off: the view is masked out of the record, no per-frame cost at
    ///   1 - opaque: the models on a black background, the annotation as data
    ///   2 - overlay: the models at 50% alpha over the photo, to eyeball the
    ///       annotation alignment against the scene
    ///
    /// A view (not a plain group) so the mode-0 masking is a whole-subgraph
    /// record skip. Add it to the WINDOW render graph after the present pass
    /// and before the HUD / GUI overlays (vsgOcean::createCommandGraph takes
    /// it as its presentOverlay argument and keeps it sized on resize).
    ///
    /// The uniform palette holds the first 64 models of the pass (in ID
    /// order); scenes with more models fall back to the shader's in-place
    /// hash-free golden-angle hue - the display of model 65+ is ad hoc, the
    /// ID data stays exact either way.
    class VSG_OCEAN_EXPORT SemanticOverlay : public vsg::Inherit<vsg::View, SemanticOverlay>
    {
    public:
        /// The overlay of the given pass, drawn at the current window extent.
        SemanticOverlay(const vsg::ref_ptr<SemanticPass>& semantic, VkExtent2D windowExtent);

        /// The display mode: 0 off, 1 opaque, 2 the 50% overlay (see the class
        /// documentation). Off culls the draw at the vertex stage.
        void setMode(int mode);

        /// The window extent the overlay stretches over (a uniform update;
        /// the semantic image keeps its creation extent, so a resized window
        /// stretches it - same policy as the HUD).
        void setWindowExtent(VkExtent2D extent);

    protected:
        vsg::ref_ptr<vsg::vec4Array> uniform_; // [0] params, [1..] the palette
    };
}
