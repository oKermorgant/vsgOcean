#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>

#include <vsg/maths/vec3.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace vsgOcean
{
    /// The model name -> display color rule of the semantic annotation views.
    ///
    /// THE WHOLE RULE, so anyone can check or reproduce a color offline (dataset
    /// consumers, external tools, a figure in a paper) without running vsgOcean.
    /// All steps are integer / IEEE-double deterministic, so the color of a name
    /// is the same on every machine, every run - unlike a load or discovery
    /// order, a name is a stable identity (the reason the hash is keyed on it).
    ///
    /// 1. h = FNV-1a 32-bit hash of the UTF-8 bytes of the name:
    ///        h = 2166136261u; for each byte b: { h ^= b; h *= 16777619u; }
    ///    (arithmetic mod 2^32; std::hash is NOT used - it is implementation
    ///    defined and would break cross-run reproducibility)
    /// 2. hue = (h * 137.507764) mod 360    (the golden angle, deg)
    /// 3. the color is hsv(hue, 0.55, 0.92) - a pastel by construction, chosen
    ///    to read well against the dark ocean scene - quantized to 8-bit sRGB.
    ///
    /// Examples (the reference for anyone checking a rendering):
    ///     "wamv"     h = 2602818458, hue =  17.51 deg, #eb8f6a
    ///     "islands"  h =  383860813, hue = 202.85 deg, #6ab9eb
    ///     "floatgen" h = 1776443093, hue =  71.67 deg, #d1eb6a
    ///     "bluerov2" h = 3609239590, hue = 121.18 deg, #6aeb6c
    ///     "wreck"    h = 2022291049, hue = 105.20 deg, #89eb6a
    ///
    /// The color is PRESENTATION ONLY: the semantic image itself carries the
    /// compact per-scene model IDs (see SemanticPass), never these colors, and
    /// the id -> name table shipped with a dataset maps back to the names. The
    /// display shader of SemanticOverlay mirrors step 2-3 as a fallback for
    /// scenes with more models than its uniform palette holds (the two must
    /// stay in sync).

    /// Step 1: the FNV-1a 32-bit hash of the name's UTF-8 bytes.
    [[nodiscard]] VSG_OCEAN_EXPORT std::uint32_t nameHash(std::string_view name);

    /// The display color of a model name as "#rrggbb" (lowercase hex, the sRGB
    /// bytes of step 3) - the form to print, log or store next to a dataset.
    [[nodiscard]] VSG_OCEAN_EXPORT std::string hexColor(std::string_view name);

    /// The display color of a model name as linear RGB floats (the sRGB bytes
    /// of step 3 decoded to linear, so an sRGB framebuffer reproduces the bytes
    /// exactly) - the form to feed a shader uniform.
    [[nodiscard]] VSG_OCEAN_EXPORT vsg::vec3 displayColor(std::string_view name);

    /// The display color of a model name as sRGB floats (the bytes of step 3
    /// over 255) - the form a GUI colour swatch expects (ImGui colours are
    /// sRGB-space floats).
    [[nodiscard]] VSG_OCEAN_EXPORT vsg::vec3 srgbColor(std::string_view name);
}
