#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>

#include <vsg/all.h>

namespace vsgOcean
{
    /// Draws a pipeline's composited image to the output (the window), as the last
    /// pass of a main render pipeline; the HUD and GUI overlays are drawn on top of
    /// it, within the same render graph. Created by RenderPipeline.
    class VSG_OCEAN_EXPORT PresentPass : public vsg::Inherit<vsg::Group, PresentPass>
    {
    public:
        /// composite: the pipeline's composited image to present
        explicit PresentPass(vsg::ref_ptr<vsg::ImageView> composite);

        /// Point at a new composited image (after a render target rebuild): swaps the
        /// sampled view and releases the descriptor set, so the next compile
        /// (vsg::CompileManager) rebinds it.
        void setCompositeView(vsg::ref_ptr<vsg::ImageView> composite);

        /// The window extent the composite is presented to; the composite is fitted
        /// into it (aspect preserved, letterboxed). Cheap: only a uniform update, no
        /// Vulkan objects are touched, so this is safe to call on a window resize.
        void setWindowExtent(VkExtent2D extent);

    protected:
        vsg::ref_ptr<vsg::vec4Array> uniform_;
        vsg::ref_ptr<vsg::DescriptorImage> imageDescriptor_;
        vsg::ref_ptr<vsg::DescriptorSet> descriptorSet_;
    };
}
