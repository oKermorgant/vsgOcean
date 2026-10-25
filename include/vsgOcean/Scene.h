#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>
#include <vsgOcean/Environment.h>
#include <vsgOcean/Model.h>
#include <vsgOcean/OceanSurface.h>
#include <vsgOcean/Sky.h>
#include <vsgOcean/Underwater.h>

namespace vsgOcean
{
    /// The complete ocean scene: a tree of Models rooted at the world model, together with
    /// the environment rendering - sky, sun-driven lighting and the ocean surface - all
    /// created by the scene and tunable from the outside.
    ///
    /// Add the Scene to a vsg::View and call update() every frame with the camera and
    /// simulation time. Models are addressable by frame name and movable at runtime
    /// through setPose() - the seam for driving visuals from a dynamics simulator.
    class VSG_OCEAN_EXPORT Scene : public vsg::Inherit<vsg::Group, Scene>
    {
    public:
        Scene();

        /// The root "world" model, at the origin by definition.
        vsg::ref_ptr<Model> world() const { return world_; }

        /// Look up a model by name anywhere in the model tree. Returns null if unknown.
        vsg::ref_ptr<Model> model(const std::string& modelName) const;

        /// Look up a frame by name anywhere in the model tree (see Model::frame).
        vsg::ref_ptr<Frame> frame(const std::string& frameName) const;

        /// Add a model to the scene, attached to the named parent model
        /// (empty, or "world", means the world model).
        void addModel(const vsg::ref_ptr<Model>& model, const std::string& parentModelName = "");

        /// Set the pose of a named frame in any model of the tree (see Model::setPose).
        void setPose(const std::string& frameName, const vsg::dmat4& pose, const std::string& parentFrameName = "");

        // --- environment, created with the scene and tunable ---

        vsg::ref_ptr<Environment> environment() const { return environment_; }
        void setEnvironment(const vsg::ref_ptr<Environment>& environment) { environment_ = environment; }

        /// The sky, driven by the environment.
        vsg::ref_ptr<Sky> sky() const { return sky_; }

        /// The ocean surface and its wave field (the Gazebo wave model).
        vsg::ref_ptr<OceanSurface> surface() const { return surface_; }

        /// The underwater pass, blended over the scene when the camera is below the surface.
        vsg::ref_ptr<Underwater> underwater() const { return underwater_; }

        /// The ambient light, dimmed at night by update().
        vsg::ref_ptr<vsg::AmbientLight> ambientLight() const { return ambientLight_; }

        /// The directional sun light, driven by the environment.
        vsg::ref_ptr<vsg::DirectionalLight> sunLight() const { return sunLight_; }

        /// Refresh the sky, lighting and ocean surface from the environment, camera and
        /// simulation time t in seconds. Call every frame, before record.
        void update(const vsg::Camera* camera, double t);

        // --- scene loading ---

        /// Build a scene from files: SDF worlds and models (when vsgOcean is built with SDF
        /// support) or any model / scene file supported by the provided vsg::Options.
        /// Every successfully loaded file is merged into one scene. Returns an empty scene
        /// if nothing could be loaded.
        static vsg::ref_ptr<Scene> load(const std::vector<vsg::Path>& filenames, vsg::ref_ptr<vsg::Options> options);
        static vsg::ref_ptr<Scene> load(const vsg::Path& filename, vsg::ref_ptr<vsg::Options> options);

    protected:
        vsg::ref_ptr<Model> world_;
        vsg::ref_ptr<Environment> environment_;
        vsg::ref_ptr<Sky> sky_;
        vsg::ref_ptr<OceanSurface> surface_;
        vsg::ref_ptr<Underwater> underwater_;
        vsg::ref_ptr<vsg::AmbientLight> ambientLight_;
        vsg::ref_ptr<vsg::DirectionalLight> sunLight_;
    };
}
