/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/RenderTarget.h>

namespace vsgOcean
{
    RenderTarget::RenderTarget(vsg::ref_ptr<vsg::Window> window) :
        window_(std::move(window))
    {
        auto device = window_->getOrCreateDevice();
        extent_ = window_->extent2D();
        if (window_->framebufferSamples() != VK_SAMPLE_COUNT_1_BIT)
        {
            vsg::warn("vsgOcean::RenderTarget - the underwater composite requires a single-sample window (no MSAA)");
        }

        // offscreen color+depth attachments; both end the render pass in SHADER_READ_ONLY
        // layout, ready for the composite to sample
        auto color = vsg::Image::create();
        color->imageType = VK_IMAGE_TYPE_2D;
        color->format = window_->surfaceFormat().format;
        color->extent = VkExtent3D{extent_.width, extent_.height, 1};
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
        depth->format = window_->depthFormat();
        depth->extent = VkExtent3D{extent_.width, extent_.height, 1};
        depth->mipLevels = 1;
        depth->arrayLayers = 1;
        depth->samples = VK_SAMPLE_COUNT_1_BIT;
        depth->tiling = VK_IMAGE_TILING_OPTIMAL;
        depth->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        depth->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth->compile(device.get());
        depth->allocateAndBindMemory(device.get());

        colorView_ = vsg::ImageView::create(color, VK_IMAGE_ASPECT_COLOR_BIT);
        colorView_->compile(device.get());
        depthView_ = vsg::ImageView::create(depth, VK_IMAGE_ASPECT_DEPTH_BIT);
        depthView_->compile(device.get());

        auto colorAttachment = vsg::defaultColorAttachment(color->format);
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        auto depthAttachment = vsg::defaultDepthAttachment(depth->format);
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // sampled by the composite
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        vsg::RenderPass::Attachments attachments{colorAttachment, depthAttachment};
        vsg::SubpassDescription subpass{};
        subpass.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        subpass.depthStencilAttachments = {{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
        renderPass_ = vsg::RenderPass::create(device.get(), attachments, vsg::RenderPass::Subpasses{subpass}, vsg::RenderPass::Dependencies{});

        framebuffer_ = vsg::Framebuffer::create(renderPass_, vsg::ImageViews{colorView_, depthView_}, extent_.width, extent_.height, 1);
    }
}
