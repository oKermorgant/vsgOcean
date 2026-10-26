/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/RenderPipeline.h>

namespace vsgOcean
{
    void RenderPipeline::buildScenePass(const vsg::ref_ptr<Scene>& scene, const vsg::ref_ptr<vsg::Camera>& camera, const vsg::vec4& clearColor)
    {
        // the scene pass: sky, lighting, models and ocean surface into the offscreen target
        auto view = vsg::View::create(camera);
        view->addChild(scene);

        sceneRenderGraph_ = vsg::RenderGraph::create();
        sceneRenderGraph_->renderPass = target_->renderPass();
        sceneRenderGraph_->framebuffer = target_->framebuffer();
        sceneRenderGraph_->renderArea = VkRect2D{{0, 0}, target_->extent()};
        sceneRenderGraph_->viewportState = vsg::ViewportState::create(0, 0, target_->extent().width, target_->extent().height);
        sceneRenderGraph_->clearValues.resize(2);
        sceneRenderGraph_->clearValues[0].color = clearColor;
        sceneRenderGraph_->clearValues[1].depthStencil = {0.0f, 0}; // reversed depth: 0 is far
        sceneRenderGraph_->windowResizeHandler = {};                 // the target has a fixed size
        sceneRenderGraph_->addChild(view);

        // the composite of this pipeline, sampling the target; wrapped in a view, so it
        // compiles reliably alongside other graph content
        composite_ = Underwater::create(target_->colorView(), target_->depthView());
        compositeView_ = vsg::View::create(camera);
        compositeView_->addChild(composite_);
    }

    RenderPipeline::RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Camera> camera) :
        target_(RenderTarget::create(std::move(window)))
    {
        buildScenePass(scene, std::move(camera), vsg::vec4(0.2f, 0.2f, 0.4f, 1.0f));
    }

    RenderPipeline::RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Device> device, vsg::ref_ptr<vsg::Camera> camera,
                                   VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat) :
        target_(RenderTarget::create(std::move(device), extent, colorFormat, depthFormat))
    {
        buildScenePass(scene, camera, vsg::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        // the composite pass renders into its own sampled output image, e.g. displayed
        // in a GUI or read back to a ROS image topic
        auto output = vsg::Image::create();
        output->imageType = VK_IMAGE_TYPE_2D;
        output->format = colorFormat;
        output->extent = VkExtent3D{extent.width, extent.height, 1};
        output->mipLevels = 1;
        output->arrayLayers = 1;
        output->samples = VK_SAMPLE_COUNT_1_BIT;
        output->tiling = VK_IMAGE_TILING_OPTIMAL;
        output->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        output->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        output->compile(target_->renderPass()->device.get());
        output->allocateAndBindMemory(target_->renderPass()->device.get());
        outputView_ = vsg::ImageView::create(output, VK_IMAGE_ASPECT_COLOR_BIT);
        outputView_->compile(target_->renderPass()->device.get());

        auto outputAttachment = vsg::defaultColorAttachment(colorFormat);
        outputAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        outputAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        vsg::RenderPass::Attachments attachments{outputAttachment};
        vsg::SubpassDescription subpass{};
        subpass.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        auto renderPass = vsg::RenderPass::create(target_->renderPass()->device.get(), attachments, vsg::RenderPass::Subpasses{subpass}, vsg::RenderPass::Dependencies{});
        auto framebuffer = vsg::Framebuffer::create(renderPass, vsg::ImageViews{outputView_}, extent.width, extent.height, 1);

        sensorRenderGraph_ = vsg::RenderGraph::create();
        sensorRenderGraph_->renderPass = renderPass;
        sensorRenderGraph_->framebuffer = framebuffer;
        sensorRenderGraph_->renderArea = VkRect2D{{0, 0}, extent};
        sensorRenderGraph_->viewportState = vsg::ViewportState::create(0, 0, extent.width, extent.height);
        sensorRenderGraph_->clearValues.resize(1);
        sensorRenderGraph_->clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        sensorRenderGraph_->windowResizeHandler = {};
        sensorRenderGraph_->addChild(compositeView_);
    }

    void RenderPipeline::update(const vsg::ref_ptr<Scene>& scene, const vsg::Camera* camera, double t)
    {
        // this camera's submersion below the surface: each pipeline tracks its own camera
        double submersion = 0.0;
        if (camera && scene && scene->surfaceEnabled())
        {
            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                const auto wavefield = scene->surface() ? scene->surface()->wavefield() : nullptr;
                const double waveHeight = wavefield ? scene->environment()->seaLevel() + wavefield->height(lookAt->eye.x, lookAt->eye.y, t)
                                                     : scene->environment()->seaLevel();
                submersion = waveHeight - lookAt->eye.z; // positive below the surface
            }
        }

        composite_->update(*scene->environment(), camera, submersion);
    }
}
