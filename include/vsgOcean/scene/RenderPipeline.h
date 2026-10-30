#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/PresentPass.h>
#include <vsgOcean/scene/RenderTarget.h>
#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/scene/Underwater.h>

namespace vsgOcean
{
    /// Renders the scene from one camera in two stages: the scene pass draws the
    /// sky, lighting, models and ocean surface into an offscreen color+depth target,
    /// which the underwater composite then draws into the pipeline's composited
    /// image - a sampled, transfer-capable image that is both the pipeline's output
    /// (sensor cameras, screenshots) and what the present pass draws to the window
    /// (main view), under the HUD and GUI overlays.
    ///
    /// A pipeline is a unit of multiple-camera rendering: add sceneRenderGraph() and
    /// compositeRenderGraph() to the command graph; the main pipeline additionally
    /// draws its presentNode() in a window render graph. Call update() every frame to
    /// refresh the composite for this camera.
    class VSG_OCEAN_EXPORT RenderPipeline : public vsg::Inherit<vsg::Object, RenderPipeline>
    {
    public:
        /// main view pipeline: renders at the window extent, with the window formats
        RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Camera> camera);

        /// sensor pipeline: renders at an explicit extent and formats
        RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Device> device, vsg::ref_ptr<vsg::Camera> camera,
                       VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat);

        /// The scene pass, rendering into the offscreen target: add to the command graph.
        [[nodiscard]] vsg::ref_ptr<vsg::RenderGraph> sceneRenderGraph() const { return sceneRenderGraph_; }

        /// The composite pass, rendering into the pipeline's composited image:
        /// add to the command graph (for every pipeline).
        [[nodiscard]] vsg::ref_ptr<vsg::RenderGraph> compositeRenderGraph() const { return compositeRenderGraph_; }

        /// The present pass drawing the composited image to the window: add to the
        /// window render graph (main pipelines only).
        [[nodiscard]] vsg::ref_ptr<PresentPass> presentNode() const { return presentPass_; }

        /// The composite pass of this pipeline (for per-frame refresh).
        [[nodiscard]] vsg::ref_ptr<Underwater> composite() const { return composite_; }

        /// The composited image view: the pipeline's output, e.g. for GUI display,
        /// screenshots or later image readback.
        [[nodiscard]] vsg::ref_ptr<vsg::ImageView> outputView() const { return outputView_; }

        /// The camera of this pipeline (e.g. to drive its view matrix).
        [[nodiscard]] vsg::ref_ptr<vsg::Camera> camera() const { return camera_; }

        /// The offscreen target the scene pass renders into.
        [[nodiscard]] const vsg::ref_ptr<RenderTarget>& target() const { return target_; }

        /// Rebuild the render target and the composited image at a new extent (window
        /// resize). After calling, recompile the composite and present passes with
        /// the viewer's vsg::CompileManager so their descriptors rebind.
        void resize(VkExtent2D extent);

        /// Write the composited image to a PNG file. Runs a one-off transfer on the
        /// graphics queue, so call it outside of frame rendering (e.g. after the last
        /// present). False when the capture or the write failed.
        [[nodiscard]] bool writeScreenshot(const vsg::Path& filename) const;

        /// Refresh the composite from the scene (submersion of this camera, wave state)
        /// and the environment. Call every frame, before record.
        void update(const vsg::ref_ptr<Scene>& scene, const vsg::Camera* camera, double t);

    protected:
        void buildScenePass(const vsg::vec4& clearColor);
        void createOutputImage(VkExtent2D extent);
        void buildCompositePass();

        vsg::ref_ptr<Scene> scene_;
        vsg::ref_ptr<vsg::Device> device_;
        vsg::ref_ptr<vsg::Camera> camera_;
        VkExtent2D extent_{};
        VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

        vsg::ref_ptr<RenderTarget> target_;
        vsg::ref_ptr<RenderTarget> retainedTarget_;
        vsg::ref_ptr<vsg::RenderGraph> sceneRenderGraph_;
        vsg::ref_ptr<vsg::RenderGraph> compositeRenderGraph_;
        vsg::ref_ptr<vsg::Image> outputImage_;
        vsg::ref_ptr<vsg::ImageView> outputView_;
        vsg::ref_ptr<vsg::Image> retainedImage_;
        vsg::ref_ptr<Underwater> composite_;
        vsg::ref_ptr<PresentPass> presentPass_;
    };
}
