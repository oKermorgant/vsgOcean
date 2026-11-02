/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/RenderTarget.h>

namespace vsgOcean
{
    namespace
    {
        // shared construction of the attachments, render pass and framebuffer
        // (an existing render pass is kept: it depends on the formats, not on the size)
        void build(const vsg::ref_ptr<vsg::Device>& device, VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat,
                   vsg::ref_ptr<vsg::ImageView>& colorView, vsg::ref_ptr<vsg::ImageView>& depthView,
                   vsg::ref_ptr<vsg::RenderPass>& renderPass, vsg::ref_ptr<vsg::Framebuffer>& framebuffer)
        {
            // offscreen color+depth attachments; both end the render pass in SHADER_READ_ONLY
            // layout, ready for the composite to sample
            auto color = vsg::Image::create();
            color->imageType = VK_IMAGE_TYPE_2D;
            color->format = colorFormat;
            color->extent = VkExtent3D{extent.width, extent.height, 1};
            color->mipLevels = 1;
            color->arrayLayers = 1;
            color->samples = VK_SAMPLE_COUNT_1_BIT;
            color->tiling = VK_IMAGE_TILING_OPTIMAL;
            color->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            color->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color->compile(device.get());
            color->allocateAndBindMemory(device.get());

            auto depth = vsg::Image::create();
            depth->imageType = VK_IMAGE_TYPE_2D;
            depth->format = depthFormat;
            depth->extent = VkExtent3D{extent.width, extent.height, 1};
            depth->mipLevels = 1;
            depth->arrayLayers = 1;
            depth->samples = VK_SAMPLE_COUNT_1_BIT;
            depth->tiling = VK_IMAGE_TILING_OPTIMAL;
            depth->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            depth->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depth->compile(device.get());
            depth->allocateAndBindMemory(device.get());

            colorView = vsg::ImageView::create(color, VK_IMAGE_ASPECT_COLOR_BIT);
            colorView->compile(device.get());
            depthView = vsg::ImageView::create(depth, VK_IMAGE_ASPECT_DEPTH_BIT);
            depthView->compile(device.get());

            if (!renderPass)
            {
                auto colorAttachment = vsg::defaultColorAttachment(colorFormat);
                colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                auto depthAttachment = vsg::defaultDepthAttachment(depthFormat);
                depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // sampled by the composite
                depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                vsg::RenderPass::Attachments attachments{colorAttachment, depthAttachment};
                vsg::SubpassDescription subpass{};
                subpass.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
                subpass.depthStencilAttachments = {{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
                renderPass = vsg::RenderPass::create(device.get(), attachments, vsg::RenderPass::Subpasses{subpass}, vsg::RenderPass::Dependencies{});
            }

            framebuffer = vsg::Framebuffer::create(renderPass, vsg::ImageViews{colorView, depthView}, extent.width, extent.height, 1);
        }
    }

    RenderTarget::RenderTarget(vsg::ref_ptr<vsg::Window> window) :
        window_(std::move(window))
    {
        if (window_->framebufferSamples() != VK_SAMPLE_COUNT_1_BIT)
        {
            vsg::warn("vsgOcean::RenderTarget - the composite requires a single-sample window (no MSAA)");
        }

        extent_ = window_->extent2D();
        colorFormat_ = window_->surfaceFormat().format;
        depthFormat_ = window_->depthFormat();
        build(window_->getOrCreateDevice(), extent_, colorFormat_, depthFormat_,
              colorView_, depthView_, renderPass_, framebuffer_);
    }

    RenderTarget::RenderTarget(vsg::ref_ptr<vsg::Device> device, VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat) :
        extent_(extent),
        colorFormat_(colorFormat),
        depthFormat_(depthFormat)
    {
        build(std::move(device), extent, colorFormat, depthFormat, colorView_, depthView_, renderPass_, framebuffer_);
    }

}
