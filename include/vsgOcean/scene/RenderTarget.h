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
    /// The offscreen color+depth target the scene renders into, drawn to the window by
    /// the underwater composite (see vsgOcean::createCommandGraph). Both attachments
    /// end the scene render pass in SHADER_READ_ONLY layout, ready to be sampled.
    ///
    /// The target is created at the window extent; createCommandGraph watches the
    /// window size and rebuilds it when it has been stable for a moment (so an
    /// interactive resize is not interrupted by rebuilds).
    class VSG_OCEAN_EXPORT RenderTarget : public vsg::Inherit<vsg::Object, RenderTarget>
    {
    public:
        /// offscreen color+depth target at the window extent, with the window formats
        explicit RenderTarget(vsg::ref_ptr<vsg::Window> window);

        /// offscreen color+depth target with explicit formats (e.g. a sensor camera)
        RenderTarget(vsg::ref_ptr<vsg::Device> device, VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat);

        const vsg::ref_ptr<vsg::RenderPass>& renderPass() const { return renderPass_; }
        const vsg::ref_ptr<vsg::Framebuffer>& framebuffer() const { return framebuffer_; }
        const vsg::ref_ptr<vsg::ImageView>& colorView() const { return colorView_; }
        const vsg::ref_ptr<vsg::ImageView>& depthView() const { return depthView_; }
        [[nodiscard]] VkExtent2D extent() const { return extent_; }

    protected:
        vsg::ref_ptr<vsg::Window> window_;
        VkExtent2D extent_{};
        vsg::ref_ptr<vsg::RenderPass> renderPass_;
        vsg::ref_ptr<vsg::Framebuffer> framebuffer_;
        vsg::ref_ptr<vsg::ImageView> colorView_;
        vsg::ref_ptr<vsg::ImageView> depthView_;
    };
}
