/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/ui/EnvironmentGui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

#ifdef VSGOCEAN_WITH_GUI
#    include <vsgImGui/RenderImGui.h>
#    include <vsgImGui/SendEventsToImGui.h>
#endif

namespace vsgOcean
{
#ifdef VSGOCEAN_WITH_GUI

    namespace
    {
        // ImGui-displayable adapter for a live render target view: compiles the
        // descriptor set with the GUI's resources, then draws with ImGui::Image
        class LiveGuiTexture : public vsg::Inherit<vsg::Compilable, LiveGuiTexture>
        {
        public:
            explicit LiveGuiTexture(vsg::ref_ptr<vsg::ImageView> view) :
                view_(std::move(view))
            {
            }

            void compile(vsg::Context& context) override
            {
                if (descriptorSet_) return;

                auto sampler = vsg::Sampler::create();
                sampler->magFilter = VK_FILTER_LINEAR;
                sampler->minFilter = VK_FILTER_LINEAR;
                sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

                auto layout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
                    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
                auto imageInfo = vsg::ImageInfo::create(sampler, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                descriptorSet_ = vsg::DescriptorSet::create(layout, vsg::Descriptors{vsg::DescriptorImage::create(imageInfo, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)});
                descriptorSet_->compile(context);
            }

            [[nodiscard]] ImTextureID id() const { return static_cast<ImTextureID>(VkDescriptorSet(descriptorSet_->vk(0))); }

            vsg::ref_ptr<vsg::ImageView> view_;
            vsg::ref_ptr<vsg::DescriptorSet> descriptorSet_;
        };

        using CameraTexture = std::pair<std::string, vsg::ref_ptr<LiveGuiTexture>>;

        // the camera preview widget: the render-to-texture views of the sensor
        // pipelines, independent of the tuning panel (V key)
        bool drawCameraWidget(const vsg::ref_ptr<EnvironmentControls>& controls, const std::vector<CameraTexture>& cameras)
        {
            if (!controls->camerasVisible) return true;

            // top right corner, clear of the HUD text in the top left
            const auto& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 336.0f, 10.0f), ImGuiCond_Once);
            ImGui::Begin("vsgOcean cameras", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            if (cameras.empty()) ImGui::TextUnformatted("no camera is rendering");

            for (const auto& [name, texture] : cameras)
            {
                if (!texture->descriptorSet_) continue; // not compiled yet
                ImGui::TextUnformatted(name.c_str());
                ImGui::Image(texture->id(), ImVec2(320.0f, 240.0f));
            }

            ImGui::End();
            return true;
        }

        // The tuning widgets read the environment / wave field EVERY frame and write
        // only on change: an external update (a topic, the code, a key binding) is
        // reflected immediately, and a widget change takes over from any preset
        // (e.g. moving the sun stops the day-cycle animation).
        //
        // drawHelp: the basic key bindings, the model tracking, and the live
        // application stats (fps, cpu, memory). The tuning widgets follow, grouped
        // by layer of the environment.

        // The live application stats, sampled from /proc a few times a second so
        // the reads and the display stay cheap (the fps is ImGui's own smoothed
        // rate). The CPU percent is of ONE core (a value of 140% means ~1.4 cores
        // busy including the driver / rasterizer threads).
        struct AppStats
        {
            double lastSample = 0.0;
            double cpuJiffies = 0.0;
            float cpuPercent = 0.0f;
            float rssMB = 0.0f;

            void sample()
            {
                using clock = std::chrono::steady_clock;
                const double seconds = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
                if (seconds - lastSample < 0.25) return;
                const double dt = std::max(seconds - lastSample, 1.0e-3);
                lastSample = seconds;

                if (FILE* f = fopen("/proc/self/stat", "r"))
                {
                    char line[1024];
                    if (fgets(line, sizeof(line), f))
                    {
                        if (const char* close = strrchr(line, ')'))
                        {
                            long utime = 0, stime = 0;
                            // after the comm field: 11 process fields, then utime, stime
                            if (sscanf(close + 2, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %ld %ld", &utime, &stime) == 2)
                            {
                                const auto jiffies = static_cast<double>(utime + stime);
                                if (cpuJiffies > 0.0)
                                {
                                    const auto hz = static_cast<double>(sysconf(_SC_CLK_TCK));
                                    cpuPercent = static_cast<float>((jiffies - cpuJiffies) / hz / dt * 100.0);
                                }
                                cpuJiffies = jiffies;
                            }
                        }
                    }
                    fclose(f);
                }

                if (FILE* f = fopen("/proc/self/statm", "r"))
                {
                    long total = 0, resident = 0;
                    if (fscanf(f, "%ld %ld", &total, &resident) == 2)
                    {
                        rssMB = static_cast<float>(static_cast<double>(resident) *
                                                   static_cast<double>(sysconf(_SC_PAGE_SIZE)) /
                                                   (1024.0 * 1024.0));
                    }
                    fclose(f);
                }
            }
        };

        bool drawHelp(const vsg::ref_ptr<EnvironmentControls>& controls, const vsg::ref_ptr<Scene>& scene,
                      const std::string& deviceName)
        {
            if (!controls->helpVisible) return true;

            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Once);
            ImGui::Begin("vsgOcean help", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            // the widgets size to their content: a uniform slider width, and the
            // window auto-resizes to the longest label
            ImGui::PushItemWidth(280.0f);

            for (const auto& [key, action] : EnvironmentControls::bindings)
            {
                ImGui::BulletText("%s: %s", key.data(), action.data());
            }
            ImGui::Separator();

            // export the environment as tuned so far: a structured YAML file,
            // reloadable with the example's --env flag (same as the E key)
            if (ImGui::Button("export environment"))
            {
                if (!scene->saveEnvironment(controls->environmentExportPath))
                {
                    ImGui::OpenPopup("export failed");
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(std::string(controls->environmentExportPath).c_str());
            if (ImGui::BeginPopup("export failed"))
            {
                ImGui::TextUnformatted("could not export (built without yaml-cpp?)");
                ImGui::EndPopup();
            }
            ImGui::Separator();

            // camera tracking: cycle through the models of the scene
            const auto models = scene->modelNames();
            std::vector<const char*> labels{"(none)"};
            for (const auto& model : models) labels.push_back(model.c_str());
            auto current = 0;
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (models[i] == scene->trackedFrame()) current = static_cast<int>(i) + 1;
            }
            if (ImGui::Combo("track model", &current, labels.data(), static_cast<int>(labels.size())))
            {
                scene->setTrackedFrame(current == 0 ? std::string{} : models[static_cast<size_t>(current) - 1]);
            }

            // the live stats block (like the osgOcean stats overlay)
            static AppStats stats;
            stats.sample();
            ImGui::Separator();
            const auto& io = ImGui::GetIO();
            const float frameMs = io.Framerate > 1.0e-3f ? 1000.0f / io.Framerate : 0.0f;
            ImGui::Text("fps %.1f (%.1f ms/frame)", io.Framerate, frameMs);
            ImGui::Text("cpu %.0f%% (of one core)", stats.cpuPercent);
            ImGui::Text("memory %.0f MB resident", stats.rssMB);
            if (!deviceName.empty()) ImGui::TextUnformatted(deviceName.c_str());

            ImGui::PopItemWidth();

            ImGui::End();
            return true;
        }

        // the sky layer: the sun (with the day-cycle animation) and the weather
        // above the water - the fog distance haze and the cloud cover
        bool drawSkyWidget(const vsg::ref_ptr<EnvironmentControls>& controls, const vsg::ref_ptr<Environment>& environment)
        {
            if (!controls->skyVisible) return true;

            ImGui::SetNextWindowPos(ImVec2(10.0f, 420.0f), ImGuiCond_Once);
            ImGui::Begin("sky", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushItemWidth(280.0f);

            auto sun = static_cast<float>(environment->sunElevation());
            if (ImGui::SliderFloat("sun elevation (deg)", &sun, -90.0f, 90.0f))
            {
                controls->sunCycleEnabled = false; // the widget takes over the animation
                environment->setSunElevation(sun);
            }
            sun = static_cast<float>(environment->sunAzimuth());
            if (ImGui::SliderFloat("sun azimuth (deg)", &sun, 0.0f, 360.0f))
            {
                controls->sunCycleEnabled = false;
                environment->setSunAzimuth(sun);
            }
            ImGui::Checkbox("day cycle animation", &controls->sunCycleEnabled);

            auto fog = static_cast<float>(environment->fogDensity());
            if (ImGui::SliderFloat("fog density", &fog, 0.0f, 1.0f, "%.3f")) environment->setFogDensity(fog);

            auto clouds = static_cast<int>(environment->cloudiness());
            if (ImGui::SliderInt("cloud cover (oktas)", &clouds, 0, 8)) environment->setCloudiness(static_cast<double>(clouds));

            auto rain = static_cast<float>(environment->rainRate());
            if (ImGui::SliderFloat("rain", &rain, 0.0f, 1.0f)) environment->setRainRate(rain);

            ImGui::PopItemWidth();
            ImGui::End();
            return true;
        }

        // the surface layer: the wind driving the waves (the wind DIRECTION is not
        // exposed here yet - a maritime compass widget comes later) and the wave
        // field of the ocean surface
        bool drawSurfaceWidget(const vsg::ref_ptr<EnvironmentControls>& controls, const vsg::ref_ptr<Scene>& scene,
                               const vsg::ref_ptr<Environment>& environment, const vsg::ref_ptr<Wavefield>& wavefield)
        {
            if (!controls->surfaceVisible) return true;

            ImGui::SetNextWindowPos(ImVec2(360.0f, 10.0f), ImGuiCond_Once);
            ImGui::Begin("ocean surface", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushItemWidth(280.0f);

            auto wind = static_cast<float>(environment->windSpeed());
            if (ImGui::SliderFloat("wind speed (m/s)", &wind, 0.0f, 30.0f)) environment->setWindSpeed(wind);

            auto water = scene->surfaceEnabled();
            if (ImGui::Checkbox("ocean surface", &water)) scene->setSurfaceEnabled(water);

            if (wavefield)
            {
                auto fftSea = wavefield->model() == Wavefield::Model::FFT;
                if (ImGui::Checkbox("FFT sea (ECKV spectrum)", &fftSea)) wavefield->setModel(fftSea ? Wavefield::Model::FFT : Wavefield::Model::CWR);
                if (fftSea)
                {
                    auto fft = wavefield->fftParameters();
                    auto tile = static_cast<float>(fft.tileSize);
                    auto choppiness = static_cast<float>(fft.choppiness);
                    auto waveAge = static_cast<float>(fft.capOmegaC);
                    auto spread = static_cast<float>(fft.spread);
                    bool changed = ImGui::SliderFloat("tile size (m)", &tile, 50.0f, 1024.0f);
                    changed |= ImGui::SliderFloat("choppiness", &choppiness, 0.0f, 1.5f);
                    changed |= ImGui::SliderFloat("wave age (inverse)", &waveAge, 0.84f, 5.0f);
                    changed |= ImGui::SliderFloat("directional spread", &spread, 1.0f, 30.0f);
                    auto detail = wavefield->fftDetail();
                    if (ImGui::Checkbox("fine detail cascade", &detail)) wavefield->setFftDetail(detail);
                    auto foam = static_cast<float>(wavefield->foam());
                    if (ImGui::SliderFloat("foam", &foam, 0.0f, 1.0f)) wavefield->setFoam(foam);
                    if (changed)
                    {
                        fft.tileSize = tile;
                        fft.choppiness = choppiness;
                        fft.capOmegaC = waveAge;
                        fft.spread = spread;
                        wavefield->setFftParameters(fft);
                    }
                }
                else
                {
                    auto amplitude = static_cast<float>(wavefield->amplitude());
                    if (ImGui::SliderFloat("wave amplitude (m)", &amplitude, 0.0f, 2.0f)) wavefield->setAmplitude(amplitude);
                    auto period = static_cast<float>(wavefield->period());
                    if (ImGui::SliderFloat("wave period (s)", &period, 1.0f, 20.0f)) wavefield->setPeriod(period);
                    auto steepness = static_cast<float>(wavefield->steepness());
                    if (ImGui::SliderFloat("wave steepness", &steepness, 0.0f, 1.0f)) wavefield->setSteepness(steepness);
                    auto number = static_cast<int>(wavefield->number());
                    if (ImGui::SliderInt("wave components", &number, 1, static_cast<int>(Wavefield::maxWaves))) wavefield->setNumber(static_cast<size_t>(number));
                    auto scale = static_cast<float>(wavefield->scale());
                    if (ImGui::SliderFloat("wave scale", &scale, 1.0f, 4.0f)) wavefield->setScale(scale);
                    auto angle = static_cast<float>(vsg::degrees(wavefield->angle()));
                    if (ImGui::SliderFloat("wave angle (deg)", &angle, 0.0f, 90.0f)) wavefield->setAngle(vsg::radians(static_cast<double>(angle)));
                }
            }

            ImGui::PopItemWidth();
            ImGui::End();
            return true;
        }

        // the underwater layer: the water optics (the Jerlov taxonomy), the sea
        // level, the drift of the water column and its particles
        // the lights widget: one intensity slider per LIGHT GROUP. Each model's
        // lamps are clustered by aim direction (pairwise within 45 deg - one
        // face of the robot), and the group is the control; an SDF <light>
        // intensity is each member's CEILING - the sliders only dim below it,
        // and the shading, the volumetric cones and the snow sparkle all follow
        bool drawLightsWidget(const vsg::ref_ptr<EnvironmentControls>& controls, const vsg::ref_ptr<Scene>& scene)
        {
            if (!controls->lightsVisible) return true;

            ImGui::SetNextWindowPos(ImVec2(680.0f, 10.0f), ImGuiCond_Once);
            ImGui::Begin("lights", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushItemWidth(220.0f);

            const auto groups = scene->lightGroupNames();
            if (groups.empty())
            {
                ImGui::TextUnformatted("no lights in the scene");
            }
            for (const auto& group : groups)
            {
                auto factor = static_cast<float>(scene->lightGroupIntensity(group));
                if (ImGui::SliderFloat(group.c_str(), &factor, 0.0f, 1.0f)) scene->setLightGroupIntensity(group, factor);
                if (ImGui::IsItemHovered())
                {
                    std::string members;
                    for (const auto& lamp : scene->lightGroupMembers(group)) members += (members.empty() ? "" : ", ") + lamp;
                    ImGui::SetTooltip("%s", members.c_str());
                }
            }

            ImGui::PopItemWidth();
            ImGui::End();
            return true;
        }

        bool drawUnderwaterWidget(const vsg::ref_ptr<EnvironmentControls>& controls, const vsg::ref_ptr<Environment>& environment)
        {
            if (!controls->underwaterVisible) return true;

            ImGui::SetNextWindowPos(ImVec2(360.0f, 250.0f), ImGuiCond_Once);
            ImGui::Begin("underwater", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushItemWidth(280.0f);

            auto jerlov = static_cast<int>(environment->jerlovType());
            if (ImGui::SliderInt("Jerlov water type", &jerlov, 0, static_cast<int>(Environment::jerlovTypeCount) - 1))
            {
                environment->setJerlovType(static_cast<Environment::JerlovType>(jerlov));
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(environment->jerlovName().c_str());

            auto water = environment->waterComposition();
            auto chlorophyll = static_cast<float>(water.chlorophyll);
            auto cdom = static_cast<float>(water.cdom);
            auto sediment = static_cast<float>(water.sediment);
            bool composed = ImGui::SliderFloat("chlorophyll (mg/m3)", &chlorophyll, 0.01f, 30.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            composed |= ImGui::SliderFloat("CDOM a440 (1/m)", &cdom, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            composed |= ImGui::SliderFloat("sediment (g/m3)", &sediment, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (composed) environment->setWaterComposition({chlorophyll, cdom, sediment});

            auto sea = static_cast<float>(environment->seaLevel());
            if (ImGui::SliderFloat("sea level (m)", &sea, -50.0f, 10.0f)) environment->setSeaLevel(sea);

            auto drift = environment->waterCurrent();
            auto driftSpeed = vsg::length(vsg::vec2(drift.x, drift.y));
            if (ImGui::SliderFloat("water current (m/s)", &driftSpeed, 0.0f, 2.0f))
            {
                const auto dir = driftSpeed > 1.0e-3f && vsg::length(vsg::vec2(drift.x, drift.y)) > 1.0e-3f
                                     ? vsg::normalize(vsg::vec2(drift.x, drift.y))
                                     : vsg::vec2(1.0f, 0.0f);
                environment->setWaterCurrent(vsg::vec3(dir.x * driftSpeed, dir.y * driftSpeed, drift.z));
            }

            auto snow = static_cast<float>(environment->marineSnowDensity());
            if (ImGui::SliderFloat("marine snow density", &snow, 0.0f, 100.0f)) environment->setMarineSnowDensity(snow);

            auto blur = environment->underwaterBlur();
            if (ImGui::Checkbox("underwater DOF blur", &blur)) environment->setUnderwaterBlur(blur);

            ImGui::PopItemWidth();
            ImGui::End();
            return true;
        }
    }

    vsg::ref_ptr<vsg::Node> createEnvironmentGui(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer, const vsg::ref_ptr<EnvironmentControls>& controls,
                                                 const std::vector<std::pair<std::string, vsg::ref_ptr<vsg::ImageView>>>& cameraViews)
    {
        if (!controls || !controls->scene()) return {};

        const auto scene = controls->scene();
        const auto environment = scene->environment();
        const auto surface = scene->surface();
        const auto wavefield = surface ? surface->wavefield() : nullptr;

        // vsg events forwarded to ImGui, before any camera controller
        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());

        // the camera views rendered to texture, displayed in the panel
        std::vector<CameraTexture> cameras;
        for (const auto& [name, view] : cameraViews)
        {
            cameras.emplace_back(name, LiveGuiTexture::create(view));
        }

        // the render device name, for the stats block (helps debugging e.g. swapchain
        // issues: which device the window pipeline ended up on)
        std::string deviceName;
        if (const auto& device = window->getOrCreateDevice())
        {
            deviceName = device->getPhysicalDevice()->getProperties().deviceName;
        }

        // the texture adapters are children of the GUI node, so they compile with it
        auto gui = vsgImGui::RenderImGui::create(window,
                                                 [controls, scene, environment, wavefield, cameras, deviceName]()
                                                 {
                                                     // let the camera controller and the keyboard controls ignore
                                                     // input while the GUI captures it
                                                     controls->guiCapturesInput = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;

                                                     drawCameraWidget(controls, cameras);
                                                     drawHelp(controls, scene, deviceName);
                                                     drawSkyWidget(controls, environment);
                                                     drawSurfaceWidget(controls, scene, environment, wavefield);
                                                     drawLightsWidget(controls, scene);
                                                     return drawUnderwaterWidget(controls, environment);
                                                 });
        if (auto group = gui.cast<vsg::Group>())
        {
            for (const auto& [name, texture] : cameras)
            {
                (void)name;
                group->addChild(texture);
            }
        }
        return gui;
    }

#else

    vsg::ref_ptr<vsg::Node> createEnvironmentGui(vsg::ref_ptr<vsg::Window>, vsg::ref_ptr<vsg::Viewer>, const vsg::ref_ptr<EnvironmentControls>&,
                                                 const std::vector<std::pair<std::string, vsg::ref_ptr<vsg::ImageView>>>&)
    {
        vsg::warn("vsgOcean was not built with GUI support (vsgImGui not found); the environment key bindings still work");
        return {};
    }

#endif

    vsg::ref_ptr<Hud> createEnvironmentHud(const vsg::ref_ptr<EnvironmentControls>&, vsg::ref_ptr<vsg::Options> fontOptions, VkExtent2D extent)
    {
        auto hud = Hud::create(std::move(fontOptions), extent);
        if (!hud->valid()) return {};

        std::vector<std::string> lines;
        lines.reserve(EnvironmentControls::bindings.size() + 1);
        lines.emplace_back("vsgOcean");
        for (const auto& [key, action] : EnvironmentControls::bindings)
        {
            lines.emplace_back(std::string(key) + ": " + std::string(action));
        }
        hud->setLines(std::move(lines));
        return hud;
    }
}
