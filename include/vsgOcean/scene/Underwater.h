#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Environment.h>

namespace vsgOcean
{
    /// Underwater rendering composite: a fullscreen pass that draws the offscreen scene
    /// to the window, applying per pixel (from the scene depth buffer) the atmospheric
    /// fog by view distance - the models use the standard vsg shaders, which have no fog
    /// of their own - and, when the camera is below the water surface, the Jerlov
    /// underwater light transport: the water path length follows the view distance to
    /// each object, and the transmittance is per channel (red is absorbed first).
    /// A pass-through of the sky where there is no scene geometry.
    ///
    /// The pass samples the offscreen color and depth views of the render target built by
    /// vsgOcean::createCommandGraph, which also wires it into the command graph and into
    /// the Scene (Scene::update refreshes it from the environment and camera).
    class VSG_OCEAN_EXPORT Underwater : public vsg::Inherit<vsg::Group, Underwater>
    {
    public:
        /// color and depth: the offscreen scene attachments the pass composites
        Underwater(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> depth);

        /// Point the pass at new offscreen attachments (after a render target rebuild):
        /// swaps the sampled views and releases the descriptor set, so the next
        /// compile (vsg::CompileManager) rebinds them.
        void setSceneTextures(vsg::ref_ptr<vsg::ImageView> color, vsg::ref_ptr<vsg::ImageView> depth);

        /// Refresh the pass from the environment (Jerlov water type, fog), the camera
        /// (eye, inverse view projection, render extent) and this camera's submersion
        /// depth below the surface (each pipeline computes its own). Call every frame,
        /// before record.
        void update(const Environment& environment, const vsg::Camera* camera, double submersion);

    protected:
        // uniform block: [0..3] inverse view projection, [4] eye + submersion depth,
        // [5] water color + mean attenuation, [6] per-channel attenuation + sun height,
        // [7] render extent + sea level, [8] fog color + density
        vsg::ref_ptr<vsg::vec4Array> uniform_;

        vsg::ref_ptr<vsg::DescriptorImage> colorDescriptor_;
        vsg::ref_ptr<vsg::DescriptorImage> depthDescriptor_;
        vsg::ref_ptr<vsg::DescriptorSet> descriptorSet_;
    };
}
