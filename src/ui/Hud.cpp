/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/ui/Hud.h>

#include <vsg/text/Font.h>
#include <vsg/text/StandardLayout.h>
#include <vsg/text/Text.h>

namespace vsgOcean
{
    namespace
    {
        constexpr float lineHeightPixels{18.0f}; // rendered line height target, in pixels
        constexpr float marginPixels{10.0f};    // text margin from the window corner

        // candidate font files, tried in order (the first that loads wins);
        // override with the VSGOCEAN_FONT environment variable
        const char* fontCandidates[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
            "/System/Library/Fonts/Supplemental/Courier New.ttf",
            "C:/Windows/Fonts/consola.ttf",
        };
    }

    Hud::Hud(vsg::ref_ptr<vsg::Options> fontOptions, VkExtent2D extent) :
        fontOptions_(std::move(fontOptions)),
        extent_(extent)
    {
        if (const auto overrideFont = vsg::getEnv("VSGOCEAN_FONT"); !overrideFont.empty())
        {
            font_ = vsg::read_cast<vsg::Font>(vsg::Path(overrideFont), fontOptions_);
        }
        for (const char* candidate : fontCandidates)
        {
            if (font_) break;
            font_ = vsg::read_cast<vsg::Font>(vsg::Path(candidate), fontOptions_);
        }
        if (!font_)
        {
            vsg::warn("vsgOcean::Hud - no font found (set VSGOCEAN_FONT); the HUD renders nothing");
            return;
        }

        // world units per pixel, so a text line is lineHeightPixels tall
        const auto pixel = std::max(font_->height, 1.0f) / lineHeightPixels;

        auto projection = vsg::Orthographic::create(0.0, static_cast<double>(extent_.width) * pixel,
                                                    0.0, static_cast<double>(extent_.height) * pixel,
                                                    0.0, 2.0);
        auto viewMatrix = vsg::LookAt::create(vsg::dvec3(0.0, 0.0, -1.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 1.0, 0.0));
        camera_ = vsg::Camera::create(projection, viewMatrix, vsg::ViewportState::create(extent_));

        view_ = vsg::View::create(camera_);
        addChild(view_);
    }

    void Hud::setLines(std::vector<std::string> lines)
    {
        lines_ = std::move(lines);
        build();
    }


    void Hud::build()
    {
        if (!font_ || lines_.empty()) return;

        // world units per pixel, so a text line is lineHeightPixels tall
        const auto pixel = std::max(font_->height, 1.0f) / lineHeightPixels;

        std::string joined;
        for (size_t i = 0; i < lines_.size(); ++i)
        {
            if (i > 0) joined += '\n';
            joined += lines_[i];
        }

        auto text = vsg::Text::create();
        text->text = vsg::stringValue::create(joined);
        text->font = font_;
        auto layout = vsg::StandardLayout::create();
        layout->position = vsg::vec3(marginPixels * pixel, (static_cast<float>(extent_.height) - marginPixels) * pixel, 0.0f);
        layout->color = vsg::vec4(0.92f, 0.94f, 1.0f, 0.85f);
        text->layout = layout;
        text->setup(0, fontOptions_);

        view_->children.clear();
        view_->addChild(text);
    }
}
