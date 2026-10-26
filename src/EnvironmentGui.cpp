/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/EnvironmentGui.h>

#ifdef VSGOCEAN_WITH_GUI
#    include <vsgImGui/RenderImGui.h>
#    include <vsgImGui/SendEventsToImGui.h>
#endif

namespace vsgOcean
{
#ifdef VSGOCEAN_WITH_GUI

    vsg::ref_ptr<vsg::Node> createEnvironmentGui(vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Viewer> viewer, const vsg::ref_ptr<EnvironmentControls>& controls)
    {
        if (!controls || !controls->scene()) return {};

        const auto scene = controls->scene();
        const auto environment = scene->environment();
        const auto surface = scene->surface();
        const auto wavefield = surface ? surface->wavefield() : nullptr;

        // vsg events forwarded to ImGui, before any camera controller
        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());

        return vsgImGui::RenderImGui::create(window, [controls, scene, environment, surface, wavefield]() {
            // let the camera controller and the keyboard controls ignore input while
            // the GUI captures it (before the panel visibility check: the flag must be
            // refreshed every frame)
            controls->guiCapturesInput = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
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
            int current = 0;
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (models[i] == scene->trackedFrame()) current = static_cast<int>(i) + 1;
            }
            if (ImGui::Combo("track model", &current, labels.data(), static_cast<int>(labels.size())))
            {
                scene->setTrackedFrame(current == 0 ? std::string{} : models[static_cast<size_t>(current) - 1]);
            }
            ImGui::Separator();

            float sun = static_cast<float>(environment->sunElevation());
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

            int jerlov = static_cast<int>(environment->jerlovType());
            if (ImGui::SliderInt("Jerlov water type", &jerlov, 0, 8))
            {
                environment->setJerlovType(static_cast<Environment::JerlovType>(jerlov));
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(environment->jerlovName().c_str());

            float sea = static_cast<float>(environment->seaLevel());
            if (ImGui::SliderFloat("sea level (m)", &sea, -50.0f, 10.0f)) environment->setSeaLevel(sea);

            bool water = scene->surfaceEnabled();
            if (ImGui::Checkbox("ocean surface", &water))
            {
                scene->setSurfaceEnabled(water);
            }
            ImGui::Separator();

            float wind = static_cast<float>(environment->windSpeed());
            if (ImGui::SliderFloat("wind speed (m/s)", &wind, 0.0f, 30.0f)) environment->setWindSpeed(wind);
            wind = static_cast<float>(environment->windDirection());
            if (ImGui::SliderFloat("wind direction (deg)", &wind, 0.0f, 360.0f)) environment->setWindDirection(wind);

            if (wavefield)
            {
                float amplitude = static_cast<float>(wavefield->amplitude());
                if (ImGui::SliderFloat("wave amplitude (m)", &amplitude, 0.0f, 3.0f)) wavefield->setAmplitude(amplitude);
                float period = static_cast<float>(wavefield->period());
                if (ImGui::SliderFloat("wave period (s)", &period, 1.0f, 20.0f)) wavefield->setPeriod(period);
                float steepness = static_cast<float>(wavefield->steepness());
                if (ImGui::SliderFloat("wave steepness", &steepness, 0.0f, 1.0f)) wavefield->setSteepness(steepness);
            }

            float fog = static_cast<float>(environment->fogDensity());
            if (ImGui::SliderFloat("fog density", &fog, 0.0f, 0.02f, "%.4f")) environment->setFogDensity(fog);

            ImGui::End();
            return true;
        });
    }

#else

    vsg::ref_ptr<vsg::Node> createEnvironmentGui(vsg::ref_ptr<vsg::Window>, vsg::ref_ptr<vsg::Viewer>, const vsg::ref_ptr<EnvironmentControls>&)
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
