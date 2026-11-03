/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/core/EnvironmentFile.h>
#include <vsgOcean/scene/RenderTarget.h>
#include <vsg/utils/LineSegmentIntersector.h>
#include <vsgOcean/scene/RenderPipeline.h>
#include <vsgOcean/ui/Hud.h>
#include <vsgOcean/scene/Underwater.h>
#include <vsgOcean/semantic/SemanticOverlay.h>

#include <algorithm>

#if defined(VSGOCEAN_WITH_SDF)
#    include <vsgOcean/scene/SDFLoader.h>
#endif

namespace vsgOcean
{
    namespace
    {
        bool isSdfFile(const vsg::Path& filename)
        {
            const auto extension = vsg::lowerCaseFileExtension(filename);
            return extension == ".sdf" || extension == ".world";
        }

        // records the load-time intensity of every point / spot light it meets
        // (the vsg light objects carry the SDF ceiling until update() scales them)
        class LampMaximaCollector : public vsg::Visitor
        {
        public:
            explicit LampMaximaCollector(std::vector<Scene::LampRecord>& lamps_, const std::string& modelName_)
                : lamps(lamps_), modelName(modelName_) {}

            std::vector<Scene::LampRecord>& lamps;
            const std::string& modelName;

            // the default apply(Object&) does not descend: walk every node
            void apply(vsg::Node& node) override
            {
                node.traverse(*this);
            }

            void apply(vsg::PointLight& light) override
            {
                record(light);
            }

            void apply(vsg::SpotLight& light) override
            {
                record(light);
            }

        private:
            void record(vsg::Light& light)
            {
                if (alreadySeen(light)) return;
                auto name = light.name.empty() ? "light" + std::to_string(lamps.size()) : light.name;
                Scene::LampRecord lamp;
                lamp.light = &light;
                lamp.name = modelName + "/" + name;
                lamp.ceiling = light.intensity;
                lamp.factor = 1.0;
                if (const auto spot = light.cast<vsg::SpotLight>())
                {
                    lamp.direction = vsg::normalize(spot->direction);
                    lamp.aimed = true;
                }
                lamps.push_back(std::move(lamp));
            }

            // a light reached through several paths (the model walk and the world
            // walk) is one lamp: keep the first record
            bool alreadySeen(const vsg::Light& light) const
            {
                return std::any_of(lamps.begin(), lamps.end(), [&light](const auto& record) { return record.light == &light; });
            }
        };
    }

    Scene::Scene() :
        world_(Model::create("world")),
        environment_(Environment::create()),
        sky_(Sky::create()),
        surface_(OceanSurface::create()),
        marineSnow_(MarineSnow::create()),
        rain_(Rain::create()),
        ambientLight_(vsg::AmbientLight::create()),
        sunLight_(vsg::DirectionalLight::create())
    {
        world_->setParentName({});

        // the children order defines the render order: the sky first (no depth writes),
        // then the lighting, the models and the transparent ocean surface (behind a
        // switch, so it can be disabled). The underwater composite is not part of the
        // scene: it is created by createCommandGraph and draws the offscreen scene to
        // the window
        surfaceSwitch_ = vsg::Switch::create();
        surfaceSwitch_->addChild(true, surface_);

        addChild(sky_);
        addChild(ambientLight_);
        addChild(sunLight_);
        addChild(world_);
        addChild(surfaceSwitch_);
        // the snow is transparent and never writes depth: drawn last, it blends over
        // everything, including the transparent surface. The rain follows: also
        // transparent, also never writing depth, and only ever above the water
        addChild(marineSnow_);
        addChild(rain_);
    }

    void Scene::setSurfaceEnabled(bool enabled)
    {
        surfaceSwitch_->setAllChildren(enabled);
    }

    double Scene::waveHeightScale() const
    {
        return surfaceEnabled() ? surface_->wavefield()->heightScale() : 0.0;
    }

    double Scene::surfaceHeight(double x, double y, double t) const
    {
        // the wave field above the mean water level, plus the environment's sea
        // level offset - the height a floating body rides at. The caller passes
        // the SAME raw time update() received: the waves animate at (t - origin)
        // - the single-precision shaders cannot hold a large absolute time - and
        // the CPU wave field must sample the same shifted clock as the GPU
        if (timeOrigin_) t -= *timeOrigin_;
        const auto waves = surfaceEnabled() ? surface_->wavefield()->height(x, y, t) : 0.0;
        return waves + environment_->seaLevel();
    }

    bool Scene::surfaceEnabled() const
    {
        return !surfaceSwitch_->children.empty() && surfaceSwitch_->children[0].mask != 0;
    }

    bool Scene::saveEnvironment(const vsg::Path& path) const
    {
        if (!environment_ || !surface_ || !surface_->wavefield()) return false;
        return writeEnvironmentFile(path, *environment_, *surface_->wavefield());
    }

    bool Scene::loadEnvironment(const vsg::Path& path)
    {
        if (!environment_ || !surface_ || !surface_->wavefield()) return false;
        return loadEnvironmentFile(path, *environment_, *surface_->wavefield());
    }

    vsg::ref_ptr<Model> Scene::model(const std::string& modelName) const
    {
        return world_->model(modelName);
    }

    vsg::ref_ptr<Frame> Scene::frame(const std::string& frameName) const
    {
        return world_->frame(frameName);
    }

    void Scene::addModel(const vsg::ref_ptr<Model>& model, const std::string& parentModelName)
    {
        if (!model) return;

        auto parent = (parentModelName.empty() || parentModelName == "world") ? world_ : world_->model(parentModelName);
        if (!parent)
        {
            vsg::warn("vsgOcean::Scene::addModel - unknown parent model ", parentModelName, ", attaching ", model->name, " to the world model");
            parent = world_;
        }

        parent->addModel(model);

        // the model's SDF lights (if any) join the dimmable set at their ceiling
        captureLampMaxima(model.get(), model->name);
    }

    void Scene::captureLampMaxima(vsg::Node* root, const std::string& modelName)
    {
        if (!root) return;

        LampMaximaCollector collector{lamps_, modelName};
        root->accept(collector);
        rebuildLightGroups();
    }

    void Scene::rebuildLightGroups()
    {
        lampGroups_.clear();

        // bucket the lamps by model, keeping the first-appearance order
        std::vector<std::pair<std::string, std::vector<size_t>>> byModel;
        for (size_t i = 0; i < lamps_.size(); ++i)
        {
            const auto model = lamps_[i].name.substr(0, lamps_[i].name.find('/'));
            const auto bucket = std::find_if(byModel.begin(), byModel.end(),
                                             [&model](const auto& entry) { return entry.first == model; });
            if (bucket != byModel.end()) bucket->second.push_back(i);
            else byModel.emplace_back(model, std::vector<size_t>{i});
        }

        // cluster each model's lamps: the relation is "directions within
        // 45 deg", and clusters are its CONNECTED COMPONENTS - a lamp joins
        // through ANY member (chaining), and two clusters merge whenever any
        // cross pair comes within 45 deg, so a group spans a whole face of
        // the robot however the lamps spread. The aimless (point) lamps of a
        // model form one group among themselves (a point light has no face)
        constexpr auto withinFace = 0.70710678; // cos(45 deg)
        for (const auto& [model, indices] : byModel)
        {
            // union-find over the model's lamps
            std::vector<size_t> parent(indices.size());
            std::iota(parent.begin(), parent.end(), 0);
            const auto find = [&parent](size_t a)
            {
                while (parent[a] != a) a = parent[a] = parent[parent[a]];
                return a;
            };
            const auto unite = [&find](std::vector<size_t>& p, size_t a, size_t b) { p[find(a)] = find(b); };
            for (size_t i = 0; i < indices.size(); ++i)
            {
                for (size_t j = i + 1; j < indices.size(); ++j)
                {
                    const auto& a = lamps_[indices[i]];
                    const auto& b = lamps_[indices[j]];
                    if (a.aimed != b.aimed) continue; // the aimless lamps group among themselves only
                    if (!a.aimed || dot(a.direction, b.direction) >= withinFace) unite(parent, i, j);
                }
            }

            // the components, ordered by their root member (stable names)
            std::map<size_t, std::vector<size_t>> components;
            for (size_t i = 0; i < indices.size(); ++i) components[find(i)].push_back(indices[i]);
            size_t groupIndex = 0;
            for (auto& [root, members] : components)
            {
                (void)root;
                lampGroups_.push_back({model + "/lights" + std::to_string(++groupIndex), std::move(members)});
            }
        }
    }

    std::vector<std::string> Scene::lightGroupNames() const
    {
        std::vector<std::string> names;
        names.reserve(lampGroups_.size());
        for (const auto& group : lampGroups_) names.push_back(group.name);
        return names;
    }

    std::vector<std::string> Scene::lightGroupMembers(const std::string& groupName) const
    {
        std::vector<std::string> members;
        if (const auto it = std::find_if(lampGroups_.begin(), lampGroups_.end(),
                                         [&groupName](const auto& group) { return group.name == groupName; });
            it != lampGroups_.end())
        {
            members.reserve(it->members.size());
            for (const auto i : it->members) members.push_back(lamps_[i].name);
        }
        return members;
    }

    double Scene::lightGroupIntensity(const std::string& groupName) const
    {
        const auto it = std::find_if(lampGroups_.begin(), lampGroups_.end(),
                                     [&groupName](const auto& group) { return group.name == groupName; });
        if (it != lampGroups_.end() && !it->members.empty()) return lamps_[it->members.front()].factor;
        return 1.0;
    }

    void Scene::setLightGroupIntensity(const std::string& groupName, double factor)
    {
        const auto clamped = std::clamp(factor, 0.0, 1.0);
        if (const auto it = std::find_if(lampGroups_.begin(), lampGroups_.end(),
                                         [&groupName](const auto& group) { return group.name == groupName; });
            it != lampGroups_.end())
        {
            for (const auto i : it->members) lamps_[i].factor = clamped;
            return;
        }
        vsg::warn("vsgOcean::Scene::setLightGroupIntensity - unknown light group ", groupName);
    }

    std::vector<std::string> Scene::lightNames() const
    {
        std::vector<std::string> names;
        names.reserve(lamps_.size());
        for (const auto& record : lamps_) names.push_back(record.name);
        return names;
    }

    double Scene::lightIntensity(const std::string& lightName) const
    {
        if (const auto it = std::find_if(lamps_.begin(), lamps_.end(),
                                         [&lightName](const auto& record) { return record.name == lightName; });
            it != lamps_.end())
        {
            return it->factor;
        }
        return 1.0;
    }

    void Scene::setLightIntensity(const std::string& lightName, double factor)
    {
        const auto clamped = std::clamp(factor, 0.0, 1.0);
        if (const auto it = std::find_if(lamps_.begin(), lamps_.end(),
                                         [&lightName](const auto& record) { return record.name == lightName; });
            it != lamps_.end())
        {
            it->factor = clamped;
            return;
        }
        vsg::warn("vsgOcean::Scene::setLightIntensity - unknown light ", lightName);
    }

    void Scene::setPose(const std::string& frameName, const vsg::dmat4& pose, const std::string& parentFrameName)
    {
        world_->setPose(frameName, pose, parentFrameName);
    }

    std::vector<std::string> Scene::modelNames() const
    {
        std::vector<std::string> names;
        names.reserve(world_->models().size());
        for (const auto& [modelName, model] : world_->models())
        {
            (void)model;
            names.push_back(modelName);
        }
        return names;
    }

    bool Scene::frameWorldPose(const std::string& frameName, vsg::dmat4& pose) const
    {
        return world_->frameWorldPose(frameName, vsg::dmat4(), pose); // the world model sits at the origin
    }

    void Scene::setTrackedFrame(const std::string& frameName)
    {
        trackedFrame_ = frameName;
        trackedPoseValid_ = false; // the relative viewpoint is re-established
    }

    void Scene::update(const vsg::Camera* camera, double t)
    {
        if (!timeOrigin_) timeOrigin_ = t;
        t -= *timeOrigin_;

        // the sky evaluates its own underwater mode per camera, in-shader (each
        // render pipeline's camera gets its own submersion below the wave field)
        sky_->update(*environment_, t, surfaceEnabled() ? surface_->wavefield() : nullptr);

        // scene lighting follows the sun: bright and neutral at noon, dim and warm at
        // dawn and dusk, black at night (no ambient floor: an unlit night scene is lit
        // only by its own lights). The shared lights are camera independent: the
        // per-camera water filtering of the scene happens in each pipeline's composite
        // (per-pixel Beer-Lambert over the view path)
        const auto sun = environment_->sunDirection();
        const auto day = std::clamp((sun.z - -0.05) / (0.25 - -0.05), 0.0, 1.0);
        const double dayFactor = day * day * (3.0 - 2.0 * day); // smoothstep

        sunLight_->direction = sun;
        // the cloud cover dims the scene lighting like it dims the sun itself (an
        // overcast day is ~15% of the clear-sun light, not black)
        sunLight_->intensity = static_cast<float>(0.95 * dayFactor * (1.0 - 0.85 * environment_->cloudiness() / 8.0));
        sunLight_->color = vsg::mix(vsg::vec3(1.0f, 0.55f, 0.25f), vsg::vec3(1.0f, 0.97f, 0.90f), static_cast<float>(dayFactor));
        ambientLight_->intensity = static_cast<float>(0.07 * dayFactor);

        // the sun's reflection terms on the water (glitter, glare lobe, the
        // reflected sun disc) require the sun to actually reach the camera:
        // one ray per frame from the camera towards the sun, against the world
        // geometry only (the ocean and sky are excluded by construction - the
        // ray points away from the surface and the sky is not geometry). The
        // result is smoothed so an island edge sliding over the sun fades the
        // reflection path instead of popping it. Sensor pipelines share the
        // main camera's visibility.
        if (camera && camera->viewMatrix && world_ && sun.z > 0.0)
        {
            const auto lookAt = camera->viewMatrix.cast<vsg::LookAt>();
            vsg::LineSegmentIntersector intersector{lookAt->eye, lookAt->eye + sun * 10000.0};
            world_->accept(intersector);
            const float visible = intersector.intersections.empty() ? 1.0f : 0.0f;
            sunVisibility_ += (visible - sunVisibility_) * 0.25f;
        }
        else
        {
            sunVisibility_ += (1.0f - sunVisibility_) * 0.25f;
        }
        surface_->setSunVisibility(sunVisibility_);

        surface_->update(*environment_, t);

        // the scene's point and spot lights, collected ONCE per frame (a full
        // scene-graph walk): the marine snow and every pipeline's composite share
        // this collection, so the walk runs exactly once per frame however many
        // pipelines there are. First each lamp is scaled to its runtime factor
        // below its SDF ceiling - both the model shading and the collected
        // lights (cones, snow sparkle) then see the dimmed intensity
        for (const auto& lamp : lamps_) lamp.light->intensity = lamp.ceiling * lamp.factor;
        lampLights_ = collectLampLights(world_.get());

        // the marine snow follows the water current and the lamp-lit water column
        marineSnow_->update(*environment_, t, lampLights_);

        // the rain follows the environment; the surface responds (roughness,
        // flattened glitter) through the rain rate it reads every frame
        rain_->update(*environment_, t);
        surface_->setRainRate(static_cast<float>(environment_->rainRate()));

        // the main pipeline's composite follows the main camera; every other pipeline
        // computes its own (see RenderPipeline::update)
        if (underwater_)
        {
            auto submersion = 0.0;
            if (surfaceEnabled() && camera && camera->viewMatrix)
            {
                if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
                {
                    // the mean sea level is the underwater criterion (stable across
                    // waves, no per-frame wave sampling); the exact Wavefield::height
                    // stays available for features that need it
                    submersion = environment_->seaLevel() - lookAt->eye.z; // positive below the surface
                }
            }
            underwater_->update(*environment_, camera, submersion, &lampLights_, waveHeightScale());
        }

        // camera tracking: the camera follows the tracked frame with a constant
        // relative viewpoint - the frame's rotation rotates the view with it, and
        // the camera controls (orbit, translate, dolly) adjust that relative
        // viewpoint. The frame's previous pose carries the viewpoint across frames
        if (!trackedFrame_.empty() && camera && camera->viewMatrix)
        {
            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                vsg::dmat4 pose;
                if (frameWorldPose(trackedFrame_, pose))
                {
                    if (trackedPoseValid_)
                    {
                        const auto previous = vsg::inverse(trackedPose_);
                        lookAt->eye = pose * (previous * lookAt->eye);
                        lookAt->center = pose * (previous * lookAt->center);
                    }
                    else
                    {
                        // tracking just started: centre the tracked object,
                        // preserving the view direction and distance
                        const vsg::dvec3 position(pose[3][0], pose[3][1], pose[3][2]);
                        lookAt->eye += position - lookAt->center;
                        lookAt->center = position;
                    }
                    trackedPose_ = pose;
                    trackedPoseValid_ = true;
                }
            }
        }
    }

    vsg::ref_ptr<Scene> Scene::load(const std::vector<vsg::Path>& filenames, vsg::ref_ptr<vsg::Options> options)
    {
        auto scene = Scene::create();

        for (const auto& filename : filenames)
        {
#if defined(VSGOCEAN_WITH_SDF)
            if (isSdfFile(filename))
            {
                loadScene(filename, scene, options);
                continue;
            }
#endif
            // any other supported model / scene file is loaded directly through vsg::read()
            if (auto node = vsg::read_cast<vsg::Node>(filename, options))
            {
                scene->world()->addChild(node);
            }
            else
            {
                vsg::warn("Unable to load scene ", filename);
            }
        }

        // every lamp loaded with the scene joins the dimmable set at its SDF
        // ceiling (the models were already captured by addModel - the pointer
        // dedup keeps one record per lamp)
        scene->captureLampMaxima(scene->world_.get(), "world");

        return scene;
    }

    vsg::ref_ptr<Scene> Scene::load(const vsg::Path& filename, vsg::ref_ptr<vsg::Options> options)
    {
        return load(std::vector<vsg::Path>{filename}, options);
    }

    namespace
    {
        // Watches the window size and resizes the main pipeline to it. While the size
        // is still changing (an interactive drag) the last composite is just stretched
        // over the window; once it has been stable for a moment the offscreen target
        // The offscreen target keeps its CREATION extent for the whole run: the
        // present pass stretches the composited image over the window, and the
        // pipeline camera follows the window ASPECT only, so the view stays
        // undistorted at the window's field of view. Rebuilding the target on
        // resize instead (d0209c5) raced the in-flight record / present against
        // the swapchain rebuilds of a drag-resize storm and hung the GPU
        // (vkQueuePresentKHR / an ANV empty-batch hang, reproducible with the
        // resize harness; the fixed target rides through the storm).
        class ProjectionFollow : public vsg::Inherit<vsg::WindowResizeHandler, ProjectionFollow>
        {
        public:
            vsg::ref_ptr<RenderPipeline> pipeline;
            vsg::ref_ptr<SemanticOverlay> semanticOverlay;

            void apply(vsg::Object& object) override
            {
                // the plain projection update needs no device idle; the present
                // pass must learn the new window extent too, or its normalized
                // sampling saturates at the composite's edge texel past the
                // original size (the repeated-edge artefact). The default
                // handler continues to rescale the present view and the overlays
                if (pipeline && visit(this, 0))
                {
                    pipeline->camera()->projectionMatrix->changeExtent(previous_extent, new_extent);
                    if (const auto present = pipeline->presentNode()) present->setWindowExtent(new_extent);
                }
                // the semantic overlay stretches the same way as the present pass
                if (semanticOverlay) semanticOverlay->setWindowExtent(new_extent);
                Inherit::apply(object);
            }
        };
    }

    vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::Camera> camera, const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Node> overlay)
    {
        // the main pipeline: the scene renders into an offscreen target, composited
        // into the pipeline's output image, then presented to the window with the HUD
        // and GUI overlays on top
        auto pipeline = RenderPipeline::create(scene, window, camera);
        scene->setUnderwater(pipeline->composite());

        return createCommandGraph(window, viewer, pipeline, overlay, {});
    }

    vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer,
                                                        const vsg::ref_ptr<RenderPipeline>& pipeline, vsg::ref_ptr<vsg::Node> overlay,
                                                        vsg::ref_ptr<vsg::Node> presentOverlay)
    {
        // the composite is presented to the window, in front of which the HUD and the
        // GUI overlays are drawn, within the same render pass
        // the present view has its own camera: the window resize handler rescales the
        // cameras of the window render graph, while the pipeline camera is resized
        // together with its render target (the TargetResizeHandler below)
        auto presentCamera = vsg::Camera::create(vsg::Orthographic::create(0.0, 1.0, 0.0, 1.0, 0.0, 1.0),
                                                 vsg::LookAt::create(), vsg::ViewportState::create(window->extent2D()));
        auto presentView = vsg::View::create(presentCamera);
        presentView->addChild(pipeline->presentNode());

        auto windowRenderGraph = vsg::RenderGraph::create(window);
        windowRenderGraph->addChild(presentView);
        // the semantic annotation view draws between the photo and the HUD / GUI
        if (presentOverlay) windowRenderGraph->addChild(presentOverlay);
        if (overlay) windowRenderGraph->addChild(overlay);

        // resize: the offscreen target keeps its extent; the pipeline camera
        // follows the window aspect (ProjectionFollow above), and the default
        // handler rescales the present view and the overlays
        auto resizeHandler = ProjectionFollow::create();
        resizeHandler->pipeline = pipeline;
        resizeHandler->semanticOverlay = presentOverlay ? presentOverlay.cast<SemanticOverlay>() : nullptr;
        windowRenderGraph->windowResizeHandler = resizeHandler;

        auto commandGraph = vsg::CommandGraph::create(window);
        commandGraph->addChild(pipeline->sceneRenderGraph());
        commandGraph->addChild(pipeline->compositeRenderGraph());
        commandGraph->addChild(windowRenderGraph);

        return commandGraph;
    }
}
