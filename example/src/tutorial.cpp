// vsgOcean tutorial: the smallest complete marine scene.
//
// A window, an ocean scene, a camera, and a vehicle riding the waves - the
// whole loop in one readable main, no command line, no GUI plumbing. Every
// block below is a step; the vsgOcean_example application is this file plus
// the full feature set (widgets, sensors, semantic views, showcases).
//
// The pieces, in order:
//   1. a window (vsg itself, nothing vsgOcean-specific)
//   2. the scene - SDF worlds and models load natively
//   3. the environment - every layer is runtime-tunable, here from the code
//   4. a camera framed on the scene
//   5. the render pipeline (the underwater composite comes with it)
//   6. the loop - refresh the scene, drive your models, render

#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/scene/RenderPipeline.h>
#include <vsgOcean/scene/RenderTarget.h>
#include <vsgOcean/ui/EnvironmentControls.h>
#include <vsgOcean/ui/HorizonController.h>

#include <vsg/app/Camera.h>
#include <vsg/app/Window.h>
#include <vsg/app/Viewer.h>
#include <vsg/io/Options.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsgXchange/all.h>

#include <cmath>

#ifdef VSGOCEAN_EXAMPLE_MODELS_DIR
inline std::string modelFile(const std::string& path) { return VSGOCEAN_EXAMPLE_MODELS_DIR "/" + path; }
#else
inline std::string modelFile(const std::string& path) { return "models/" + path; }
#endif

int main()
{
    // -- 1. the window -------------------------------------------------------
    // a plain vsg viewer and window; vsgOcean renders into any of them
    auto viewer = vsg::Viewer::create();
    auto window = vsg::Window::create(vsg::WindowTraits::create(1280, 720, "vsgOcean tutorial"));
    if (!window) return 1;
    viewer->addWindow(window);

    // the io options: vsgXchange's readers load the meshes (glTF here)
    auto options = vsg::Options::create();
#ifdef vsgXchange_all
    options->add(vsgXchange::all::create());
#endif

    // -- 2. the scene --------------------------------------------------------
    // SDF worlds and models load natively (when vsgOcean is built with sdformat):
    // the islands terrain, a surface vehicle, the rest of the demo fleet if you
    // want it. Raw meshes (glTF, OBJ...) load too, as plain named models.
#ifdef VSGOCEAN_WITH_SDF
    auto scene = vsgOcean::Scene::load({modelFile("islands/islands.sdf"), modelFile("wamv/wamv.sdf")}, options);
#else
    auto scene = vsgOcean::Scene::load({modelFile("islands/islands.glb"), modelFile("wamv/mesh/wamv_base.glb")}, options);
#endif
    if (!scene) return 1;

    // -- 3. the environment --------------------------------------------------
    // everything the sky, the weather, the water and the waves expose is a
    // setter on the environment or the wave field - the same knobs the example
    // binds to its widgets. A taste:
    auto environment = scene->environment();
    environment->setWindSpeed(8.0);            // the sea state follows the wind
    environment->setWindDirection(45.0);
    environment->setCloudiness(3.0);           // oktas
    // environment->setJerlovType(vsgOcean::Environment::JerlovType::Coastal5);
    // environment->setSunElevation(25.0);     // a low, warm sun
    // scene->surface()->wavefield()->setModel(vsgOcean::Wavefield::Model::FFT);

    // -- 4. the camera --------------------------------------------------------
    // frame the scene bounds: any vsg camera works, this is the usual recipe
    vsg::ComputeBounds computeBounds;
    scene->accept(computeBounds);
    const vsg::dvec3 centre = (computeBounds.bounds.min + computeBounds.bounds.max) * 0.5;
    const double radius = vsg::length(computeBounds.bounds.max - computeBounds.bounds.min) * 0.5;
    auto lookAt = vsg::LookAt::create(centre + vsg::dvec3(0.0, -radius * 3.5, radius * 0.8), centre, vsg::dvec3(0.0, 0.0, 1.0));
    auto camera = vsg::Camera::create(vsg::Perspective::create(30.0, 1.778, 0.05, radius * 10.0), lookAt,
                                      vsg::ViewportState::create(window->extent2D()));

    // -- 5. the render pipeline ------------------------------------------------
    // one pipeline per camera: the scene pass, the underwater composite (the
    // per-pixel Jerlov transport), and the present pass to the window. The
    // command graph wires them into the viewer; the underwater composite must
    // reach the scene for it to apply.
    auto pipeline = vsgOcean::RenderPipeline::create(scene, window, camera);
    scene->setUnderwater(pipeline->composite());
    viewer->assignRecordAndSubmitTaskAndPresentation({vsgOcean::createCommandGraph(window, viewer, pipeline)});

    // the input handlers: the window close, the environment keys (the sun
    // cycle, the widget toggles - S sky, O ocean surface, U underwater...),
    // and the camera controls (left drag translate, right drag orbit around
    // a level horizon, wheel dolly)
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));
    viewer->addEventHandler(vsgOcean::EnvironmentControls::create(scene));
    viewer->addEventHandler(vsgOcean::HorizonController::create(camera));

    if (!viewer->compile()) return 1;

    // -- 6. the loop ------------------------------------------------------------
    // every frame: hand vsg the events, refresh the scene from the environment
    // (the sky, the lighting, the waves - pass the frame's simulation time),
    // then drive your models and render. THIS is the seam a simulator plugs
    // into: poses come from the outside through setPose.
    while (viewer->advanceToNextFrame())
    {
        viewer->handleEvents();
        const auto t = viewer->getFrameStamp()->simulationTime;

        scene->update(camera.get(), t);

        // drive the boat: the ocean surface height at (x, y, t) - waves and sea
        // level composed - places it on the water; setPose moves the whole
        // model (its frames are addressable individually the same way, by
        // "model/frame" name)
        const double x = 10.0 * std::cos(2.0 * vsg::PI * t / 60.0);
        const double y = 10.0 * std::sin(2.0 * vsg::PI * t / 60.0);
        const double z = scene->surfaceHeight(x, y, t);
        scene->setPose("wamv", vsg::translate(vsg::dvec3(x, y, z)) * vsg::rotate(2.0 * vsg::PI * t / 60.0 + vsg::PI / 2.0, 0.0, 0.0, 1.0));

        viewer->recordAndSubmit();
        viewer->present();
    }
    return 0;
}
