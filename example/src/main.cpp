#include <vsg/all.h>

#ifdef vsgXchange_FOUND
#    include <vsgXchange/all.h>
#endif

#include <vsgOcean/HorizonController.h>
#include <vsgOcean/Scene.h>

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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>


void enableGenerateDebugInfo(vsg::ref_ptr<vsg::Options> options)
{
    auto shaderHints = vsg::ShaderCompileSettings::create();
    shaderHints->generateDebugInfo = true;

    auto& text = options->shaderSets["text"] = vsg::createTextShaderSet(options);
    text->defaultShaderHints = shaderHints;

    auto& flat = options->shaderSets["flat"] = vsg::createFlatShadedShaderSet(options);
    flat->defaultShaderHints = shaderHints;

    auto& phong = options->shaderSets["phong"] = vsg::createPhongShaderSet(options);
    phong->defaultShaderHints = shaderHints;

    auto& pbr = options->shaderSets["pbr"] = vsg::createPhysicsBasedRenderingShaderSet(options);
    pbr->defaultShaderHints = shaderHints;
}

int main(int argc, char** argv)
{
    try
    {
        // set up defaults and read command line arguments to override them
        vsg::CommandLine arguments(&argc, argv);

        // create windowTraits using the any command line arugments to configure settings
        auto windowTraits = vsg::WindowTraits::create(arguments);

        // if we want to redirect std::cout and std::cerr to the vsg::Logger call vsg::Logger::redirect_stdout()
        if (arguments.read({"--redirect-std", "-r"})) vsg::Logger::instance()->redirect_std();

        // set up vsg::Options to pass in filepaths, ReaderWriters and other IO related options to use when reading and writing files.
        auto options = vsg::Options::create();
        options->sharedObjects = vsg::SharedObjects::create();
        options->fileCache = vsg::getEnv("VSG_FILE_CACHE");
        options->paths = vsg::getEnvPaths("VSG_FILE_PATH");

#ifdef vsgXchange_all
        // add vsgXchange's support for reading and writing 3rd party file formats
        options->add(vsgXchange::all::create());
#endif

        options->readOptions(arguments);

        if (uint32_t numOperationThreads = 0; arguments.read("--ot", numOperationThreads)) options->operationThreads = vsg::OperationThreads::create(numOperationThreads);

        bool reportAverageFrameRate = arguments.read("--fps");
        bool reportMemoryStats = arguments.read("--rms");
        if (arguments.read({"-t", "--test"}))
        {
            windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            windowTraits->fullscreen = true;
            reportAverageFrameRate = true;
        }
        if (arguments.read({"--st", "--small-test"}))
        {
            windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            windowTraits->width = 192, windowTraits->height = 108;
            windowTraits->decoration = false;
            reportAverageFrameRate = true;
        }

        bool multiThreading = arguments.read("--mt");
        auto maxTime = arguments.value(std::numeric_limits<double>::max(), "--max-time");

        if (arguments.read("--ThreadLogger")) vsg::Logger::instance() = vsg::ThreadLogger::create();
        if (int log_level = 0; arguments.read("--log-level", log_level)) vsg::Logger::instance()->level = vsg::Logger::Level(log_level);
        auto numFrames = arguments.value(-1, "-f");
        auto pathFilename = arguments.value<vsg::Path>("", "-p");
        auto loadLevels = arguments.value(0, "--load-levels");
        auto maxPagedLOD = arguments.value(0, "--maxPagedLOD");
        auto LODScale = arguments.value<double>(1.0, "--LODScale");

        auto horizonMountainHeight = arguments.value(0.0, "--hmh");
        auto nearFarRatio = arguments.value<double>(0.001, "--nfr");
        if (arguments.read("--rgb")) options->mapRGBtoRGBAHint = false;

        std::string shaderSetName;
        vsg::Path shaderSetFilename;
        while(arguments.read("--ss", shaderSetName, shaderSetFilename))
        {
            if (auto shaderSet = vsg::read_cast<vsg::ShaderSet>(shaderSetFilename, options))
            {
                options->shaderSets[shaderSetName] = shaderSet;
                vsg::info("assigned shaderSets[", shaderSetName, "] = ", shaderSet);
            }
        }

        bool depthClamp = arguments.read({"--dc", "--depthClamp"});
        if (depthClamp)
        {
            std::cout << "Enabled depth clamp." << std::endl;
            auto deviceFeatures = windowTraits->deviceFeatures = vsg::DeviceFeatures::create();
            deviceFeatures->get().samplerAnisotropy = VK_TRUE;
            deviceFeatures->get().depthClamp = VK_TRUE;
        }

        vsg::ref_ptr<vsg::ResourceHints> resourceHints;
        if (auto resourceHintsFilename = arguments.value<vsg::Path>("", "--rh"))
        {
            resourceHints = vsg::read_cast<vsg::ResourceHints>(resourceHintsFilename, options);
        }

        if (auto outputResourceHintsFilename = arguments.value<vsg::Path>("", "--orh"))
        {
            if (!resourceHints) resourceHints = vsg::ResourceHints::create();
            vsg::write(resourceHints, outputResourceHintsFilename, options);
            return 0;
        }

        if (arguments.read({"--shader-debug-info", "--sdi"}))
        {
            enableGenerateDebugInfo(options);
            windowTraits->deviceExtensionNames.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
        }

        if (int log_level = 0; arguments.read("--log-level", log_level)) vsg::Logger::instance()->level = vsg::Logger::Level(log_level);
        auto logFilename = arguments.value<vsg::Path>("", "--log");

        vsg::ref_ptr<vsg::Instrumentation> instrumentation;
        if (arguments.read({"--gpu-annotation", "--ga"}) && vsg::isExtensionSupported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            windowTraits->debugUtils = true;

            auto gpu_instrumentation = vsg::GpuAnnotation::create();
            if (arguments.read("--name"))
                gpu_instrumentation->labelType = vsg::GpuAnnotation::SourceLocation_name;
            else if (arguments.read("--className"))
                gpu_instrumentation->labelType = vsg::GpuAnnotation::Object_className;
            else if (arguments.read("--func"))
                gpu_instrumentation->labelType = vsg::GpuAnnotation::SourceLocation_function;

            instrumentation = gpu_instrumentation;
        }
        else if (arguments.read({"--profiler", "--pr"}))
        {
            // set Profiler options
            auto settings = vsg::Profiler::Settings::create();
            arguments.read("--cpu", settings->cpu_instrumentation_level);
            arguments.read("--gpu", settings->gpu_instrumentation_level);
            arguments.read("--log-size", settings->log_size);

            // create the profiler
            instrumentation = vsg::Profiler::create(settings);
        }

        vsg::Affinity affinity;
        uint32_t cpu = 0;
        while (arguments.read("-c", cpu))
        {
            affinity.cpus.insert(cpu);
        }

        // should animations be automatically played
        auto autoPlay = !arguments.read({"--no-auto-play", "--nop"});

        // demo: animate the sun through a full day (1 simulated hour per second)
        auto sunCycle = !arguments.read("--no-sun-cycle");

        // environment and wave settings, applied to the scene after it is loaded
        // (the scene creates them with reasonable defaults)
        bool overrideSun = false, overrideWind = false, overrideJerlov = false, overrideFog = false;
        double sunElevation = 0.0, sunAzimuth = 0.0, windSpeed = 0.0, windDirection = 0.0, fogDensity = 0.0;
        int jerlov = 0;
        if (arguments.read("--sun", sunElevation, sunAzimuth)) overrideSun = true;
        if (arguments.read("--wind", windSpeed, windDirection)) overrideWind = true;
        if (arguments.read("--jerlov", jerlov)) overrideJerlov = true;
        if (arguments.read("--fog", fogDensity)) overrideFog = true;

        // waves: the mean component wave of the Gazebo wave model (boating_gz / asv_wave_sim)
        const double waveAmplitude = arguments.value(0.4, "--wave-amplitude");
        const double wavePeriod = arguments.value(5.0, "--wave-period");
        const double waveSteepness = arguments.value(0.8, "--wave-steepness");

        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        // take the scene files from the remaining positional arguments, falling back to all example models
        std::vector<vsg::Path> sceneFilenames;
        for (int i = 1; i < argc; ++i) sceneFilenames.push_back(vsg::Path(arguments[i]));

        // build the overall ocean scene: models, sky, lighting and ocean surface
        vsg::ref_ptr<vsgOcean::Scene> vsg_scene;
        if (!sceneFilenames.empty())
        {
            vsg_scene = vsgOcean::Scene::load(sceneFilenames, options);
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
        auto environment = vsg_scene->environment();
        if (overrideSun)
        {
            environment->setSunElevation(sunElevation);
            environment->setSunAzimuth(sunAzimuth);
        }
        if (overrideWind)
        {
            environment->setWindSpeed(windSpeed);
            environment->setWindDirection(windDirection);
        }
        if (overrideJerlov)
        {
            environment->setJerlovType(static_cast<vsgOcean::Environment::JerlovType>(jerlov));
        }
        if (overrideFog)
        {
            environment->setFogDensity(fogDensity);
        }
        auto wavefield = vsg_scene->surface()->wavefield();
        wavefield->setAmplitude(waveAmplitude);
        wavefield->setPeriod(wavePeriod);
        wavefield->setSteepness(waveSteepness);

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

        // create the viewer and assign window(s) to it
        auto viewer = vsg::Viewer::create();
        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cout << "Could not create window." << std::endl;
            return 1;
        }

        viewer->addWindow(window);

        auto ellipsoidModel = vsg_scene->getRefObject<vsg::EllipsoidModel>("EllipsoidModel");

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

            perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio, horizonMountainHeight);
        }
        else
        {
            // compute the bounds of the scene graph to help position camera
            vsg::ComputeBounds computeBounds;
            vsg_scene->accept(computeBounds);

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

            perspective = vsg::Perspective::create(30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio * nearRadius, radius * 10.5);
        }

        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        // add close handler to respond to the close window button and pressing escape
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));

        auto cameraAnimation = vsg::CameraAnimationHandler::create(camera, pathFilename, options);
        viewer->addEventHandler(cameraAnimation);
        if (autoPlay && cameraAnimation->animation)
        {
            cameraAnimation->play();

            if (reportAverageFrameRate && (maxTime == std::numeric_limits<double>::max()) && (numFrames < 0))
            {
                maxTime = cameraAnimation->animation->maxTime();
            }
        }

        // camera controller: horizon-level orbit (left drag), translate (right drag), dolly (wheel)
        viewer->addEventHandler(vsgOcean::HorizonController::create(camera));

        // if required preload specific number of PagedLOD levels.
        if (loadLevels > 0)
        {
            vsg::LoadPagedLOD loadPagedLOD(camera, loadLevels);

            auto startTime = vsg::clock::now();

            vsg_scene->accept(loadPagedLOD);

            auto time = std::chrono::duration<float, std::chrono::milliseconds::period>(vsg::clock::now() - startTime).count();
            std::cout << "No. of tiles loaded " << loadPagedLOD.numTiles << " in " << time << "ms." << std::endl;
        }

        auto view = vsg::View::create(camera);
        view->LODScale = LODScale;
        // the scene carries its own sky, sun-driven lighting and transparent ocean
        // surface, added to the view in the right drawing order
        view->addChild(vsg_scene);

        auto renderGraph = vsg::RenderGraph::create(window, view);
        auto commandGraph = vsg::CommandGraph::create(window, renderGraph);
        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

        if (instrumentation) viewer->assignInstrumentation(instrumentation);

        if (multiThreading)
        {
            viewer->setupThreading();

            if (affinity)
            {
                auto cpu_itr = affinity.cpus.begin();

                // set affinity of main thread
                if (cpu_itr != affinity.cpus.end())
                {
                    std::cout << "vsg::setAffinity() " << *cpu_itr << std::endl;
                    vsg::setAffinity(vsg::Affinity(*cpu_itr++));
                }

                for (auto& thread : viewer->threads)
                {
                    if (thread.joinable() && cpu_itr != affinity.cpus.end())
                    {
                        std::cout << "vsg::setAffinity(" << thread.get_id() << ") " << *cpu_itr << std::endl;
                        vsg::setAffinity(thread, vsg::Affinity(*cpu_itr++));
                    }
                }
            }
        }
        else if (affinity)
        {
            std::cout << "vsg::setAffinity(";
            for (auto cpu_num : affinity.cpus)
            {
                std::cout << " " << cpu_num;
            }
            std::cout << " )" << std::endl;

            vsg::setAffinity(affinity);
        }

        viewer->compile(resourceHints);

        if (maxPagedLOD > 0)
        {
            // set targetMaxNumPagedLODWithHighResSubgraphs after Viewer::compile() as it will assign any DatabasePager if required.
            for (auto& task : viewer->recordAndSubmitTasks)
            {
                if (task->databasePager) task->databasePager->targetMaxNumPagedLODWithHighResSubgraphs = maxPagedLOD;
            }
        }

        if (autoPlay)
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
        while (viewer->advanceToNextFrame() && (numFrames < 0 || (numFrames--) > 0) && (viewer->getFrameStamp()->simulationTime < maxTime))
        {
            // pass any events into EventHandlers assigned to the Viewer
            viewer->handleEvents();

            viewer->update();

            // demo: update the model poses wrt the world model and the link poses wrt their
            // parent frame (the parent is guessed by vsgOcean from the model tree)
            const auto t = viewer->getFrameStamp()->simulationTime;

            if (vsg_scene->model("aquabot"))
            {
                // the surface vehicle performs a large circle on the surface; slightly
                // below z = 0 so the hulls have a visible draft (the mesh waterline is
                // at its origin, the hull bottom 0.026 m above)
                const auto angle = 2.0 * vsg::PI * t / 60.0;
                vsg_scene->setPose("aquabot", vsg::translate(vsg::dvec3(10.0 * std::cos(angle), 10.0 * std::sin(angle), -0.3)) * vsg::rotate(angle + vsg::PI / 2.0, 0.0, 0.0, 1.0));
            }

            if (vsg_scene->model("bluerov2"))
            {
                // the ROV circles below the surface (Z < 0), at its own pace
                const auto angle = -2.0 * vsg::PI * t / 40.0;
                vsg_scene->setPose("bluerov2", vsg::translate(vsg::dvec3(6.0 * std::cos(angle), 6.0 * std::sin(angle), -3.0)) * vsg::rotate(angle + vsg::PI / 2.0, 0.0, 0.0, 1.0));
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

            // demo: the sun moves slowly through the sky, 1 simulated hour per second
            if (sunCycle)
            {
                const auto dayPhase = 2.0 * vsg::PI * std::fmod(t, 24.0) / 24.0;
                environment->setSunElevation(60.0 * std::sin(dayPhase));
            }

            // refresh the sky, sun-driven lighting and ocean surface from the
            // environment, camera and simulation time
            vsg_scene->update(camera, t);

            viewer->recordAndSubmit();

            viewer->present();
        }

        if (reportAverageFrameRate)
        {
            auto fs = viewer->getFrameStamp();
            double fps = static_cast<double>(fs->frameCount) / std::chrono::duration<double, std::chrono::seconds::period>(vsg::clock::now() - viewer->start_point()).count();
            std::cout << "Average frame rate = " << fps << " fps" << std::endl;
        }

        if (reportMemoryStats)
        {
            if (options->sharedObjects)
            {
                vsg::LogOutput output;
                options->sharedObjects->report(output);
            }
        }

        if (auto profiler = instrumentation.cast<vsg::Profiler>())
        {
            instrumentation->finish();
            if (logFilename)
            {
                std::ofstream fout(logFilename);
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
