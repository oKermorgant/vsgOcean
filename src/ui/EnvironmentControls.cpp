/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/ui/EnvironmentControls.h>

#include <algorithm>

namespace vsgOcean
{
    EnvironmentControls::EnvironmentControls(const vsg::ref_ptr<Scene>& scene) :
        scene_(scene)
    {
    }

    void EnvironmentControls::apply(vsg::KeyPressEvent& keyPress)
    {
        // while the GUI captures the input the keys are its - except H, which must
        // always be able to hide the panel
        if (guiCapturesInput && keyPress.keyBase != vsg::KEY_h) return;
        if (!scene_) return;
        const auto environment = scene_->environment();
        const auto surface = scene_->surface();
        const auto wavefield = surface ? surface->wavefield() : nullptr;

        // any sun adjustment takes over from the day cycle animation
        auto adjustSun = [&]()
        {
            sunCycleEnabled = false;
            return true;
        };

        switch (keyPress.keyBase)
        {
        case vsg::KEY_Up:
            if (adjustSun()) environment->setSunElevation(environment->sunElevation() + 2.0);
            break;
        case vsg::KEY_Down:
            if (adjustSun()) environment->setSunElevation(environment->sunElevation() - 2.0);
            break;
        case vsg::KEY_Right:
            if (adjustSun()) environment->setSunAzimuth(environment->sunAzimuth() + 5.0);
            break;
        case vsg::KEY_Left:
            if (adjustSun()) environment->setSunAzimuth(environment->sunAzimuth() - 5.0);
            break;
        case vsg::KEY_Page_Up:
            environment->setSeaLevel(environment->seaLevel() + 0.5);
            break;
        case vsg::KEY_Page_Down:
            environment->setSeaLevel(environment->seaLevel() - 0.5);
            break;
        case vsg::KEY_a:
            if (wavefield) wavefield->setAmplitude(std::max(wavefield->amplitude() - 0.1, 0.0));
            break;
        case vsg::KEY_d:
            if (wavefield) wavefield->setAmplitude(wavefield->amplitude() + 0.1);
            break;
        case vsg::KEY_q:
            if (wavefield) wavefield->setPeriod(std::max(wavefield->period() - 0.5, 1.0));
            break;
        case vsg::KEY_e:
            if (wavefield) wavefield->setPeriod(wavefield->period() + 0.5);
            break;
        case vsg::KEY_w:
            environment->setWindSpeed(environment->windSpeed() + 1.0);
            break;
        case vsg::KEY_s:
            environment->setWindSpeed(std::max(environment->windSpeed() - 1.0, 0.0));
            break;
        case vsg::KEY_z:
        {
            const auto type = std::max(static_cast<int>(environment->jerlovType()) - 1, 0);
            environment->setJerlovType(static_cast<Environment::JerlovType>(type));
            break;
        }
        case vsg::KEY_c:
        {
            const auto type = std::min(static_cast<int>(environment->jerlovType()) + 1,
                                       static_cast<int>(Environment::JerlovType::Coastal9));
            environment->setJerlovType(static_cast<Environment::JerlovType>(type));
            break;
        }
        case vsg::KEY_p:
            scene_->setSurfaceEnabled(!scene_->surfaceEnabled());
            break;
        case vsg::KEY_Space:
            sunCycleEnabled = !sunCycleEnabled;
            break;
        case vsg::KEY_Tab:
        {
            // cycle the camera tracking through the models of the scene: none, then
            // each model in turn, then none again
            const auto names = scene_->modelNames();
            size_t next{0};
            if (const auto& tracked = scene_->trackedFrame(); !tracked.empty())
            {
                const auto it = std::find(names.begin(), names.end(), tracked);
                if (it != names.end()) next = static_cast<size_t>(it - names.begin()) + 1;
            }
            scene_->setTrackedFrame(next < names.size() ? names[next] : std::string{});
            break;
        }
        case vsg::KEY_h:
            panelVisible = !panelVisible;
            break;
        default:
            break;
        }
    }
}
