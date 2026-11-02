#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>
#include <vsgOcean/core/Environment.h>
#include <vsgOcean/core/Model.h>
#include <vsgOcean/scene/OceanSurface.h>
#include <vsgOcean/scene/MarineSnow.h>
#include <vsgOcean/scene/Sky.h>
#include <vsgOcean/scene/Underwater.h>
#include <optional>
#include <string>
#include <vector>

namespace vsgOcean
{
    class RenderPipeline;

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
        [[nodiscard]] vsg::ref_ptr<Model> model(const std::string& modelName) const;

        /// Look up a frame by name anywhere in the model tree (see Model::frame).
        [[nodiscard]] vsg::ref_ptr<Frame> frame(const std::string& frameName) const;

        /// A scene lamp with its SDF ceiling and the runtime factor below it:
        /// update() writes ceiling * factor into the vsg light each frame, so the
        /// model shading and the collected lamp lights (the volumetric cones,
        /// the snow sparkle) all follow.
        struct LampRecord
        {
            vsg::ref_ptr<vsg::Light> light;
            std::string name;   // qualified "model/light"
            double ceiling = 0.0; // the SDF load-time intensity
            double factor = 1.0;  // the runtime intensity factor, 0..1
            vsg::dvec3 direction{0.0, 0.0, 0.0}; // the aim (spot lamps)
            bool aimed = false;                  // false: a point lamp, no direction
        };

        /// The scene's dimmable lamps by qualified name "model/light" (every SDF
        /// <light> of every model, and any light of a directly loaded scene file).
        /// An SDF intensity is the CEILING; setLightIntensity scales a lamp below
        /// it (0 off, 1 the SDF value) - the model shading, the volumetric lamp
        /// cones and the marine snow sparkle all follow. Empty when the scene
        /// carries no lights.
        [[nodiscard]] std::vector<std::string> lightNames() const;

        /// The current intensity factor of a named lamp (0..1 of its SDF
        /// ceiling). 1 (the SDF value) for an unknown name.
        [[nodiscard]] double lightIntensity(const std::string& lightName) const;

        /// Dim a named lamp below its SDF ceiling (0 off, 1 the SDF value;
        /// anything above is clamped). Unknown names are ignored with a warning.
        void setLightIntensity(const std::string& lightName, double factor);

        /// The lamps of each model are CLUSTERED BY AIM DIRECTION: the cluster
        /// relation is "directions within 45 deg", and the groups are its
        /// CONNECTED COMPONENTS - a lamp joins through any member (chaining),
        /// and two groups merge whenever any cross pair comes within 45 deg.
        /// In practice a group spans one face of the robot (a vehicle's spread
        /// dive lamps are ONE lamp); a model's point lamps (no direction) form
        /// one group among themselves. The groups are the control surface of
        /// the lights widget and of the ROS topics; the per-lamp API above
        /// stays for precise programmatic use. Qualified "model/lightsN"
        /// names, ordered per model.
        [[nodiscard]] std::vector<std::string> lightGroupNames() const;

        /// The member lamp names of a light group (empty for an unknown group).
        [[nodiscard]] std::vector<std::string> lightGroupMembers(const std::string& groupName) const;

        /// The group's intensity factor (0..1 of the members' SDF ceilings);
        /// the FIRST member's factor (a group is set as a whole).
        [[nodiscard]] double lightGroupIntensity(const std::string& groupName) const;

        /// Dim a whole light group below its members' SDF ceilings (0 off, 1 the
        /// SDF values). Unknown names are ignored with a warning.
        void setLightGroupIntensity(const std::string& groupName, double factor);

        /// Add a model to the scene, attached to the named parent model
        /// (empty, or "world", means the world model).
        void addModel(const vsg::ref_ptr<Model>& model, const std::string& parentModelName = "");

        /// Set the pose of a named frame in any model of the tree (see Model::setPose).
        void setPose(const std::string& frameName, const vsg::dmat4& pose, const std::string& parentFrameName = "");

        /// The models of the scene (direct children of the world model), by name.
        [[nodiscard]] std::vector<std::string> modelNames() const;

        /// World pose of a named frame (e.g. a model). False (and an untouched pose)
        /// when unknown.
        [[nodiscard]] bool frameWorldPose(const std::string& frameName, vsg::dmat4& pose) const;

        /// The model the camera tracks (empty: none). Scene::update() keeps the
        /// camera's viewpoint relative to the tracked frame: the frame's rotation
        /// rotates the view with it, and orbiting / translating / dollying adjust
        /// that relative viewpoint.
        void setTrackedFrame(const std::string& frameName);
        const std::string& trackedFrame() const { return trackedFrame_; }

        // --- environment, created with the scene and tunable ---

        vsg::ref_ptr<Environment> environment() const { return environment_; }
        void setEnvironment(const vsg::ref_ptr<Environment>& environment) { environment_ = environment; }

        /// The sky, driven by the environment.
        vsg::ref_ptr<Sky> sky() const { return sky_; }

        /// The ocean surface and its wave field (the Gazebo wave model).
        vsg::ref_ptr<OceanSurface> surface() const { return surface_; }

        /// The marine snow field of the water column (density and current live in
        /// the Environment).
        vsg::ref_ptr<MarineSnow> marineSnow() const { return marineSnow_; }

        /// Enable / disable the ocean surface rendering; disabling it also disables the
        /// underwater rendering (the camera is never considered submerged).
        void setSurfaceEnabled(bool enabled);
        [[nodiscard]] bool surfaceEnabled() const;

        /// height scale of the surface waves in metres, 0 when the surface is disabled
        [[nodiscard]] double waveHeightScale() const;

        /// The underwater composite pass, applied to the scene when the camera is below
        /// the surface. Created by createCommandGraph together with the render target.
        [[nodiscard]] vsg::ref_ptr<Underwater> underwater() const { return underwater_; }
        void setUnderwater(const vsg::ref_ptr<Underwater>& underwater) { underwater_ = underwater; }

        /// The scene's point and spot lights in world space, collected ONCE by
        /// update() each frame (a full scene-graph walk) and shared by the marine
        /// snow and every pipeline's composite - nobody else should walk the graph
        /// for the lights.
        [[nodiscard]] const std::vector<LampLight>& lampLights() const { return lampLights_; }

        /// The ambient light, dimmed at night by update().
        vsg::ref_ptr<vsg::AmbientLight> ambientLight() const { return ambientLight_; }

        /// The directional sun light, driven by the environment.
        vsg::ref_ptr<vsg::DirectionalLight> sunLight() const { return sunLight_; }

        /// Refresh the sky, lighting and ocean surface from the environment, camera and
        /// simulation time t in seconds. Call every frame, before record. The animations
        /// run from the first t received: the shaders work in single precision, which
        /// cannot hold a large absolute time (a wall clock epoch freezes the waves).
        void update(const vsg::Camera* camera, double t);

        /// The t of the first update(): the shaders animate the waves at (t - origin)
        /// to stay within float precision, so a CPU caller sampling Wavefield::height
        /// (buoyancy) must pass the same shifted time. 0 before the first update().
        [[nodiscard]] double timeOrigin() const { return timeOrigin_.value_or(0.0); }

        // --- scene loading ---

        /// Build a scene from files: SDF worlds and models (when vsgOcean is built with SDF
        /// support) or any model / scene file supported by the provided vsg::Options.
        /// Every successfully loaded file is merged into one scene. Returns an empty scene
        /// if nothing could be loaded.
        [[nodiscard]] static vsg::ref_ptr<Scene> load(const std::vector<vsg::Path>& filenames, vsg::ref_ptr<vsg::Options> options);
        [[nodiscard]] static vsg::ref_ptr<Scene> load(const vsg::Path& filename, vsg::ref_ptr<vsg::Options> options);

    protected:
        /// A direction cluster of one model's lamps: every aimed member lies
        /// within 45 deg of every other (one robot face); the aimless (point)
        /// lamps of a model share one group.
        struct LampGroup
        {
            std::string name;              // qualified "model/lightsN"
            std::vector<size_t> members;   // indices into the lamp records
        };

        /// Record the load-time intensity of every point / spot light under root
        /// as its CEILING (an SDF <light> intensity is the max; update() scales
        /// the lights by setLightIntensity below it), then rebuild the groups.
        void captureLampMaxima(vsg::Node* root, const std::string& modelName);

        /// Recluster the captured lamps into the per-model direction groups.
        void rebuildLightGroups();

        vsg::ref_ptr<Model> world_;
        vsg::ref_ptr<Environment> environment_;
        vsg::ref_ptr<Sky> sky_;
        vsg::ref_ptr<OceanSurface> surface_;
        vsg::ref_ptr<MarineSnow> marineSnow_;
        vsg::ref_ptr<vsg::Switch> surfaceSwitch_;
        vsg::ref_ptr<Underwater> underwater_;
        std::vector<LampLight> lampLights_;
        // the dimmable scene lamps: the SDF load-time intensity (the ceiling)
        // and the runtime factor of each, by qualified name
        std::vector<LampRecord> lamps_;
        // the per-model direction clusters (the control surface)
        std::vector<LampGroup> lampGroups_;
        // the smoothed sun visibility driving the ocean's sun reflection terms
        float sunVisibility_ = 1.0f;
        std::optional<double> timeOrigin_; // the t of the first update()
        vsg::ref_ptr<vsg::AmbientLight> ambientLight_;
        vsg::ref_ptr<vsg::DirectionalLight> sunLight_;
        std::string trackedFrame_;
        vsg::dmat4 trackedPose_;     // the tracked frame's world pose of the last update
        bool trackedPoseValid_{false};
    };

    /// Build the record graph for a scene: the scene (sky, lighting, models, ocean
    /// surface) renders into an offscreen color+depth target, then the underwater
    /// composite draws it to the window, applying the atmospheric fog and the Jerlov
    /// light transport by view distance when the camera is submerged. The underwater
    /// pass is created and attached to the scene, and refreshed by Scene::update every
    /// frame.
    ///
    /// The offscreen target follows the window size: a per-frame operation watches the
    /// window extent and rebuilds the target (and the camera viewport / aspect) once
    /// the size has been stable for a moment, so an interactive resize is not
    /// interrupted by rebuilds. Requires a single-sample window (no MSAA).
    ///
    /// overlay: an optional node rendered on the window after the composite (e.g. a
    /// vsgImGui::RenderImGui GUI), within the same render pass.
    [[nodiscard]] VSG_OCEAN_EXPORT vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(vsg::ref_ptr<vsg::Window> window,
                                                                                      vsg::ref_ptr<vsg::Viewer> viewer,
                                                                                      vsg::ref_ptr<vsg::Camera> camera,
                                                                                      const vsg::ref_ptr<Scene>& scene,
                                                                                      vsg::ref_ptr<vsg::Node> overlay = {});

    /// The same record graph for an existing main pipeline (e.g. kept by the
    /// application for screenshot capture): the pipeline's scene and composite passes,
    /// then the present pass to the window with the overlays on top.
    [[nodiscard]] VSG_OCEAN_EXPORT vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(vsg::ref_ptr<vsg::Window> window,
                                                                                      vsg::ref_ptr<vsg::Viewer> viewer,
                                                                                      const vsg::ref_ptr<RenderPipeline>& pipeline,
                                                                                      vsg::ref_ptr<vsg::Node> overlay = {});
}
