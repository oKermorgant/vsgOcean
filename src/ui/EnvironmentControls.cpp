/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

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
        // the environment tuning lives in the GUI widgets; the keys only toggle the
        // widgets themselves (and must always be able to hide a capturing GUI), and
        // the export / view / tracking keys work through a capture as well
        if (guiCapturesInput && keyPress.keyBase != vsg::KEY_h && keyPress.keyBase != vsg::KEY_c
            && keyPress.keyBase != vsg::KEY_s && keyPress.keyBase != vsg::KEY_o && keyPress.keyBase != vsg::KEY_u
            && keyPress.keyBase != vsg::KEY_l && keyPress.keyBase != vsg::KEY_e && keyPress.keyBase != vsg::KEY_r
            && keyPress.keyBase != vsg::KEY_Tab)
        {
            return;
        }
        if (!scene_) return;

        switch (keyPress.keyBase)
        {
        case vsg::KEY_h:
            helpVisible = !helpVisible;
            break;
        case vsg::KEY_c:
            camerasVisible = !camerasVisible;
            break;
        case vsg::KEY_s:
            skyVisible = !skyVisible;
            break;
        case vsg::KEY_o:
            surfaceVisible = !surfaceVisible;
            break;
        case vsg::KEY_u:
            underwaterVisible = !underwaterVisible;
            break;
        case vsg::KEY_l:
            lightsVisible = !lightsVisible;
            break;
        case vsg::KEY_r:
            // cycle the semantic annotation view: photo, full annotation, 50%
            // overlay, back to the photo
            semanticMode = (semanticMode + 1) % 3;
            break;
        case vsg::KEY_e:
            // export the environment as tuned so far (a structured YAML file,
            // reloadable with the example's --env flag); the path is logged
            if (!scene_->saveEnvironment(environmentExportPath))
            {
                vsg::warn("vsgOcean: could not export the environment (built without environment file support?)");
            }
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
        default:
            break;
        }
    }
}
