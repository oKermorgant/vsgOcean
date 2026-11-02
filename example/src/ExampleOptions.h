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

        // geolocation (negative latitude = the southern hemisphere: the southern
        // constellations replace the northern ones)
        bool overrideLatitude = false;
        double latitude = 0.0;
        bool overrideLongitude = false;
        double longitude = 0.0;

        // waves: the mean component wave of the Gazebo wave model (asv_wave_sim);
        // the component count, scale and angle follow asv_wave_sim's defaults, so
        // the sea is irregular and short-crested rather than a single wave train
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

    // Parse the command line into the example settings. vsg::CommandLine removes the
    // arguments it consumes, so the positional scene files are collected last,
    // through the argc it keeps up to date.
    inline Options parseOptions(vsg::CommandLine& arguments, int& argc)
    {
        Options settings;

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

        if (arguments.read("--sun", settings.sunElevation, settings.sunAzimuth)) settings.overrideSun = true;
        if (arguments.read("--wind", settings.windSpeed, settings.windDirection)) settings.overrideWind = true;
        if (arguments.read("--jerlov", settings.jerlov)) settings.overrideJerlov = true;
        settings.overrideWater |= arguments.read("--chlorophyll", settings.water.chlorophyll);
        settings.overrideWater |= arguments.read("--cdom", settings.water.cdom);
        settings.overrideWater |= arguments.read("--sediment", settings.water.sediment);
        if (arguments.read("--fog", settings.fogDensity)) settings.overrideFog = true;
        if (arguments.read("--latitude", settings.latitude)) settings.overrideLatitude = true;
        if (arguments.read("--longitude", settings.longitude)) settings.overrideLongitude = true;
        if (arguments.read("--snow", settings.snowDensity)) settings.overrideSnow = true;
        if (arguments.read("--clouds", settings.cloudiness)) settings.overrideClouds = true;
        if (arguments.read("--blur", settings.underwaterBlur)) settings.overrideBlur = true;
        if (arguments.read("--current", settings.waterCurrent.x, settings.waterCurrent.y)) settings.overrideCurrent = true;

        settings.waveAmplitude = arguments.value(0.1, "--wave-amplitude");
        settings.wavePeriod = arguments.value(5.0, "--wave-period");
        settings.waveSteepness = arguments.value(1.0, "--wave-steepness");
        settings.waveNumber = arguments.value(8, "--wave-number");
        settings.waveScale = arguments.value(2.0, "--wave-scale");
        settings.waveAngle = arguments.value(36.0, "--wave-angle");
        settings.fft = arguments.read("--fft");
        settings.fftTile = arguments.value(256.0, "--fft-tile");
        settings.fftChoppiness = arguments.value(0.6, "--fft-choppiness");
        settings.fftWaveAge = arguments.value(0.84, "--fft-wave-age");
        settings.fftDetail = arguments.read("--fft-detail");
        settings.foam = arguments.value(0.0, "--foam");

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
