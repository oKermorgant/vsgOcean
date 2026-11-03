/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/semantic/SemanticPalette.h>

#include <cmath>
#include <string>

namespace vsgOcean
{
    namespace
    {
        // hue -> RGB, the standard hexagonal HSV conversion; then the sRGB
        // transfer function inverted, so the linear result reproduces the sRGB
        // bytes once the (sRGB) framebuffer encodes it again
        vsg::vec3 hsvToLinearRgb(double hue, double saturation, double value)
        {
            const auto c = value * saturation;
            const auto hp = std::fmod(hue, 360.0) / 60.0;
            const auto x = c * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
            auto r = 0.0, g = 0.0, b = 0.0;
            if (hp < 1.0) { r = c; g = x; }
            else if (hp < 2.0) { r = x; g = c; }
            else if (hp < 3.0) { g = c; b = x; }
            else if (hp < 4.0) { g = x; b = c; }
            else if (hp < 5.0) { r = x; b = c; }
            else { r = c; b = x; }
            const auto m = value - c;

            const auto toLinear = [m](double channel) {
                const auto srgb = std::lround((channel + m) * 255.0) / 255.0; // quantized: the bytes are the rule
                return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
            };
            return {static_cast<float>(toLinear(r)), static_cast<float>(toLinear(g)), static_cast<float>(toLinear(b))};
        }

        std::string toHex(double channel)
        {
            const auto byte = static_cast<unsigned>(std::lround(channel * 255.0));
            auto digits = "0123456789abcdef";
            std::string hex;
            hex += digits[(byte >> 4) & 0xf];
            hex += digits[byte & 0xf];
            return hex;
        }
    }

    std::uint32_t nameHash(std::string_view name)
    {
        auto hash = 2166136261u; // the FNV-1a offset basis
        for (const auto byte : name)
        {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 16777619u; // the FNV-1a prime, mod 2^32
        }
        return hash;
    }

    std::string hexColor(std::string_view name)
    {
        // steps 2-3 of the rule (SemanticPalette.h): golden-angle hue from the
        // hash, fixed pastel saturation / value - the hex of the sRGB bytes
        const auto hue = static_cast<double>(nameHash(name)) * 137.507764;
        const auto color = hsvToLinearRgb(hue, 0.55, 0.92);

        // hex works on the sRGB bytes: linear was only needed for the shader
        const auto toSrgbByte = [](float linear) {
            const auto srgb = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            return static_cast<double>(srgb);
        };
        return "#" + toHex(toSrgbByte(color.r)) + toHex(toSrgbByte(color.g)) + toHex(toSrgbByte(color.b));
    }

    vsg::vec3 displayColor(std::string_view name)
    {
        const auto hue = static_cast<double>(nameHash(name)) * 137.507764;
        return hsvToLinearRgb(hue, 0.55, 0.92);
    }

}
