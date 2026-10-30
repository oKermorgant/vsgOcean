/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/scene/RenderTarget.h>
#include <vsgOcean/scene/RenderPipeline.h>
#include <vsgOcean/ui/Hud.h>
#include <vsgOcean/scene/Underwater.h>

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
    }

    Scene::Scene() :
        world_(Model::create("world")),
        environment_(Environment::create()),
        sky_(Sky::create()),
        surface_(OceanSurface::create()),
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
    }

    void Scene::setSurfaceEnabled(bool enabled)
    {
        surfaceSwitch_->setAllChildren(enabled);
    }

    bool Scene::surfaceEnabled() const
    {
        return !surfaceSwitch_->children.empty() && surfaceSwitch_->children[0].mask != 0;
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
    }

    void Scene::update(const vsg::Camera* camera, double t)
    {
        // the sky evaluates its own underwater mode per camera, in-shader (each
        // render pipeline's camera gets its own submersion below the wave field)
        sky_->update(*environment_, t, surfaceEnabled() ? surface_->wavefield() : nullptr);

        // scene lighting follows the sun: bright and neutral at noon, dim and warm at
        // dawn and dusk, black at night (no ambient floor: an unlit night scene is lit
        // only by its own lights). The shared lights are camera independent: the
        // per-camera water filtering of the scene happens in each pipeline's composite
        // (per-pixel Beer-Lambert over the view path)
        const auto sun = environment_->sunDirection();
        const double day = std::clamp((sun.z - -0.05) / (0.25 - -0.05), 0.0, 1.0);
        const double dayFactor = day * day * (3.0 - 2.0 * day); // smoothstep

        sunLight_->direction = sun;
        sunLight_->intensity = static_cast<float>(0.95 * dayFactor);
        sunLight_->color = vsg::mix(vsg::vec3(1.0f, 0.55f, 0.25f), vsg::vec3(1.0f, 0.97f, 0.90f), static_cast<float>(dayFactor));
        ambientLight_->intensity = static_cast<float>(0.07 * dayFactor);

        surface_->update(*environment_, t);

        // the main pipeline's composite follows the main camera; every other pipeline
        // computes its own (see RenderPipeline::update)
        if (underwater_)
        {
            double submersion = 0.0;
            if (surfaceEnabled() && camera && camera->viewMatrix)
            {
                if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
                {
                    const double waveHeight = environment_->seaLevel() + surface_->wavefield()->height(lookAt->eye.x, lookAt->eye.y, t);
                    submersion = waveHeight - lookAt->eye.z; // positive below the surface
                }
            }
            underwater_->update(*environment_, camera, submersion);
        }

        // camera tracking: keep the camera centred on the tracked model, preserving
        // the view direction and distance, so orbiting and dollying still work
        if (!trackedFrame_.empty() && camera && camera->viewMatrix)
        {
            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                vsg::dmat4 pose;
                if (frameWorldPose(trackedFrame_, pose))
                {
                    const vsg::dvec3 position(pose[3][0], pose[3][1], pose[3][2]);
                    lookAt->eye += position - lookAt->center;
                    lookAt->center = position;
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

        return scene;
    }

    vsg::ref_ptr<Scene> Scene::load(const vsg::Path& filename, vsg::ref_ptr<vsg::Options> options)
    {
        return load(std::vector<vsg::Path>{filename}, options);
    }

    namespace
    {
        // Watches the window size and rebuilds the main pipeline's render target and
        // composited image once the size has been stable for a moment, so an
        // interactive resize is not interrupted by rebuilds (and their resource
        // allocation). The pipeline keeps the previous resources alive until the next
        // rebuild, safely past the frames that may still be in flight and sampling
        // them.
        class RenderTargetResize : public vsg::Inherit<vsg::Operation, RenderTargetResize>
        {
        public:
            RenderTargetResize(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::Camera> camera,
                               const vsg::ref_ptr<RenderPipeline>& pipeline, vsg::ref_ptr<Hud> hud = {}) :
                window_(std::move(window)),
                viewer_(std::move(viewer)),
                camera_(std::move(camera)),
                pipeline_(pipeline),
                hud_(std::move(hud))
            {
            }

            void run() override
            {
                const auto windowExtent = window_->extent2D();
                if (windowExtent.width == 0 || windowExtent.height == 0) return; // minimized
                if (windowExtent.width == pipeline_->target()->extent().width &&
                    windowExtent.height == pipeline_->target()->extent().height)
                    return;

                // a new size: wait until it has been stable
                const auto now = vsg::clock::now();
                if (windowExtent.width != lastSeen_.width || windowExtent.height != lastSeen_.height)
                {
                    lastSeen_ = windowExtent;
                    stableSince_ = now;
                    return;
                }
                if (now - stableSince_ < std::chrono::milliseconds(250)) return;

                // rebuild the pipeline's target and composited image
                const auto oldExtent = pipeline_->target()->extent();
                pipeline_->resize(windowExtent);
                lastSeen_ = windowExtent;

                // the camera viewport and projection aspect follow the window
                camera_->viewportState = vsg::ViewportState::create(0, 0, windowExtent.width, windowExtent.height);
                if (camera_->projectionMatrix) camera_->projectionMatrix->changeExtent(oldExtent, windowExtent);

                // HUD overlays follow the window too
                if (hud_) hud_->resize(windowExtent);

                // rebind the composite and present passes to the new attachments
                if (viewer_->compileManager)
                {
                    viewer_->compileManager->compile(pipeline_->composite());
                    viewer_->compileManager->compile(pipeline_->presentNode());
                }
            }

        protected:
            ~RenderTargetResize() override = default;

            vsg::ref_ptr<vsg::Window> window_;
            vsg::ref_ptr<vsg::Viewer> viewer_;
            vsg::ref_ptr<vsg::Camera> camera_;
            vsg::ref_ptr<RenderPipeline> pipeline_;
            vsg::ref_ptr<Hud> hud_;
            VkExtent2D lastSeen_{std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
            vsg::clock::time_point stableSince_{};
        };
    }
    vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::Camera> camera, const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Node> overlay)
    {
        // the main pipeline: the scene renders into an offscreen target, composited
        // into the pipeline's output image, then presented to the window with the HUD
        // and GUI overlays on top
        auto pipeline = RenderPipeline::create(scene, window, camera);
        scene->setUnderwater(pipeline->composite());

        return createCommandGraph(window, viewer, pipeline, overlay);
    }

    vsg::ref_ptr<vsg::CommandGraph> createCommandGraph(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer,
                                                        const vsg::ref_ptr<RenderPipeline>& pipeline, vsg::ref_ptr<vsg::Node> overlay)
    {
        // the composite is presented to the window, in front of which the HUD and the
        // GUI overlays are drawn, within the same render pass
        auto presentView = vsg::View::create(pipeline->camera());
        presentView->addChild(pipeline->presentNode());

        auto windowRenderGraph = vsg::RenderGraph::create(window);
        windowRenderGraph->addChild(presentView);
        if (overlay) windowRenderGraph->addChild(overlay);

        auto commandGraph = vsg::CommandGraph::create(window);
        commandGraph->addChild(pipeline->sceneRenderGraph());
        commandGraph->addChild(pipeline->compositeRenderGraph());
        commandGraph->addChild(windowRenderGraph);

        // rebuild the offscreen target when the window size changes (debounced);
        // runs every frame from viewer->update(). HUD overlays in the overlay group
        // follow the resize as well
        vsg::ref_ptr<Hud> hud;
        if (overlay)
        {
            if (auto group = overlay->cast<vsg::Group>())
            {
                for (auto& child : group->children)
                {
                    if ((hud = child.cast<Hud>())) break;
                }
            }
        }
        viewer->updateOperations->add(RenderTargetResize::create(window, viewer, pipeline->camera(), pipeline, hud),
                                       vsg::UpdateOperations::ALL_FRAMES);

        return commandGraph;
    }
}
