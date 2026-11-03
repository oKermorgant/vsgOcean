#include <vsg/all.h>

#ifdef vsgXchange_FOUND
#    include <vsgXchange/all.h>
#endif

#include "ExampleOptions.h"
#include "DemoPoses.h"
#include "Headless.h"

#include <vsgOcean/ui/EnvironmentControls.h>
#include <vsgOcean/scene/RenderPipeline.h>
#include <vsgOcean/ui/EnvironmentGui.h>
#include <vsgOcean/ui/HorizonController.h>
#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/semantic/SemanticOverlay.h>
#include <vsgOcean/semantic/SemanticPalette.h>

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
            vsg_scene = vsgOcean::Scene::load({modelFile("islands/islands.sdf"), modelFile("wamv/wamv.sdf"),
                                               modelFile("bluerov2/bluerov2.sdf"), modelFile("herkules_ship_wreck/model.sdf"), modelFile("floatgen/model.sdf")}, options);
        }
#else
        else
        {
            // degraded demo without SDF support: load the raw terrain and hull meshes as named
            // models, so the vehicle trajectories below still have something to animate
            vsg_scene = vsgOcean::Scene::load({modelFile("islands/islands.glb"),
                                               modelFile("wamv/mesh/wamv_base.glb"),
                                               modelFile("bluerov2/meshes/bluerov2_noprop.glb")}, options);
        }
#endif

        // apply the command line environment / wave settings to the scene; the
        // saved environment file goes first, so every explicit flag below overrides
        // its key (an absent flag leaves the file's value in place)
        const auto environment = vsg_scene->environment();
        if (!settings.environmentFile.empty() && !vsg_scene->loadEnvironment(settings.environmentFile))
        {
            std::cerr << "cannot load the environment file " << settings.environmentFile << std::endl;
            return 1;
        }
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
            environment->setJerlovType(static_cast<vsgOcean::Environment::JerlovType>(std::clamp(settings.jerlov, 0, static_cast<int>(vsgOcean::Environment::jerlovTypeCount) - 1)));
        }
        if (settings.overrideWater)
        {
            // the options given override the fields of the selected type
            auto water = environment->waterComposition();
            if (settings.water.chlorophyll != vsgOcean::WaterComposition{}.chlorophyll) water.chlorophyll = settings.water.chlorophyll;
            if (settings.water.cdom != vsgOcean::WaterComposition{}.cdom) water.cdom = settings.water.cdom;
            if (settings.water.sediment != vsgOcean::WaterComposition{}.sediment) water.sediment = settings.water.sediment;
            environment->setWaterComposition(water);
        }
        if (settings.overrideFog)
        {
            environment->setFogDensity(settings.fogDensity);
        }
        if (settings.overrideSnow)
        {
            environment->setMarineSnowDensity(settings.snowDensity);
        }
        if (settings.overrideClouds)
        {
            environment->setCloudiness(settings.cloudiness);
        }
        if (settings.overrideRain)
        {
            environment->setRainRate(settings.rainRate);
        }
        if (settings.overrideBlur)
        {
            environment->setUnderwaterBlur(settings.underwaterBlur);
        }
        if (settings.overrideCurrent)
        {
            environment->setWaterCurrent(settings.waterCurrent);
        }
        const auto wavefield = vsg_scene->surface()->wavefield();
        if (settings.overrideWaves) // no wave flag: keep the wave field as loaded
        {
            wavefield->setAmplitude(settings.waveAmplitude);
            wavefield->setPeriod(settings.wavePeriod);
            wavefield->setSteepness(settings.waveSteepness);
            wavefield->setNumber(static_cast<size_t>(settings.waveNumber));
            wavefield->setScale(settings.waveScale);
            wavefield->setAngle(vsg::radians(settings.waveAngle));
        }
        // the Jacobian foam amount applies to BOTH sea models (the component
        // sea's foam reads the analytic Jacobian, see ocean.frag) - it used to
        // ride the --fft block only, silently disabling --foam on the default
        // component sea
        wavefield->setFoam(settings.foam);
        if (settings.fft)
        {
            auto fft = wavefield->fftParameters();
            fft.tileSize = settings.fftTile;
            fft.choppiness = settings.fftChoppiness;
            fft.capOmegaC = settings.fftWaveAge;
            wavefield->setFftParameters(fft);
            wavefield->setModel(vsgOcean::Wavefield::Model::FFT);
            wavefield->setFftDetail(settings.fftDetail);
        }

        // geolocation of the scene: taken from the SDF world when available,
        // Pornichet (France) hard-coded otherwise; --latitude/--longitude override
        // (a negative latitude is the southern hemisphere - the southern
        // constellations replace the northern ones in the night sky)
        if (environment->latitude() == 0.0 && environment->longitude() == 0.0)
        {
            environment->setLatitude(47.1075);
            environment->setLongitude(-2.3533);
        }
        if (settings.overrideLatitude) environment->setLatitude(settings.latitude);
        if (settings.overrideLongitude) environment->setLongitude(settings.longitude);

        vsg::info("Environment: sun elevation ", environment->sunElevation(), " deg, azimuth ", environment->sunAzimuth(),
                  " deg; wind ", environment->windSpeed(), " m/s from ", environment->windDirection(), " deg; water type ",
                  environment->jerlovName(), "; fog density ", environment->fogDensity(), "; location ", environment->latitude(),
                  " lat, ", environment->longitude(), " lon");

        // per-lamp dimming below the SDF ceilings (the lights widget's sliders
        // and the same API a robot driver would use)
        for (const auto& [name, factor] : settings.lightFactors)
        {
            vsg::info("Dimming light ", name, " to ", factor, " of its SDF intensity");
            vsg_scene->setLightIntensity(name, factor);
        }

        // the lamps clustered into the models' direction groups (one control
        // per robot face - the [L] widget's sliders)
        for (const auto& group : vsg_scene->lightGroupNames())
        {
            std::string members;
            for (const auto& lamp : vsg_scene->lightGroupMembers(group)) members += (members.empty() ? "" : ", ") + lamp;
            vsg::info("Light group ", group, ": ", members);
        }

        // demo: links animated wrt their parent frame (steering pods of the surface vehicle,
        // spinning propellers and thrusters). getJoint() composes their motion on the poses
        // captured from the SDF files at load time.
        vsgOceanExample::DemoPoses demoPoses;
        demoPoses.capture(*vsg_scene);

        // the sensor pipelines (created in the windowed path below); the demo animation
        // references their cameras, so they are declared here and stay null in headless
        vsg::ref_ptr<vsgOcean::RenderPipeline> rovPipeline, boatPipeline;

        // the floating turbine's heave state lives in the pose computation header
        // (DemoPoses.h), with the rest of the demo kinematics

        // the demo animation: the model poses wrt the world model and the link poses
        // wrt their parent frame, computed by DemoPoses (the seam where a dynamics
        // simulator will plug in). Shared by the windowed and the headless loops.
        const auto demoFrame = [&](double t)
        {
            // the models' world poses
            for (const auto* model : {"wamv", "bluerov2", "floatgen"})
            {
                if (vsg_scene->model(model))
                {
                    if (auto pose = demoPoses.getPose(model, *wavefield, t)) vsg_scene->setPose(model, *pose);
                }
            }

            // the boat camera rides at the bow of the surface vehicle, above the
            // roofline of the central pod (the pod's front edge reaches ~2.2 m - a
            // lower camera sees the roof in the lower half of the frame), looking
            // along the direction of travel: the vehicle's +X is its heading
            if (boatPipeline && vsg_scene->model("wamv"))
            {
                if (auto pose = demoPoses.getPose("wamv", *wavefield, t))
                {
                    const auto lookAt = boatPipeline->camera()->viewMatrix.cast<vsg::LookAt>();
                    const vsg::dvec3 position = (*pose) * vsg::dvec3(0.0, 0.0, 0.0);
                    const vsg::dvec3 travel = vsg::normalize((*pose) * vsg::dvec3(1.0, 0.0, 0.0) - position);
                    lookAt->eye = position + travel * 2.1 + vsg::dvec3(0.0, 0.0, 2.4);
                    lookAt->center = lookAt->eye + travel * 8.0 + vsg::dvec3(0.0, 0.0, -0.5);
                }
            }

            // the ROV camera is forward-facing, at the vehicle's nose: it looks at
            // the wreck it circles, without the vehicle in view
            if (rovPipeline && vsg_scene->model("bluerov2"))
            {
                if (auto pose = demoPoses.getPose("bluerov2", *wavefield, t))
                {
                    auto attitude = *pose;
                    attitude[3] = vsg::dvec4(0.0, 0.0, 0.0, 1.0); // strip the translation: the rotation only
                    const auto lookAt = rovPipeline->camera()->viewMatrix.cast<vsg::LookAt>();
                    lookAt->eye = (*pose) * vsg::dvec3(0.0, 0.0, 0.0) + attitude * vsg::dvec3(0.55, 0.0, 0.15);
                    lookAt->center = lookAt->eye + (attitude * vsg::dvec3(1.0, 0.0, 0.0)) * 8.0;
                }
            }

            // the internal frames' joint poses
            for (const auto& [model, joint] : vsgOceanExample::DemoPoses::animatedJoints())
            {
                if (auto pose = demoPoses.getJoint(model, joint, t)) vsg_scene->setPose(model + "/" + joint, *pose);
            }
        };

        // --showcase <kind> (headless + --video): scripted maritime videos on a
        // deterministic 60 fps clock. "surface": high over the sea, the wind
        // building to a foam-capped FFT sea, then the Jerlov water types cycling
        // (~5 s each). "underwater": tracking the ROV from behind at the wreck,
        // the water type stepping clearer while the marine snow varies. "rov":
        // the ROV's own nose camera over the same underwater script - render it
        // alongside "underwater" and overlay it as a picture-in-picture. The demo
        // animation runs on the SAME showcase clock, so the two runs match.
        // "semantic": the annotation tour - a travelling approach to the WAM-V,
        // the semantic view, the descent to the ROV at the wreck, then the
        // semantic view tracking it (the [R] cycle as a scripted shot).
        const auto showcaseFrame = [&environment, &wavefield, &demoPoses, &settings, &vsg_scene](double t, vsg::Camera* camera) -> int
        {
            const auto ease = [](double u) { u = std::clamp(u, 0.0, 1.0); return u * u * (3.0 - 2.0 * u); };
            vsg::dvec3 eye{0.0, -30.0, 5.0}, center{0.0, 0.0, 0.0};
            int semanticMode = -1; // -1: the tour does not script the semantic view

            if (settings.showcaseKind == "semantic")
            {
                // the annotation tour: a photo travelling to the WAM-V, the
                // semantic view at the boat, a photo travel down to the ROV
                // (crossing the surface), then the semantic view tracking the
                // ROV at the wreck - the [R] cycle as a scripted shot
                environment->setSunElevation(35.0);
                environment->setSunAzimuth(135.0);
                environment->setCloudiness(2.0);
                environment->setMarineSnowDensity(10.0);
                // the ROV's dive lamps at a quarter of their ceiling (as the
                // underwater tour: full power cuts the murk and reads "blind")
                if (t < 1.0 / 60.0)
                {
                    for (const auto& group : vsg_scene->lightGroupNames())
                    {
                        if (group.rfind("bluerov2/", 0) == 0) vsg_scene->setLightGroupIntensity(group, 0.25);
                    }
                }

                vsg::dvec3 rovEye{0.0, 0.0, -35.0}, rovCenter{0.0, 0.0, -37.0};
                if (const auto pose = demoPoses.getPose("bluerov2", *wavefield, t))
                {
                    const vsg::dvec3 position = (*pose) * vsg::dvec3(0.0, 0.0, 0.0);
                    const vsg::dvec3 forward = vsg::normalize((*pose) * vsg::dvec3(1.0, 0.0, 0.0) - position);
                    // behind the vehicle at 45 deg, as the underwater tour
                    const auto c = std::cos(vsg::radians(225.0)), s = std::sin(vsg::radians(225.0));
                    const vsg::dvec3 behind45 = vsg::normalize(vsg::dvec3(forward.x * c - forward.y * s,
                                                                          forward.x * s + forward.y * c, 0.0));
                    rovEye = position + behind45 * vsg::mix(25.0, 12.0, ease((t - 8.5) / 3.5)) + vsg::dvec3(0.0, 0.0, 2.0);
                    rovCenter = position + vsg::dvec3(0.0, 0.0, -1.0);
                }

                vsg::dvec3 boatEye{0.0, -30.0, 8.0}, boatCenter{0.0, 0.0, 0.5};
                if (const auto pose = demoPoses.getPose("wamv", *wavefield, t))
                {
                    boatCenter = (*pose) * vsg::dvec3(0.0, 0.0, 0.5);
                    boatEye = boatCenter + vsg::dvec3(0.0, 0.0, 1.5);
                }

                if (t < 4.0)
                {
                    // the approach: closing from 45 m to 14 m south of the boat
                    semanticMode = 0;
                    eye = boatEye + vsg::dvec3(0.0, -31.0 * (1.0 - ease(t / 4.0)) - 14.0, 4.0);
                    center = boatCenter;
                }
                else if (t < 6.0)
                {
                    // the annotation of the surface vehicle, still closing gently
                    semanticMode = 1;
                    eye = boatEye + vsg::dvec3(0.0, -14.0 + 4.0 * ease((t - 4.0) / 2.0), 3.0);
                    center = boatCenter;
                }
                else if (t < 8.5)
                {
                    // the descent to the ROV: the eye arcs down from the boat
                    // to the trailing viewpoint behind the vehicle, crossing
                    // the surface (the composite turns underwater on its own)
                    semanticMode = 0;
                    const auto u = ease((t - 6.0) / 2.5);
                    eye = vsg::mix(boatEye + vsg::dvec3(0.0, -10.0, 3.0), rovEye, u);
                    center = vsg::mix(boatCenter, rovCenter, ease((t - 6.0) / 1.5));
                }
                else
                {
                    // the annotation of the underwater vehicle, the tracking shot
                    semanticMode = 1;
                    eye = rovEye;
                    center = rovCenter;
                }
            }
            else if (settings.showcaseKind == "surface")
            {
                // the sea: the wind builds over the first 5 s to a whitecapped FFT
                // sea (choppiness 1.2 folds the crests, the Jacobian foam shows)
                if (t < 1.0 / 60.0)
                {
                    auto fft = wavefield->fftParameters();
                    fft.choppiness = 1.2;
                    wavefield->setFftParameters(fft);
                    wavefield->setFftDetail(true);
                    wavefield->setModel(vsgOcean::Wavefield::Model::FFT);
                }
                // the wind: calm at the start, then four 10 s steps up to 13 m/s
                // (a ~2 s ease at each boundary so the spectrum change reads as a
                // step, not a pop); the foam follows the top of the range
                static const double windStops[]{0.0, 13.0 / 3.0, 26.0 / 3.0, 13.0};
                const auto step = std::min(static_cast<size_t>(t / 10.0), std::size(windStops) - 1);
                const auto blend = ease(std::fmod(t, 10.0) / 2.0);
                const auto wind = step == 0 ? windStops[0] : vsg::mix(windStops[step - 1], windStops[step], blend);
                environment->setWindSpeed(wind);
                environment->setWindDirection(200.0);
                wavefield->setFoam(0.7 * std::clamp((wind - 9.0) / 4.0, 0.0, 1.0));
                environment->setCloudiness(3.0); // broken cloud: softens the glitter without greying the sea out
                environment->setMarineSnowDensity(0.0);

                // once the wind is up: the water types, 5 s each - a monotonic
                // sweep from the clear open ocean to the murkiest coastal (the
                // body colour, the sky mirror and the foam lighting all follow)
                static const int jerlovStops[]{0, 2, 3, 4, 6, 8, 10, 13};
                if (const auto stop = static_cast<size_t>(t / 5.0); stop < std::size(jerlovStops))
                {
                    environment->setJerlovType(static_cast<vsgOcean::Environment::JerlovType>(jerlovStops[stop]));
                }

                // the camera: low over the middle of the bay channel, drifting
                // slowly away from the islands - the water fills most of the
                // frame, the shores ride the edges, the sun sits 20 deg left of
                // the view axis (over the channel) so its glare stays at the
                // frame edge, softened by the clouds
                environment->setSunElevation(25.0);
                environment->setSunAzimuth(80.0);
                eye = vsg::dvec3{0.0, -80.0 + 8.0 * t, 12.0};
                center = eye + vsg::dvec3{110.0 * std::cos(vsg::radians(100.0)), 110.0 * std::sin(vsg::radians(100.0)), -12.0};
            }
            else // "underwater" (the tracking shot) and "rov" (its camera)
            {
                // the environment: a bright day; the water type steps MURKIER
                // over the video (oceanic I -> type II -> coastal 3 -> coastal 9)
                // while the marine snow plays - a bloom, a lull, then a thick
                // storm; the ROV's dive lamps are dimmed to a quarter of their
                // SDF ceiling (full power cuts the murk and reads "blind")
                static const int jerlovStops[]{0, 3, 7, 13};
                const auto stop = std::min(static_cast<size_t>(t / 6.0), std::size(jerlovStops) - 1);
                environment->setJerlovType(static_cast<vsgOcean::Environment::JerlovType>(jerlovStops[stop]));
                double snow;
                if (t < 8.0) snow = vsg::mix(10.0, 16.0, ease(t / 8.0));
                else if (t < 16.0) snow = vsg::mix(16.0, 4.0, ease((t - 8.0) / 8.0));
                else snow = vsg::mix(4.0, 40.0, ease((t - 16.0) / 8.0));
                environment->setMarineSnowDensity(snow);
                environment->setSunElevation(40.0);
                environment->setSunAzimuth(135.0);
                // the ROV's dive lamps are dimmed to a quarter of their SDF
                // ceiling (full power cuts the murk and reads "blind"): the two
                // lamps share ONE control - they aim within 45 deg of each
                // other, so they form a single light group
                if (t < 1.0 / 60.0)
                {
                    for (const auto& group : vsg_scene->lightGroupNames())
                    {
                        if (group.rfind("bluerov2/", 0) == 0) vsg_scene->setLightGroupIntensity(group, 0.25);
                    }
                }

                // the ROV circles the wreck nose-first: the "rov" camera sits at
                // the vehicle's nose (the sensor view); the tracking camera makes
                // its approach from 45 deg behind the ROV, closing from ~32 to
                // ~18 m, so the vehicle and the wreck beyond it share the frame
                const auto pose = demoPoses.getPose("bluerov2", *wavefield, t);
                if (pose)
                {
                    const vsg::dvec3 position = (*pose) * vsg::dvec3(0.0, 0.0, 0.0);
                    const vsg::dvec3 forward = vsg::normalize((*pose) * vsg::dvec3(1.0, 0.0, 0.0) - position);
                    if (settings.showcaseKind == "rov")
                    {
                        auto attitude = *pose;
                        attitude[3] = vsg::dvec4(0.0, 0.0, 0.0, 1.0); // the rotation only
                        eye = position + attitude * vsg::dvec3(0.55, 0.0, 0.15);
                        center = eye + (attitude * vsg::dvec3(1.0, 0.0, 0.0)) * 8.0;
                    }
                    else
                    {
                        // the horizontal direction 225 deg around from the heading
                        // (behind and to the side), the trailing viewpoint,
                        // closing from 30 to 15 m; the center sits on the vehicle
                        // so it, its lamp cones on the wreck and the snow carry
                        // the frame (the open-water background is dark at depth)
                        const auto c = std::cos(vsg::radians(225.0)), s = std::sin(vsg::radians(225.0));
                        const vsg::dvec3 behind45 = vsg::normalize(vsg::dvec3(forward.x * c - forward.y * s,
                                                                              forward.x * s + forward.y * c, 0.0));
                        const auto distance = vsg::mix(30.0, 15.0, ease(t / 24.0));
                        eye = position + behind45 * distance + vsg::dvec3(0.0, 0.0, 2.0);
                        center = position + vsg::dvec3(0.0, 0.0, -1.0);
                    }
                }
            }

            const auto lookAt = camera->viewMatrix.cast<vsg::LookAt>();
            lookAt->eye = eye;
            lookAt->center = center;
            lookAt->up = vsg::dvec3(0.0, 0.0, 1.0);
            return semanticMode;
        };

        // headless: render offscreen (a hardware GPU when the render group allows it,
        // no window or swapchain) and write the screenshot - over ssh -X the hardware
        // cannot present, but the offscreen rendering is ~50x faster than software
        if (settings.headless)
        {
            // the showcase runs on its own deterministic clock (60 frames per tour
            // second), NOT the viewer's simulationTime: headless frames render much
            // faster than real time, and the wall-clock t compresses the whole tour
            // into the first seconds. The DEMO POSES run on the same clock, so the
            // "underwater" and "rov" runs (rendered as two videos) match exactly
            double showcaseT = 0.0;
            return vsgOcean::Headless::run(vsg_scene, settings,
                                           [&](double t, vsg::Camera* camera) -> int
                                           {
                                               if (settings.showcase)
                                               {
                                                   demoFrame(showcaseT);
                                                   const auto mode = showcaseFrame(showcaseT, camera);
                                                   showcaseT += 1.0 / 60.0;
                                                   return mode;
                                               }
                                               demoFrame(t);
                                               return -1; // the semantic view follows the flags / the R key
                                           });
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
            if (auto boat = vsg_scene->model("wamv"))
            {
                vsg::ComputeBounds boatBounds;
                boat->accept(boatBounds);

                if (boatBounds.bounds.valid())
                {
                    const vsg::dvec3 boatCentre = (boatBounds.bounds.min + boatBounds.bounds.max) * 0.5;
                    const double boatRadius = vsg::length(boatBounds.bounds.max - boatBounds.bounds.min) * 0.6;

                    lookAt = vsg::LookAt::create(boatCentre + vsg::dvec3(0.0, -boatRadius * 3.5, boatRadius * 1.5), boatCentre, vsg::dvec3(0.0, 0.0, 1.0));
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
        // the R key belongs to the semantic annotation view (see the overlay
        // wiring below); a camera path is recorded through the handler's API
        cameraAnimation->toggleRecordingKey = vsg::KEY_Undefined;
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
        controls->semanticMode = settings.semanticOverlay ? 2 : (settings.semanticView ? 1 : 0);
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
            // near 0.3: a vehicle camera has nothing relevant closer than that,
            // and it clips any own-structure slivers that graze the mount;
            // far 2 km: the islands must be in frame, not just the near field
            return vsg::Camera::create(vsg::Perspective::create(60.0, 4.0 / 3.0, 0.3, 2000.0),
                                       vsg::LookAt::create(vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0)),
                                       vsg::ViewportState::create(sensorExtent));
        };

        if (vsg_scene->model("bluerov2")) rovPipeline = vsgOcean::RenderPipeline::create(vsg_scene, device, makeSensorCamera(), sensorExtent, colorFormat, depthFormat);
        if (vsg_scene->model("wamv")) boatPipeline = vsgOcean::RenderPipeline::create(vsg_scene, device, makeSensorCamera(), sensorExtent, colorFormat, depthFormat);

        std::vector<std::pair<std::string, vsg::ref_ptr<vsg::ImageView>>> cameraViews;
        if (rovPipeline) cameraViews.emplace_back("ROV camera (underwater)", rovPipeline->outputView());
        if (boatPipeline) cameraViews.emplace_back("boat camera (surface)", boatPipeline->outputView());

        // the semantic annotation pass: the models rendered as per-pixel IDs (the
        // ground truth for dataset generation - capture() reads it back). Built by
        // default so the R key can bring up its display at any time; while the
        // semantic view is off the pass renders nothing (masked out of the record)
        auto semanticPass = vsgOcean::SemanticPass::create(vsg_scene, device, camera, window->extent2D());
        if (semanticPass->modelNames().empty()) semanticPass = {}; // nothing to annotate
        auto semanticOverlay = semanticPass ? vsgOcean::SemanticOverlay::create(semanticPass, window->extent2D()) : nullptr;

        // the sensor cameras carry their own annotation: the same semantic pass
        // from their camera, drawn over their composited image, so the R key
        // switches the previews to the semantic view as well
        std::vector<vsg::ref_ptr<vsgOcean::SemanticPass>> sensorSemantic;
        std::vector<vsg::ref_ptr<vsgOcean::SemanticOverlay>> sensorSemanticOverlay;
        for (const auto& pipeline : {rovPipeline, boatPipeline})
        {
            auto pass = pipeline ? vsgOcean::SemanticPass::create(vsg_scene, device, pipeline->camera(), sensorExtent) : nullptr;
            if (pass && pass->modelNames().empty()) pass = {};
            if (pass)
            {
                auto sensorOverlay = vsgOcean::SemanticOverlay::create(pass, sensorExtent);
                pipeline->compositeRenderGraph()->addChild(sensorOverlay);
                sensorSemantic.push_back(pass);
                sensorSemanticOverlay.push_back(sensorOverlay);
            }
        }


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
        auto commandGraph = vsgOcean::createCommandGraph(window, viewer, mainPipeline, overlay,
                                                         semanticOverlay ? vsg::ref_ptr<vsg::Node>(semanticOverlay) : nullptr);
        // the semantic pass renders alongside the scene passes (its draws are
        // masked out of the record while the semantic view is off)
        if (semanticPass) commandGraph->children.insert(commandGraph->children.end() - 1, semanticPass->renderGraph());

        // the sensor pipelines render into the same command graph, before the window
        // pass (their composited images are sampled by the GUI previews). Their draws
        // are masked out each frame while the previews are off screen (see the loop)
        std::vector<vsg::ref_ptr<vsg::RenderGraph>> sensorGraphs;
        for (const auto& pass : sensorSemantic) sensorGraphs.push_back(pass->renderGraph());
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
        uint64_t lastCycleFrame = 0;
        while ((settings.numFrames < 0 || settings.numFrames > 0) &&
               viewer->advanceToNextFrame() &&
               viewer->getFrameStamp()->simulationTime < settings.maxTime)
        {
            if (settings.numFrames > 0) --settings.numFrames;

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
            // the previews are off screen. The camera widget is INDEPENDENT of the
            // tuning panel (it sits in the corner, gated by the V key alone)
            const bool previewsVisible = guiBuilt && controls->camerasVisible;
            if (rovPipeline) rovPipeline->setEnabled(previewsVisible);
            if (boatPipeline) boatPipeline->setEnabled(previewsVisible);
            if (rovPipeline) rovPipeline->update(vsg_scene, rovPipeline->camera().get());
            if (boatPipeline) boatPipeline->update(vsg_scene, boatPipeline->camera().get());

            // the semantic annotation view (the R key): the pass renders only
            // while a semantic mode is on, the overlay culls its triangle while
            // off - no per-frame cost (--semantic-cycle: the same steps
            // hands-free, for recordings and debugging the mode cycle)
            if (settings.semanticCycleFrames > 0)
            {
                // one advance per N-frame boundary, not one per frame within it
                const auto frame = viewer->getFrameStamp()->frameCount;
                if (frame > 0 && frame % settings.semanticCycleFrames == 0 && frame != lastCycleFrame)
                {
                    lastCycleFrame = frame;
                    controls->semanticMode = (controls->semanticMode + 1) % 3;
                }
            }
            if (semanticPass) semanticPass->setEnabled(controls->semanticMode != 0);
            if (semanticOverlay) semanticOverlay->setMode(controls->semanticMode);
            if (controls->semanticMode != 0 && semanticPass) semanticPass->update();

            // the sensor cameras' annotation follows the same mode, and renders
            // only while the previews are on screen (like the sensor pipelines)
            for (const auto& overlay : sensorSemanticOverlay) overlay->setMode(previewsVisible ? controls->semanticMode : 0);
            for (const auto& pass : sensorSemantic)
            {
                pass->setEnabled(previewsVisible && controls->semanticMode != 0);
                if (previewsVisible && controls->semanticMode != 0) pass->update();
            }

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
