#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Environment.h>
#include <vsgOcean/core/Wavefield.h>

#include <string_view>

namespace vsgOcean
{
    /// The environment state of a running scene as a structured YAML file: the sky and
    /// weather, the wind, the water optics and the waves - everything the runtime
    /// widgets tune, and nothing else (no lights, camera or capture settings).
    ///
    /// Written by vsgOcean::Scene::saveEnvironment (the E key of the environment
    /// controls / the export button of the help widget) and read back by
    /// Scene::loadEnvironment (the example's --env flag), so an environment tuned
    /// in the GUI is reproducible from the command line - and shareable, the file
    /// being plain YAML structured by group:
    ///
    ///     sky:
    ///       sun_elevation_deg: 35
    ///       sun_azimuth_deg: 135
    ///       cloud_cover_oktas: 0        # oktas, 0..8
    ///       rain_rate: 0                # 0..1
    ///       fog_density: 0.02
    ///       latitude_deg: 0
    ///       longitude_deg: 0
    ///     wind:
    ///       speed_m_s: 5
    ///       direction_deg: 0
    ///     water:
    ///       type: Coastal3             # a Jerlov name, or Custom when composed directly
    ///       chlorophyll_mg_m3: 1.49     # can tweak the type's preset on load
    ///       cdom_a440_1_m: 0.0829
    ///       sediment_g_m3: 1.33
    ///       sea_level_m: 0
    ///       marine_snow_density: 0.6
    ///       dof_blur: false
    ///       current_east_m_s: 0
    ///       current_north_m_s: 0
    ///     waves:
    ///       model: gerstner             # gerstner or fft
    ///       gerstner:
    ///         amplitude_m: 0.1
    ///         period_s: 5
    ///         steepness: 1
    ///         components: 8
    ///         scale: 2
    ///         angle_deg: 36
    ///       fft:
    ///         tile_size_m: 256
    ///         choppiness: 0.6
    ///         wave_age: 0.84
    ///         directional_spread: 5
    ///         detail_cascade: false
    ///         foam: 0
    ///
    /// Every key, group and subgroup is optional on load: absent entries leave
    /// their parameter untouched, so hand-written files can be partial.
    ///
    /// The support is built when yaml-cpp is found (VSGOCEAN_WITH_ENV_FILE, a
    /// common system package); without it the functions warn and return false.
    [[nodiscard]] VSG_OCEAN_EXPORT bool writeEnvironmentFile(const vsg::Path& path, const Environment& environment,
                                                             const Wavefield& waves);

    /// Read a structured YAML environment file and apply it (see writeEnvironmentFile
    /// for the groups and keys). Returns false - with a warning logged - when the
    /// support is disabled, the file cannot be parsed, or a value is malformed; the
    /// keys read up to that point are applied.
    [[nodiscard]] VSG_OCEAN_EXPORT bool loadEnvironmentFile(const vsg::Path& path, Environment& environment,
                                                             Wavefield& waves);
}
