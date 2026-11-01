/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/ui/EnvironmentGui.h>

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
            ImGui::Begin("vsgOcean cameras");

            for (const auto& [name, texture] : cameras)
            {
                if (!texture->descriptorSet_) continue; // not compiled yet
                ImGui::TextUnformatted(name.c_str());
                ImGui::Image(texture->id(), ImVec2(320.0f, 240.0f));
            }

            ImGui::End();
            return true;
        }

        // the environment panel: bindings, model tracking, sliders
        bool drawEnvironmentPanel(const vsg::ref_ptr<EnvironmentControls>& controls, const vsg::ref_ptr<Scene>& scene,
                                  const vsg::ref_ptr<Environment>& environment, const vsg::ref_ptr<OceanSurface>& surface,
                                  const vsg::ref_ptr<Wavefield>& wavefield)
        {
            if (!controls->panelVisible) return true;

            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Once);
            ImGui::Begin("vsgOcean");

            for (const auto& [key, action] : EnvironmentControls::bindings)
            {
                ImGui::BulletText("%s: %s", key.data(), action.data());
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

            ImGui::Separator();

            auto sun = static_cast<float>(environment->sunElevation());
            if (ImGui::SliderFloat("sun elevation (deg)", &sun, -90.0f, 90.0f))
            {
                controls->sunCycleEnabled = false;
                environment->setSunElevation(sun);
            }
            sun = static_cast<float>(environment->sunAzimuth());
            if (ImGui::SliderFloat("sun azimuth (deg)", &sun, 0.0f, 360.0f))
            {
                controls->sunCycleEnabled = false;
                environment->setSunAzimuth(sun);
            }
            ImGui::Checkbox("day cycle animation", &controls->sunCycleEnabled);
            ImGui::Separator();

            auto jerlov = static_cast<int>(environment->jerlovType());
            if (ImGui::SliderInt("Jerlov water type", &jerlov, 0, 8))
            {
                environment->setJerlovType(static_cast<Environment::JerlovType>(jerlov));
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(environment->jerlovName().c_str());

            auto sea = static_cast<float>(environment->seaLevel());
            if (ImGui::SliderFloat("sea level (m)", &sea, -50.0f, 10.0f)) environment->setSeaLevel(sea);

            auto snow = static_cast<float>(environment->marineSnowDensity());
            if (ImGui::SliderFloat("marine snow density", &snow, 0.0f, 1.0f)) environment->setMarineSnowDensity(snow);
            auto drift = environment->waterCurrent();
            auto driftSpeed = vsg::length(vsg::vec2(drift.x, drift.y));
            if (ImGui::SliderFloat("water current (m/s)", &driftSpeed, 0.0f, 2.0f))
            {
                const auto dir = driftSpeed > 1.0e-3f && vsg::length(vsg::vec2(drift.x, drift.y)) > 1.0e-3f
                                     ? vsg::normalize(vsg::vec2(drift.x, drift.y))
                                     : vsg::vec2(1.0f, 0.0f);
                environment->setWaterCurrent(vsg::vec3(dir.x * driftSpeed, dir.y * driftSpeed, drift.z));
            }

            auto water = scene->surfaceEnabled();
            if (ImGui::Checkbox("ocean surface", &water))
            {
                scene->setSurfaceEnabled(water);
            }
            ImGui::Separator();

            auto wind = static_cast<float>(environment->windSpeed());
            if (ImGui::SliderFloat("wind speed (m/s)", &wind, 0.0f, 30.0f)) environment->setWindSpeed(wind);
            wind = static_cast<float>(environment->windDirection());
            if (ImGui::SliderFloat("wind direction (deg)", &wind, 0.0f, 360.0f)) environment->setWindDirection(wind);

            if (wavefield)
            {
                auto amplitude = static_cast<float>(wavefield->amplitude());
                if (ImGui::SliderFloat("wave amplitude (m)", &amplitude, 0.0f, 3.0f)) wavefield->setAmplitude(amplitude);
                auto period = static_cast<float>(wavefield->period());
                if (ImGui::SliderFloat("wave period (s)", &period, 1.0f, 20.0f)) wavefield->setPeriod(period);
                auto steepness = static_cast<float>(wavefield->steepness());
                if (ImGui::SliderFloat("wave steepness", &steepness, 0.0f, 1.0f)) wavefield->setSteepness(steepness);
                auto number = static_cast<int>(wavefield->number());
                if (ImGui::SliderInt("wave components", &number, 1, static_cast<int>(vsgOcean::Wavefield::maxWaves))) wavefield->setNumber(static_cast<size_t>(number));
                auto scale = static_cast<float>(wavefield->scale());
                if (ImGui::SliderFloat("wave scale", &scale, 1.0f, 4.0f)) wavefield->setScale(scale);
                auto angle = static_cast<float>(vsg::degrees(wavefield->angle()));
                if (ImGui::SliderFloat("wave angle (deg)", &angle, 0.0f, 90.0f)) wavefield->setAngle(vsg::radians(static_cast<double>(angle)));
            }

            auto fog = static_cast<float>(environment->fogDensity());
            if (ImGui::SliderFloat("fog density", &fog, 0.0f, 1.0f, "%.3f")) environment->setFogDensity(fog);

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

        // the texture adapters are children of the GUI node, so they compile with it
        auto gui = vsgImGui::RenderImGui::create(window,
                                                 [controls, scene, environment, surface, wavefield, cameras]()
                                                 {
                                                     // let the camera controller and the keyboard controls ignore
                                                     // input while the GUI captures it
                                                     controls->guiCapturesInput = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;

                                                     drawCameraWidget(controls, cameras);
                                                     return drawEnvironmentPanel(controls, scene, environment, surface, wavefield);
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
