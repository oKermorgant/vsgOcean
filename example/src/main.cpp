#include <vsg/all.h>

#ifdef vsgXchange_FOUND
#    include <vsgXchange/all.h>
#endif

#include "ExampleOptions.h"
#include "Headless.h"

#include <vsgOcean/ui/EnvironmentControls.h>
#include <vsgOcean/scene/RenderPipeline.h>
#include <vsgOcean/ui/EnvironmentGui.h>
#include <vsgOcean/ui/HorizonController.h>
#include <vsgOcean/scene/Scene.h>

/// Resolve a model path (relative to the example models directory) against the source
/// tree, unless resources are embedded in the build directory.
inline std::string modelFile(const std::string& path)
{
#ifdef VSGOCEAN_EXAMPLE_MODELS_DIR
    return VSGOCEAN_EXAMPLE_MODELS_DIR "/" + path;
#else
    return "models/" + path;
#endif
}

#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    try
    {
        // all the command line settings in one place (see ExampleOptions.h)
        vsg::CommandLine arguments(&argc, argv);
        auto settings = vsgOceanExample::parseOptions(arguments, argc);
        if (settings.quit) return settings.exitCode;
        const auto options = settings.options;

        // build the overall ocean scene: models, sky, lighting and ocean surface
        vsg::ref_ptr<vsgOcean::Scene> vsg_scene;
        if (!settings.sceneFilenames.empty())
        {
            vsg_scene = vsgOcean::Scene::load(settings.sceneFilenames, options);
        }
#ifdef VSGOCEAN_WITH_SDF
        else
        {
            vsg_scene = vsgOcean::Scene::load({modelFile("islands/islands.sdf"), modelFile("aquabot/aquabot.sdf"),
                                               modelFile("bluerov2/bluerov2.sdf"), modelFile("herkules_ship_wreck/model.sdf")}, options);
        }
#else
        else
        {
            // degraded demo without SDF support: load the raw terrain and hull meshes as named
            // models, so the vehicle trajectories below still have something to animate
            vsg_scene = vsgOcean::Scene::load({modelFile("islands/islands.glb"),
                                               modelFile("aquabot/mesh/blue_boat.glb"),
                                               modelFile("bluerov2/meshes/bluerov2_noprop.glb")}, options);
        }
#endif

        // apply the command line environment / wave settings to the scene
        const auto environment = vsg_scene->environment();
        if (settings.overrideSun)
        {
            environment->setSunElevation(settings.sunElevation);
            environment->setSunAzimuth(settings.sunAzimuth);
        }
        if (settings.overrideWind)
        {
            environment->setWindSpeed(settings.windSpeed);
            environment->setWindDirection(settings.windDirection);
        }
        if (settings.overrideJerlov)
        {
            environment->setJerlovType(static_cast<vsgOcean::Environment::JerlovType>(settings.jerlov));
        }
        if (settings.overrideFog)
        {
            environment->setFogDensity(settings.fogDensity);
        }
        const auto wavefield = vsg_scene->surface()->wavefield();
        wavefield->setAmplitude(settings.waveAmplitude);
        wavefield->setPeriod(settings.wavePeriod);
        wavefield->setSteepness(settings.waveSteepness);
        wavefield->setNumber(static_cast<size_t>(settings.waveNumber));
        wavefield->setScale(settings.waveScale);
        wavefield->setAngle(vsg::radians(settings.waveAngle));

        // geolocation of the scene: taken from the SDF world when available,
        // Pornichet (France) hard-coded otherwise
        if (environment->latitude() == 0.0 && environment->longitude() == 0.0)
        {
            environment->setLatitude(47.1075);
            environment->setLongitude(-2.3533);
        }

        vsg::info("Environment: sun elevation ", environment->sunElevation(), " deg, azimuth ", environment->sunAzimuth(),
                  " deg; wind ", environment->windSpeed(), " m/s from ", environment->windDirection(), " deg; water type ",
                  environment->jerlovName(), "; fog density ", environment->fogDensity(), "; location ", environment->latitude(),
                  " lat, ", environment->longitude(), " lon");

        // demo: links animated wrt their parent frame (steering pods of the surface vehicle,
        // spinning propellers and thrusters). setPose() replaces the whole matrix, so their
        // initial poses - as loaded from the SDF files - are captured first.
        const std::vector<std::string> steeredLinks = {"aquabot/left_engine_link", "aquabot/right_engine_link"};
        const std::vector<std::string> spinningLinks = {
            "aquabot/left_propeller_link", "aquabot/right_propeller_link",
            "bluerov2/thruster1", "bluerov2/thruster2", "bluerov2/thruster3",
            "bluerov2/thruster4", "bluerov2/thruster5", "bluerov2/thruster6"};

        std::map<std::string, vsg::dmat4> initialLinkPoses;
        for (const auto& linkName : steeredLinks)
        {
            if (auto link = vsg_scene->frame(linkName)) initialLinkPoses[linkName] = link->matrix;
        }
        for (const auto& linkName : spinningLinks)
        {
            if (auto link = vsg_scene->frame(linkName)) initialLinkPoses[linkName] = link->matrix;
        }

        // the sensor pipelines (created in the windowed path below); the demo animation
        // references their cameras, so they are declared here and stay null in headless
        vsg::ref_ptr<vsgOcean::RenderPipeline> rovPipeline, boatPipeline;

        // the demo animation: the model poses wrt the world model and the link poses
        // wrt their parent frame (the parent is guessed by vsgOcean from the model
        // tree). Shared by the windowed and the headless render loops.
        const auto demoFrame = [&](double t)
        {
            if (vsg_scene->model("aquabot"))
            {
                // the surface vehicle performs a large circle, riding the waves: the
                // wave field is sampled at its position, slightly below the local
                // surface so the hulls keep a visible draft (the mesh waterline is at
                // its origin, the hull bottom 0.026 m above)
                const auto angle = 2.0 * vsg::PI * t / 60.0;
                const double x = 10.0 * std::cos(angle);
                const double y = 10.0 * std::sin(angle);
                const double surfaceZ = wavefield->height(x, y, t);
                vsg_scene->setPose("aquabot", vsg::translate(vsg::dvec3(x, y, surfaceZ - 0.03)) * vsg::rotate(angle + vsg::PI / 2.0, 0.0, 0.0, 1.0));

                // the boat camera rides at the bow, above the hulls, looking along the
                // direction of travel (a forward-facing camera sees little of its boat)
                if (boatPipeline)
                {
                    const auto lookAt = boatPipeline->camera()->viewMatrix.cast<vsg::LookAt>();
                    const vsg::dvec3 travel(-std::sin(angle), std::cos(angle), 0.0);
                    lookAt->eye = vsg::dvec3(x, y, surfaceZ) + travel * 1.5 + vsg::dvec3(0.0, 0.0, 1.6);
                    lookAt->center = lookAt->eye + travel * 8.0 + vsg::dvec3(0.0, 0.0, -0.6);
                }
            }

            if (vsg_scene->model("bluerov2"))
            {
                // the ROV circles the wreck at (12, 0, -48), facing it. The vehicle
                // frame is x-front, y-port, z-up: the heading rotation points the
                // model's +X at the wreck, and the pitch tilts the nose down towards
                // it, so the forward camera takes in more of the wreck
                const auto angle = -2.0 * vsg::PI * t / 60.0;
                const vsg::dvec3 position(12.0 + 8.0 * std::cos(angle), 8.0 * std::sin(angle), -37.0);
                const auto attitude = vsg::rotate(angle + vsg::PI, 0.0, 0.0, 1.0) * vsg::rotate(vsg::radians(25.0), 0.0, 1.0, 0.0);
                vsg_scene->setPose("bluerov2", vsg::translate(position) * attitude);

                // the ROV camera is forward-facing, at the vehicle's nose: it looks at
                // the wreck it circles, without the vehicle in view
                if (rovPipeline)
                {
                    const auto lookAt = rovPipeline->camera()->viewMatrix.cast<vsg::LookAt>();
                    lookAt->eye = position + attitude * vsg::dvec3(0.55, 0.0, 0.15);
                    lookAt->center = lookAt->eye + (attitude * vsg::dvec3(1.0, 0.0, 0.0)) * 8.0;
                }
            }

            // steering pods of the surface vehicle: rotation about the local Z axis of the pod
            const auto steer = 0.5 * std::sin(2.0 * vsg::PI * t / 10.0);
            for (const auto& linkName : steeredLinks)
            {
                if (initialLinkPoses.count(linkName))
                {
                    vsg_scene->setPose(linkName, initialLinkPoses[linkName] * vsg::rotate(steer, 0.0, 0.0, 1.0));
                }
            }

            // spinning propellers and thrusters: rotation about the local X axis of the link
            const auto spin = 2.0 * vsg::PI * 5.0 * t;
            for (const auto& linkName : spinningLinks)
            {
                if (initialLinkPoses.count(linkName))
                {
                    vsg_scene->setPose(linkName, initialLinkPoses[linkName] * vsg::rotate(spin, 1.0, 0.0, 0.0));
                }
            }
        };

        // headless: render offscreen (a hardware GPU when the render group allows it,
        // no window or swapchain) and write the screenshot - over ssh -X the hardware
        // cannot present, but the offscreen rendering is ~50x faster than software
        if (settings.headless)
        {
            return vsgOcean::Headless::run(vsg_scene, settings, demoFrame);
        }

        // create the viewer and assign window(s) to it
        auto viewer = vsg::Viewer::create();
        auto window = vsg::Window::create(settings.windowTraits);
        if (!window)
        {
            std::cout << "Could not create window." << std::endl;
            return 1;
        }

        viewer->addWindow(window);

        const auto ellipsoidModel = vsg_scene->getRefObject<vsg::EllipsoidModel>("EllipsoidModel");

        vsg::ref_ptr<vsg::LookAt> lookAt;
        vsg::ref_ptr<vsg::ProjectionMatrix> perspective;
        if (ellipsoidModel)
        {
            // compute the bounds of the scene graph to help position camera
            vsg::ComputeBounds computeBounds;
            vsg_scene->accept(computeBounds);

            double initialRadius = vsg::length(computeBounds.bounds.max - computeBounds.bounds.min) * 0.5;
            double modelToEarthRatio = (initialRadius / ellipsoidModel->radiusEquator());

            // if the model is small compared to the radius of the earth position camera in local coordinate frame of the model rather than ECEF.
            if (modelToEarthRatio < 1.0)
            {
                vsg::dvec3 lla = ellipsoidModel->convertECEFToLatLongAltitude((computeBounds.bounds.min + computeBounds.bounds.max) * 0.5);

                auto worldToLocal = ellipsoidModel->computeWorldToLocalTransform(lla);
                auto localToWorld = ellipsoidModel->computeLocalToWorldTransform(lla);

                // recompute the bounds of the model in the local coordinate frame of the model, rather than ECEF
                // to give a tigher bound around the dataset.
                computeBounds.matrixStack.clear();
                computeBounds.matrixStack.push_back(worldToLocal);
                computeBounds.bounds.reset();
                vsg_scene->accept(computeBounds);

                auto bounds = computeBounds.bounds;
                vsg::dvec3 centre = (bounds.min + bounds.max) * 0.5;
                double radius = vsg::length(bounds.max - bounds.min) * 0.5;

                lookAt = vsg::LookAt::create(localToWorld * (centre + vsg::dvec3(0.0, 0.0, radius)), localToWorld * centre, vsg::dvec3(0.0, 1.0, 0.0) * worldToLocal);
            }
            else
            {
                lookAt = vsg::LookAt::create(vsg::dvec3(initialRadius * 2.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
            }

            perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), settings.nearFarRatio, settings.horizonMountainHeight);
        }
        else
        {
            // compute the bounds of the scene's models to help position the camera
            // (the sky and the ocean surface grid are not part of the model tree)
            vsg::ComputeBounds computeBounds;
            vsg_scene->world()->accept(computeBounds);

            vsg::dvec3 centre = (computeBounds.bounds.min + computeBounds.bounds.max) * 0.5;
            double radius = vsg::length(computeBounds.bounds.max - computeBounds.bounds.min) * 0.6;

            // set up the camera, starting with a close view of the boat when present;
            // the far plane still covers the whole scene
            double nearRadius = radius;
            if (auto boat = vsg_scene->model("aquabot"))
            {
                vsg::ComputeBounds boatBounds;
                boat->accept(boatBounds);

                if (boatBounds.bounds.valid())
                {
                    const vsg::dvec3 boatCentre = (boatBounds.bounds.min + boatBounds.bounds.max) * 0.5;
                    const double boatRadius = vsg::length(boatBounds.bounds.max - boatBounds.bounds.min) * 0.6;

                    lookAt = vsg::LookAt::create(boatCentre + vsg::dvec3(0.0, -boatRadius * 3.5, boatRadius * 1.5), boatCentre, vsg::dvec3(0.0, 0.0, 1.0));
                    nearRadius = boatRadius;
                }
            }

            if (!lookAt)
            {
                lookAt = vsg::LookAt::create(centre + vsg::dvec3(0.0, -radius * 3.5, 0.0), centre, vsg::dvec3(0.0, 0.0, 1.0));
            }

            perspective = vsg::Perspective::create(30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), 0.05, radius * 10.5);
        }

        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        // add close handler to respond to the close window button and pressing escape
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));

        auto cameraAnimation = vsg::CameraAnimationHandler::create(camera, settings.pathFilename, options);
        viewer->addEventHandler(cameraAnimation);
        if (settings.autoPlay && cameraAnimation->animation)
        {
            cameraAnimation->play();

            if (settings.reportAverageFrameRate && (settings.maxTime == std::numeric_limits<double>::max()) && (settings.numFrames < 0))
            {
                settings.maxTime = cameraAnimation->animation->maxTime();
            }
        }

        // environment keyboard controls (sun, water, waves); also carries the input
        // capture flag consumed by the camera controller and the GUI
        auto controls = vsgOcean::EnvironmentControls::create(vsg_scene);
        controls->sunCycleEnabled = settings.sunCycle;
        viewer->addEventHandler(controls);

        // camera controller: translate (left drag), horizon-level orbit (right drag), dolly (wheel);
        // ignores input while the GUI captures it
        auto cameraController = vsgOcean::HorizonController::create(camera);
        cameraController->inputBlocked = [controls]() { return controls->guiCapturesInput; };
        viewer->addEventHandler(cameraController);

        // if required preload specific number of PagedLOD levels.
        if (settings.loadLevels > 0)
        {
            vsg::LoadPagedLOD loadPagedLOD(camera, settings.loadLevels);

            auto startTime = vsg::clock::now();

            vsg_scene->accept(loadPagedLOD);

            auto time = std::chrono::duration<float, std::chrono::milliseconds::period>(vsg::clock::now() - startTime).count();
            std::cout << "No. of tiles loaded " << loadPagedLOD.numTiles << " in " << time << "ms." << std::endl;
        }

        // runtime UI: the help HUD (key bindings) always visible in a corner, the
        // environment sliders behind the H key. Two sensor cameras render to texture
        // - the ROV camera underwater, the boat camera on the surface - displayed as
        // previews in the panel; actual sensor simulation comes later
        const VkExtent2D sensorExtent{640, 480};
        const auto device = window->getOrCreateDevice();
        const auto colorFormat = window->surfaceFormat().format;
        const auto depthFormat = window->depthFormat();
        auto makeSensorCamera = [sensorExtent]() {
            return vsg::Camera::create(vsg::Perspective::create(60.0, 4.0 / 3.0, 0.1, 200.0),
                                       vsg::LookAt::create(vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0)),
                                       vsg::ViewportState::create(sensorExtent));
        };

        if (vsg_scene->model("bluerov2")) rovPipeline = vsgOcean::RenderPipeline::create(vsg_scene, device, makeSensorCamera(), sensorExtent, colorFormat, depthFormat);
        if (vsg_scene->model("aquabot")) boatPipeline = vsgOcean::RenderPipeline::create(vsg_scene, device, makeSensorCamera(), sensorExtent, colorFormat, depthFormat);

        std::vector<std::pair<std::string, vsg::ref_ptr<vsg::ImageView>>> cameraViews;
        if (rovPipeline) cameraViews.emplace_back("ROV camera (underwater)", rovPipeline->outputView());
        if (boatPipeline) cameraViews.emplace_back("boat camera (surface)", boatPipeline->outputView());

        auto overlay = vsg::Group::create();
        if (auto hud = vsgOcean::createEnvironmentHud(controls, options, window->extent2D())) overlay->addChild(hud);
        bool guiBuilt = false;
        if (auto gui = vsgOcean::createEnvironmentGui(window, viewer, controls, cameraViews))
        {
            overlay->addChild(gui);
            guiBuilt = true;
        }
        auto mainPipeline = vsgOcean::RenderPipeline::create(vsg_scene, window, camera);
        vsg_scene->setUnderwater(mainPipeline->composite());
        auto commandGraph = vsgOcean::createCommandGraph(window, viewer, mainPipeline, overlay);

        // the sensor pipelines render into the same command graph, before the window
        // pass (their composited images are sampled by the GUI previews). Their render
        // graphs are MASKED OUT whenever the previews are not on screen: the record
        // traversal skips masked children, so the two full scene renders are skipped -
        // a plain mask field, no Vulkan object is touched (safe mid-run)
        std::vector<vsg::ref_ptr<vsg::RenderGraph>> sensorGraphs;
        for (const auto& pipeline : {rovPipeline, boatPipeline})
        {
            if (!pipeline) continue;
            sensorGraphs.push_back(pipeline->sceneRenderGraph());
            sensorGraphs.push_back(pipeline->compositeRenderGraph());
        }
        commandGraph->children.insert(commandGraph->children.end() - 1, sensorGraphs.begin(), sensorGraphs.end());
        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

        if (settings.overrideEye) lookAt->eye = settings.eye;
        if (settings.overrideCenter) lookAt->center = settings.center;

        if (settings.instrumentation) viewer->assignInstrumentation(settings.instrumentation);

        if (settings.multiThreading)
        {
            viewer->setupThreading();

            if (settings.affinity)
            {
                auto cpu_itr = settings.affinity.cpus.begin();

                // set affinity of main thread
                if (cpu_itr != settings.affinity.cpus.end())
                {
                    std::cout << "vsg::setAffinity() " << *cpu_itr << std::endl;
                    vsg::setAffinity(vsg::Affinity(*cpu_itr++));
                }

                for (auto& thread : viewer->threads)
                {
                    if (thread.joinable() && cpu_itr != settings.affinity.cpus.end())
                    {
                        std::cout << "vsg::setAffinity(" << thread.get_id() << ") " << *cpu_itr << std::endl;
                        vsg::setAffinity(thread, vsg::Affinity(*cpu_itr++));
                    }
                }
            }
        }
        else if (settings.affinity)
        {
            std::cout << "vsg::setAffinity(";
            for (auto cpu_num : settings.affinity.cpus)
            {
                std::cout << " " << cpu_num;
            }
            std::cout << " )" << std::endl;

            vsg::setAffinity(settings.affinity);
        }

        const auto compileResult = viewer->compile(settings.resourceHints);

        if (settings.maxPagedLOD > 0)
        {
            // set targetMaxNumPagedLODWithHighResSubgraphs after Viewer::compile() as it will assign any DatabasePager if required.
            for (auto& task : viewer->recordAndSubmitTasks)
            {
                if (task->databasePager) task->databasePager->targetMaxNumPagedLODWithHighResSubgraphs = settings.maxPagedLOD;
            }
        }

        if (settings.autoPlay)
        {
            // find any animation groups in the loaded scene graph and play the first animation in each of the animation groups.
            auto animationGroups = vsg::visit<vsg::FindAnimations>(vsg_scene).animationGroups;
            for (auto ag : animationGroups)
            {
                if (!ag->animations.empty()) viewer->animationManager->play(ag->animations.front());
            }
        }

        viewer->start_point() = vsg::clock::now();

        // rendering main loop
        while (viewer->advanceToNextFrame() && (settings.numFrames < 0 || (settings.numFrames--) > 0) && (viewer->getFrameStamp()->simulationTime < settings.maxTime))
        {
            // pass any events into EventHandlers assigned to the Viewer
            viewer->handleEvents();

            viewer->update();

            // demo: update the model poses and the link poses for this frame
            const auto t = viewer->getFrameStamp()->simulationTime;
            demoFrame(t);

            // demo: the sun moves slowly through the sky, 1 simulated hour per second
            if (controls->sunCycleEnabled)
            {
                const auto dayPhase = 2.0 * vsg::PI * std::fmod(t, 24.0) / 24.0;
                environment->setSunElevation(60.0 * std::sin(dayPhase));
            }

            // refresh the sky, sun-driven lighting and ocean surface from the
            // environment, camera and simulation time
            vsg_scene->update(camera, t);

            // refresh the sensor pipelines with their own cameras (each computes its
            // own submersion, so the ROV camera renders underwater). The refresh is
            // cheap and keeps their state current, so a re-shown preview is right in
            // the first frame; only the expensive scene renders are skipped while
            // the previews are off screen (the panel and the camera widget both gate
            // them)
            const bool previewsVisible = guiBuilt && controls->panelVisible && controls->camerasVisible;
            if (rovPipeline) rovPipeline->setEnabled(previewsVisible);
            if (boatPipeline) boatPipeline->setEnabled(previewsVisible);
            if (rovPipeline) rovPipeline->update(vsg_scene, rovPipeline->camera().get(), t);
            if (boatPipeline) boatPipeline->update(vsg_scene, boatPipeline->camera().get(), t);

            viewer->recordAndSubmit();

            viewer->present();
        }

        if (!settings.screenshotPath.empty())
        {
            if (!mainPipeline->writeScreenshot(settings.screenshotPath)) return 1;
        }

        if (settings.reportAverageFrameRate)
        {
            const auto fs = viewer->getFrameStamp();
            double fps = static_cast<double>(fs->frameCount) / std::chrono::duration<double, std::chrono::seconds::period>(vsg::clock::now() - viewer->start_point()).count();
            std::cout << "Average frame rate = " << fps << " fps" << std::endl;
        }

        if (settings.reportMemoryStats)
        {
            if (options->sharedObjects)
            {
                vsg::LogOutput output;
                options->sharedObjects->report(output);
            }
        }

        if (auto profiler = settings.instrumentation.cast<vsg::Profiler>())
        {
            settings.instrumentation->finish();
            if (settings.logFilename)
            {
                std::ofstream fout(settings.logFilename);
                profiler->log->report(fout);
            }
            else
            {
                profiler->log->report(std::cout);
            }
        }
    }
    catch (const vsg::Exception& ve)
    {
        for (int i = 0; i < argc; ++i) std::cerr << argv[i] << " ";
        std::cerr << "\n[Exception] - " << ve.message << " result = " << ve.result << std::endl;
        return 1;
    }

    // clean up done automatically thanks to ref_ptr<>
    return 0;
}
