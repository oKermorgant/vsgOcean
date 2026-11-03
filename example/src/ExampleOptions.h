// vsgOcean example: the command line settings, parsing and the objects they
// configure (window traits, vsg options, instrumentation), all in one place so
// main() reads as the scene setup and the render loop only.
#pragma once

#include <vsg/all.h>

#ifdef vsgXchange_FOUND
#    include <vsgXchange/all.h>
#endif

#include <vsgOcean/core/WaterOptics.h>

#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace vsgOceanExample
{
    // The settings of the example: everything the command line controls, with the
    // defaults. The environment and wave settings are applied to the scene after
    // it is loaded (the scene creates them with reasonable defaults of its own).
    struct Options
    {
        // window, io and instrumentation, configured by the arguments
        vsg::ref_ptr<vsg::WindowTraits> windowTraits;
        vsg::ref_ptr<vsg::Options> options;
        vsg::ref_ptr<vsg::ResourceHints> resourceHints;
        vsg::ref_ptr<vsg::Instrumentation> instrumentation;
        vsg::Affinity affinity;
        vsg::Path logFilename;

        // run control
        bool quit = false; // an early exit was requested (or the arguments were invalid)
        int exitCode = 0;
        int numFrames = -1;
        double maxTime = std::numeric_limits<double>::max();
        bool multiThreading = false;
        bool reportAverageFrameRate = false;
        bool reportMemoryStats = false;
        bool autoPlay = true;
        int loadLevels = 0;
        int maxPagedLOD = 0;
        vsg::Path pathFilename; // camera animation path file

        // camera
        double nearFarRatio = 0.001;
        double horizonMountainHeight = 0.0;
        bool overrideEye = false;
        vsg::dvec3 eye{0.0, 0.0, 0.0};
        bool overrideCenter = false;
        vsg::dvec3 center{0.0, 0.0, 0.0};

        // marine snow / water current overrides
        bool overrideSnow = false;
        double snowDensity = 0.0;
        bool overrideClouds = false;
        bool overrideBlur = false;
        bool underwaterBlur = false;
        double cloudiness = 0.0;
        bool overrideRain = false;
        double rainRate = 0.0;
        bool overrideCurrent = false;
        vsg::vec3 waterCurrent{0.0f, 0.0f, 0.0f};

        // headless offscreen rendering (no window / swapchain): renders the frames
        // and writes the screenshot; prefers a hardware GPU (the render group)
        bool headless = false;
        VkExtent2D headlessExtent{1280, 1024};

        // demo animation: with --sun-cycle the sun moves through a full day (one
        // simulated hour per second), from the sun pose given by --sun; off by
        // default, and paused at runtime through the environment controls
        bool sunCycle = false;

        // environment overrides
        bool overrideSun = false;
        double sunElevation = 0.0, sunAzimuth = 0.0;
        bool overrideWind = false;
        double windSpeed = 0.0, windDirection = 0.0;
        bool overrideJerlov = false;
        int jerlov = 0;
        bool overrideWater = false; // --chlorophyll / --cdom / --sediment: a custom water composition
        ::vsgOcean::WaterComposition water;
        bool overrideFog = false;
        double fogDensity = 0.0;

        // a saved environment file (E in the GUI exports it): applied first, so any
        // explicit command line environment flag below overrides its key
        vsg::Path environmentFile;

        // geolocation (negative latitude = the southern hemisphere: the southern
        // constellations replace the northern ones)
        bool overrideLatitude = false;
        double latitude = 0.0;
        bool overrideLongitude = false;
        double longitude = 0.0;

        // waves: the mean component wave of the Gazebo wave model (asv_wave_sim);
        // the component count, scale and angle follow asv_wave_sim's defaults, so
        // the sea is irregular and short-crested rather than a single wave train.
        // Applied only when a wave flag is given (else a loaded environment file
        // would be overwritten by these defaults); ungiven flags use the default.
        bool overrideWaves = false;
        double waveAmplitude = 0.1;
        double wavePeriod = 5.0;
        double waveSteepness = 1.0;
        int waveNumber = 8;
        double waveScale = 2.0;
        double waveAngle = 36.0;
        // the FFT sea (--fft): asv_wave_sim linear random FFT waves driven by the wind
        bool fft = false;
        double fftTile = 256.0;
        double fftChoppiness = 0.6;
        double fftWaveAge = 0.84;
        bool fftDetail = false;  // --fft-detail: a second, finer cascade
        double foam = 0.0;       // --foam amount in [0, 1]

        // capture: write the main view to a PNG file when the render loop ends
        // (combine with numFrames to capture a deterministic frame)
        vsg::Path screenshotPath;

        // start with the semantic annotation view on (the R key cycles the
        // modes interactively; these flags are the deterministic way to
        // screenshot or record an annotated view)
        bool semanticView = false;
        bool semanticOverlay = false;

        // advance the semantic mode every N frames (photo, annotation, overlay,
        // photo...): hands-free cycling for recordings and debugging the cycle
        int semanticCycleFrames = 0;

        // the semantic pass: write the per-model ID image next to the
        // screenshot (<screenshot>_ids.png) and print the derived boxes
        bool semantic = false;

        // headless video: write EVERY frame as a PNG (printf pattern, %05d is the
        // frame index) - e.g. `--video frames/f%05d.png` then encode with ffmpeg
        vsg::Path videoPattern;

        // the scripted maritime showcases (headless): the camera and the environment
        // follow a fixed script (see main.cpp) - "surface": high over the sea, the
        // wind building to a foam-capped FFT sea, cycling the Jerlov water types;
        // "underwater": tracking the ROV from behind at the wreck, the water type
        // stepping clearer while the marine snow varies; "rov": the ROV's own nose
        // camera over the same script (overlay it in the tracking video)
        bool showcase = false;
        std::string showcaseKind;

        // per-lamp dimming: an SDF <light> intensity is the ceiling, --light
        // scales the named lamp below it (qualified "model/light" name)
        std::vector<std::pair<std::string, double>> lightFactors;

        // the scene files, from the remaining positional arguments
        std::vector<vsg::Path> sceneFilenames;
    };

    // assign the standard vsg shader sets (text, flat, phong, pbr) to the options, so
    // the models loaded through vsgXchange shade with the scene's lights; without them
    // the loaders fall back to their own default shaders, which ignore the scene
    // lighting and render the meshes dark. generateDebugInfo adds the debug hints.
    inline void assignStandardShaderSets(const vsg::ref_ptr<vsg::Options>& options, bool generateDebugInfo)
    {
        auto shaderHints = vsg::ShaderCompileSettings::create();
        shaderHints->generateDebugInfo = generateDebugInfo;

        auto& text = options->shaderSets["text"] = vsg::createTextShaderSet(options);
        text->defaultShaderHints = shaderHints;

        auto& flat = options->shaderSets["flat"] = vsg::createFlatShadedShaderSet(options);
        flat->defaultShaderHints = shaderHints;

        auto& phong = options->shaderSets["phong"] = vsg::createPhongShaderSet(options);
        phong->defaultShaderHints = shaderHints;

        auto& pbr = options->shaderSets["pbr"] = vsg::createPhysicsBasedRenderingShaderSet(options);
        pbr->defaultShaderHints = shaderHints;
    }

    // The example's own flags, printed by --help (the vsg viewer flags are listed by
    // vsg itself through --log-level etc.; this is the vsgOcean layer). The environment
    // is also fully tunable at runtime in the GUI - E exports it as a YAML file that
    // --env loads back.
    inline void printUsage(std::ostream& out)
    {
        out << "vsgOcean_example: the islands demo (an ROV, a wreck, dive lamps)\n"
            << "\n"
            << "environment (all tunable at runtime in the GUI; E exports a YAML file, --env loads it back)\n"
            << "  --sun elev azim            static sun position (deg)\n"
            << "  --sun-cycle                animate the sun (1 simulated hour per second)\n"
            << "  --wind speed dir           wind speed and direction (m/s, deg)\n"
            << "  --clouds 0..8              cloud cover in oktas\n"
            << "  --rain 0..1                rain rate (streaks, splash rings, a roughened sea)\n"
            << "  --fog 0..1                 distance haze density\n"
            << "  --latitude deg             geolocation: the hemisphere's constellations\n"
            << "  --longitude deg\n"
            << "  --env file.yaml            a saved environment file (as exported with E)\n"
            << "  --jerlov 0..13             the water type: I, IA, IB, II, III, coastal 1-9 (default coastal 3)\n"
            << "  --chlorophyll mg/m3        a bio-optical water composition (overrides the type)\n"
            << "  --cdom 1/m --sediment g/m3\n"
            << "  --snow 0..100              marine snow density\n"
            << "  --blur 0/1                 the underwater depth-of-field blur\n"
            << "  --current vx vy            the water current (m/s)\n"
            << "\n"
            << "waves\n"
            << "  --wave-amplitude m         mean Gerstner component (amplitude, period, steepness,\n"
            << "  --wave-period s            spreading count, scale and angle below it - the wave\n"
            << "  --wave-steepness 0..1      model of asv_wave_sim, Gazebo-synchronisable)\n"
            << "  --wave-number 1..8 --wave-scale --wave-angle deg\n"
            << "  --fft                      the FFT sea instead (an ECKV spectrum, as asv_wave_sim)\n"
            << "  --fft-tile m --fft-choppiness --fft-wave-age\n"
            << "  --fft-detail               a second, finer cascade (kills the tile repetition)\n"
            << "  --foam 0..1                Jacobian whitecap foam\n"
            << "\n"
            << "lights\n"
            << "  --light model/lamp 0..1    dim a lamp below its SDF intensity (repeatable)\n"
            << "\n"
            << "camera, capture and diagnostics\n"
            << "  --eye x y z                camera position / --center x y z its look-at\n"
            << "  --headless --extent W H    render offscreen, no window (servers, CI)\n"
            << "  -f N                       render N frames then exit\n"
            << "  --screenshot file.png      write the composited view at exit\n"
            << "  --semantic                 also write the per-model ID image (<screenshot>_ids.png)\n"
            << "                             and print the derived bounding boxes\n"
            << "  --semantic-view            start in the semantic annotation view (the R key cycles)\n"
            << "  --semantic-overlay         start with the annotation at 50% over the photo\n"
            << "  --semantic-cycle N         advance the semantic view mode every N frames\n"
            << "  --video pattern            write every headless frame (encode with ffmpeg)\n"
            << "  --showcase kind            a scripted showcase video: surface | underwater | rov\n"
            << "  --fps / --profiler / --debug   frame rate at exit / instrumentation / validation\n"
            << "\n"
            << "the standard vsg viewer flags also apply (--window, --gpu, --mt, --log-level, ...);\n"
            << "the positional arguments are SDF world or model files (default: the islands demo)\n";
    }

    // Parse the command line into the example settings. vsg::CommandLine removes the
    // arguments it consumes, so the positional scene files are collected last,
    // through the argc it keeps up to date.
    inline Options parseOptions(vsg::CommandLine& arguments, int& argc)
    {
        Options settings;

        if (arguments.read({"--help", "-h"}))
        {
            printUsage(std::cout);
            settings.exitCode = 0;
            settings.quit = true; // nothing else to do
            return settings;
        }

        // window traits from the standard vsg window arguments, plus the Vulkan
        // validation layers for --debug
        settings.windowTraits = vsg::WindowTraits::create(arguments);
        settings.windowTraits->debugLayer = arguments.read("--debug");
        if (settings.windowTraits->windowTitle == vsg::WindowTraits::create()->windowTitle) settings.windowTraits->windowTitle = "vsgOcean";

        if (arguments.read({"--redirect-std", "-r"})) vsg::Logger::instance()->redirect_std();

        // file paths, reader/writers and other io options, with vsgXchange's support
        // for 3rd party file formats
        settings.options = vsg::Options::create();
        settings.options->sharedObjects = vsg::SharedObjects::create();
        settings.options->fileCache = vsg::getEnv("VSG_FILE_CACHE");
        settings.options->paths = vsg::getEnvPaths("VSG_FILE_PATH");
#ifdef vsgXchange_all
        settings.options->add(vsgXchange::all::create());
#endif
        settings.options->readOptions(arguments);
        if (uint32_t numOperationThreads = 0; arguments.read("--ot", numOperationThreads)) settings.options->operationThreads = vsg::OperationThreads::create(numOperationThreads);

        settings.reportAverageFrameRate = arguments.read("--fps");
        settings.reportMemoryStats = arguments.read("--rms");
        if (arguments.read({"-t", "--test"}))
        {
            settings.windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            settings.windowTraits->fullscreen = true;
            settings.reportAverageFrameRate = true;
        }
        if (arguments.read({"--st", "--small-test"}))
        {
            settings.windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            settings.windowTraits->width = 192, settings.windowTraits->height = 108;
            settings.windowTraits->decoration = false;
            settings.reportAverageFrameRate = true;
        }

        settings.multiThreading = arguments.read("--mt");
        settings.maxTime = arguments.value(std::numeric_limits<double>::max(), "--max-time");

        if (arguments.read("--ThreadLogger")) vsg::Logger::instance() = vsg::ThreadLogger::create();
        if (int log_level = 0; arguments.read("--log-level", log_level)) vsg::Logger::instance()->level = vsg::Logger::Level(log_level);
        settings.numFrames = arguments.value(-1, "-f");
        settings.pathFilename = arguments.value<vsg::Path>("", "-p");
        settings.loadLevels = arguments.value(0, "--load-levels");
        settings.maxPagedLOD = arguments.value(0, "--maxPagedLOD");

        settings.horizonMountainHeight = arguments.value(0.0, "--hmh");
        settings.nearFarRatio = arguments.value<double>(0.001, "--nfr");
        if (arguments.read("--rgb")) settings.options->mapRGBtoRGBAHint = false;

        std::string shaderSetName;
        vsg::Path shaderSetFilename;
        while (arguments.read("--ss", shaderSetName, shaderSetFilename))
        {
            if (auto shaderSet = vsg::read_cast<vsg::ShaderSet>(shaderSetFilename, settings.options))
            {
                settings.options->shaderSets[shaderSetName] = shaderSet;
                vsg::info("assigned shaderSets[", shaderSetName, "] = ", shaderSet);
            }
        }

        if (arguments.read({"--dc", "--depthClamp"}))
        {
            std::cout << "Enabled depth clamp." << std::endl;
            auto deviceFeatures = settings.windowTraits->deviceFeatures = vsg::DeviceFeatures::create();
            deviceFeatures->get().samplerAnisotropy = VK_TRUE;
            deviceFeatures->get().depthClamp = VK_TRUE;
        }

        if (auto resourceHintsFilename = arguments.value<vsg::Path>("", "--rh"))
        {
            settings.resourceHints = vsg::read_cast<vsg::ResourceHints>(resourceHintsFilename, settings.options);
        }

        if (auto outputResourceHintsFilename = arguments.value<vsg::Path>("", "--orh"))
        {
            if (!settings.resourceHints) settings.resourceHints = vsg::ResourceHints::create();
            vsg::write(settings.resourceHints, outputResourceHintsFilename, settings.options);
            settings.quit = true; // nothing else to do
            return settings;
        }

        const bool shaderDebugInfo = arguments.read({"--shader-debug-info", "--sdi"});
        if (shaderDebugInfo)
        {
            settings.windowTraits->deviceExtensionNames.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
        }

        // always shade the models with the scene's lights (see assignStandardShaderSets)
        assignStandardShaderSets(settings.options, shaderDebugInfo);

        settings.logFilename = arguments.value<vsg::Path>("", "--log");

        if (arguments.read({"--gpu-annotation", "--ga"}) && vsg::isExtensionSupported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            settings.windowTraits->debugUtils = true;

            auto gpu_instrumentation = vsg::GpuAnnotation::create();
            if (arguments.read("--name"))
                gpu_instrumentation->labelType = vsg::GpuAnnotation::SourceLocation_name;
            else if (arguments.read("--className"))
                gpu_instrumentation->labelType = vsg::GpuAnnotation::Object_className;
            else if (arguments.read("--func"))
                gpu_instrumentation->labelType = vsg::GpuAnnotation::SourceLocation_function;

            settings.instrumentation = gpu_instrumentation;
        }
        else if (arguments.read({"--profiler", "--pr"}))
        {
            auto profilerSettings = vsg::Profiler::Settings::create();
            arguments.read("--cpu", profilerSettings->cpu_instrumentation_level);
            arguments.read("--gpu", profilerSettings->gpu_instrumentation_level);
            arguments.read("--log-size", profilerSettings->log_size);

            settings.instrumentation = vsg::Profiler::create(profilerSettings);
        }

        uint32_t cpu = 0;
        while (arguments.read("-c", cpu))
        {
            settings.affinity.cpus.insert(cpu);
        }

        settings.autoPlay = !arguments.read({"--no-auto-play", "--nop"});
        settings.sunCycle = arguments.read("--sun-cycle");

        // debugging: start the camera at a given position (e.g. underwater); parsed
        // before the positional scene arguments are collected
        if (arguments.read("--eye", settings.eye.x, settings.eye.y, settings.eye.z)) settings.overrideEye = true;

        if (arguments.read("--headless")) settings.headless = true;
        if (arguments.read("--extent", settings.headlessExtent.width, settings.headlessExtent.height)) {}

        // aim the camera at a fixed point (the fallback camera looks at the scene
        // bounds centre; --center overrides that for deterministic framing)
        if (arguments.read("--center", settings.center.x, settings.center.y, settings.center.z)) settings.overrideCenter = true;

        // capture: write the main view to a PNG file when the render loop ends
        settings.screenshotPath = arguments.value<vsg::Path>("", "--screenshot");
        settings.semantic = arguments.read("--semantic");
        settings.semanticView = arguments.read("--semantic-view");
        settings.semanticOverlay = arguments.read("--semantic-overlay");
        settings.semanticCycleFrames = arguments.value(0, "--semantic-cycle");
        settings.videoPattern = arguments.value<vsg::Path>("", "--video");
        settings.showcaseKind = arguments.value<std::string>("", "--showcase");
        settings.showcase = !settings.showcaseKind.empty();
        {
            std::string lightName;
            double lightFactor = 1.0;
            while (arguments.read("--light", lightName, lightFactor))
            {
                settings.lightFactors.emplace_back(lightName, lightFactor);
            }
        }

        if (arguments.read("--sun", settings.sunElevation, settings.sunAzimuth)) settings.overrideSun = true;
        if (arguments.read("--wind", settings.windSpeed, settings.windDirection)) settings.overrideWind = true;
        if (arguments.read("--jerlov", settings.jerlov)) settings.overrideJerlov = true;
        settings.environmentFile = arguments.value<vsg::Path>("", "--env");
        settings.overrideWater |= arguments.read("--chlorophyll", settings.water.chlorophyll);
        settings.overrideWater |= arguments.read("--cdom", settings.water.cdom);
        settings.overrideWater |= arguments.read("--sediment", settings.water.sediment);
        if (arguments.read("--fog", settings.fogDensity)) settings.overrideFog = true;
        if (arguments.read("--latitude", settings.latitude)) settings.overrideLatitude = true;
        if (arguments.read("--longitude", settings.longitude)) settings.overrideLongitude = true;
        if (arguments.read("--snow", settings.snowDensity)) settings.overrideSnow = true;
        if (arguments.read("--clouds", settings.cloudiness)) settings.overrideClouds = true;
        if (arguments.read("--rain", settings.rainRate)) settings.overrideRain = true;
        if (arguments.read("--blur", settings.underwaterBlur)) settings.overrideBlur = true;
        if (arguments.read("--current", settings.waterCurrent.x, settings.waterCurrent.y)) settings.overrideCurrent = true;

        settings.overrideWaves |= arguments.read("--wave-amplitude", settings.waveAmplitude);
        settings.overrideWaves |= arguments.read("--wave-period", settings.wavePeriod);
        settings.overrideWaves |= arguments.read("--wave-steepness", settings.waveSteepness);
        settings.overrideWaves |= arguments.read("--wave-number", settings.waveNumber);
        settings.overrideWaves |= arguments.read("--wave-scale", settings.waveScale);
        settings.overrideWaves |= arguments.read("--wave-angle", settings.waveAngle);

        if (arguments.read("--fft"))
        {
            settings.fft = true;
            settings.overrideWaves = true;
        }
        settings.overrideWaves |= arguments.read("--fft-tile", settings.fftTile);
        settings.overrideWaves |= arguments.read("--fft-choppiness", settings.fftChoppiness);
        settings.overrideWaves |= arguments.read("--fft-wave-age", settings.fftWaveAge);
        if (arguments.read("--fft-detail"))
        {
            settings.fftDetail = true;
            settings.overrideWaves = true;
        }
        settings.overrideWaves |= arguments.read("--foam", settings.foam);

        if (arguments.errors())
        {
            settings.exitCode = arguments.writeErrorMessages(std::cerr);
            settings.quit = true;
            return settings;
        }

        // the scene files from the remaining positional arguments
        for (int i = 1; i < argc; ++i) settings.sceneFilenames.push_back(vsg::Path(arguments[i]));

        return settings;
    }
}
