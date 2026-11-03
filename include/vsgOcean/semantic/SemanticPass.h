#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Frame.h>
#include <vsgOcean/scene/Scene.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vsgOcean
{
    /// The semantic annotation pass: the models of the scene rendered with each
    /// pixel carrying the ID of the model it belongs to, read back per pixel -
    /// the ground truth for training-data generation (instance masks, tight
    /// boxes, occlusion-aware visibility are all derived from it).
    ///
    /// The pass renders a "shadow" copy of the model tree: the same geometry
    /// buffers, but every model drawn unlit with its ID as the flat color, so
    /// the ID bytes survive the UNORM target exactly (id = r | g << 8 | b << 16,
    /// 0 = background). The ocean, the sky and the underwater composite are NOT
    /// part of the pass: it is the models, and everything else is background.
    ///
    /// Usage: create it once per camera; add renderGraph() to the command graph
    /// (it renders alongside the color pipelines, from the same camera), call
    /// update() every frame (next to Scene::update, it carries the frame poses
    /// into the shadow), then capture() between frames. Use it with the
    /// simulation time for reproducible datasets.
    ///
    /// For an on-screen semantic view, render SemanticOverlay on top of the
    /// presented photo: it samples this pass's color image and paints each
    /// model with the palette color of its name (see SemanticPalette).
    ///
    /// Models added to the scene after the pass was created are not rendered
    /// by it (create the pass once the models are loaded). Skinned meshes
    /// render in their bind pose (the semantic pipeline carries no joints).
    class VSG_OCEAN_EXPORT SemanticPass : public vsg::Inherit<vsg::Object, SemanticPass>
    {
    public:
        /// The semantic image: the model ID of each pixel, row-major from the top.
        struct Image
        {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<uint32_t> ids;
        };

        /// A model's annotation derived from a captured image: the visible
        /// (occlusion-aware) pixel box, top-left origin, and the visible pixel
        /// count (the visibility numerator; the denominator is up to the
        /// application's definition).
        struct Annotation
        {
            std::string model;
            uint32_t id = 0;
            uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; // inclusive bounds
            size_t pixels = 0;
        };

        /// Build the shadow of the scene's models and the offscreen target at
        /// the given extent. The camera is shared with a color pipeline (the
        /// same view and projection).
        SemanticPass(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Device> device,
                     vsg::ref_ptr<vsg::Camera> camera, VkExtent2D extent);

        /// The pass's render graph: add to the command graph. Renders into its
        /// own target; the models only, depth-tested against each other.
        [[nodiscard]] vsg::ref_ptr<vsg::RenderGraph> renderGraph() const { return renderGraph_; }

        /// The pass's color image as a sampled view (the ID bytes, UNORM): the
        /// input of a display overlay such as SemanticOverlay.
        [[nodiscard]] const vsg::ref_ptr<vsg::ImageView>& colorView() const { return colorView_; }

        /// Enable / disable the pass's rendering: a disabled pass records no
        /// draw at all (its graph stays in the command graph at no frame cost).
        /// The pass is enabled by default; capture() still needs an enabled,
        /// up-to-date pass.
        void setEnabled(bool enabled);

        /// Carry the current pose of the scene's frames into the shadow copy.
        /// Call every frame, next to Scene::update().
        void update();

        /// Read the semantic image back to the CPU (a one-off transfer that
        /// waits for completion: call it between frames). False on failure.
        [[nodiscard]] bool capture(Image& image) const;

        /// The models covered by this pass, index = ID - 1 (the names of
        /// Scene::modelNames() at construction, minus the empty string).
        [[nodiscard]] const std::vector<std::string>& modelNames() const { return names_; }

        /// The annotations of a captured image: the visible box and pixel
        /// count of each model present in it, in ID order.
        [[nodiscard]] std::vector<Annotation> boxes(const Image& image) const;

    protected:
        vsg::ref_ptr<vsg::Device> device_;
        VkExtent2D extent_{};
        vsg::ref_ptr<vsg::Image> colorImage_;
        vsg::ref_ptr<vsg::ImageView> colorView_;
        vsg::ref_ptr<vsg::RenderPass> renderPass_;
        vsg::ref_ptr<vsg::Framebuffer> framebuffer_;
        vsg::ref_ptr<vsg::RenderGraph> renderGraph_;
        vsg::ref_ptr<vsg::View> view_;
        std::vector<std::string> names_;
        std::vector<std::pair<vsg::ref_ptr<Frame>, vsg::ref_ptr<vsg::MatrixTransform>>> frames_;
    };
}
