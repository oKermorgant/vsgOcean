#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/RenderTarget.h>
#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/scene/Underwater.h>

namespace vsgOcean
{
    /// Renders the scene from one camera: the scene pass draws the sky, lighting,
    /// models and ocean surface into an offscreen color+depth target, which the
    /// underwater composite then draws to the camera's output - the window (the main
    /// view) or a sampled image (a sensor camera, e.g. displayed in a GUI or read
    /// back for a ROS image topic).
    ///
    /// A pipeline is a unit of multiple-camera rendering: add sceneRenderGraph() to
    /// the command graph, then either the composite node to the window composite
    /// render graph (main view) or sensorRenderGraph() as a standalone pass (sensors).
    /// Call update() every frame to refresh the composite for this camera.
    ///
    /// The shared sky, ocean surface and models render correctly from any number of
    /// cameras: their camera state comes through push constants at record time.
    class VSG_OCEAN_EXPORT RenderPipeline : public vsg::Inherit<vsg::Object, RenderPipeline>
    {
    public:
        /// main view pipeline: renders at the window extent, with the window formats
        RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Camera> camera);

        /// sensor pipeline: renders at an explicit extent and formats, composited into
        /// its own sampled output image (see outputView)
        RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Device> device, vsg::ref_ptr<vsg::Camera> camera,
                       VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat);

        /// The scene pass, rendering into the offscreen target: add to the command graph.
        [[nodiscard]] vsg::ref_ptr<vsg::RenderGraph> sceneRenderGraph() const { return sceneRenderGraph_; }

        /// The composite, wrapped in a view: add to the window composite render graph (main pipelines).
        [[nodiscard]] vsg::ref_ptr<vsg::Node> compositeNode() const { return compositeView_; }

        /// The composite as a standalone render graph producing the output image
        /// (sensor pipelines): add to the command graph.
        [[nodiscard]] vsg::ref_ptr<vsg::RenderGraph> sensorRenderGraph() const { return sensorRenderGraph_; }

        /// The final composited color image of a sensor pipeline, e.g. for GUI display
        /// (vsgImGui) or read back to a ROS image topic.
        [[nodiscard]] vsg::ref_ptr<vsg::ImageView> outputView() const { return outputView_; }

        /// The composite pass of this pipeline.
        [[nodiscard]] vsg::ref_ptr<Underwater> composite() const { return composite_; }

        /// The offscreen target the scene pass renders into.
        [[nodiscard]] const vsg::ref_ptr<RenderTarget>& target() const { return target_; }

        /// Refresh the composite from the scene (submersion of this camera, wave state)
        /// and the environment. Call every frame, before record.
        void update(const vsg::ref_ptr<Scene>& scene, const vsg::Camera* camera, double t);

    protected:
        void buildScenePass(const vsg::ref_ptr<Scene>& scene, const vsg::ref_ptr<vsg::Camera>& camera, const vsg::vec4& clearColor);

        vsg::ref_ptr<RenderTarget> target_;
        vsg::ref_ptr<vsg::RenderGraph> sceneRenderGraph_;
        vsg::ref_ptr<Underwater> composite_;
        vsg::ref_ptr<vsg::View> compositeView_;
        vsg::ref_ptr<vsg::RenderGraph> sensorRenderGraph_;
        vsg::ref_ptr<vsg::ImageView> outputView_;
    };
}
