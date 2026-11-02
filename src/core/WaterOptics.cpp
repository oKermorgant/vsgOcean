/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/WaterOptics.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace vsgOcean
{
    namespace
    {
        constexpr std::array<double, 3> wavelength{650.0, 530.0, 470.0}; // nm, R G B

        // pure water: absorption (Pope & Fry 1997, rounded) and Rayleigh-like scattering
        constexpr std::array<double, 3> pureAbsorption{0.340, 0.0434, 0.0156};
        constexpr double pureScatteringAt550 = 0.0017;

        // phytoplankton absorption a = A * chl^E (Bricaud et al. 1998, rounded to the three bands)
        constexpr std::array<double, 3> phytoplanktonA{0.0095, 0.0085, 0.0270};
        constexpr std::array<double, 3> phytoplanktonE{0.80, 0.66, 0.63};

        constexpr double cdomSlope = 0.014;     // 1/nm
        constexpr double detritusSlope = 0.008; // 1/nm
        constexpr double detritusAbsorption = 0.04;       // m2/g at 440 nm
        constexpr double phytoplanktonScattering = 0.30;  // b550 = 0.30 chl^0.62 (Loisel & Morel 1998)
        constexpr double mineralScattering = 0.45;        // m2/g at 550 nm
        constexpr double phytoplanktonBackscatter = 0.01; // backscattering ratios
        constexpr double mineralBackscatter = 0.035;

        // reflectance Rrs ~ 0.095 bb / (a + bb), saturated and scaled to the display colour
        constexpr double reflectanceFactor = 0.095;
        constexpr double colorGain = 134.0;
    }

    WaterOptics computeWaterOptics(const WaterComposition& composition)
    {
        const auto chlorophyll = std::max(composition.chlorophyll, 0.0);
        const auto cdom = std::max(composition.cdom, 0.0);
        const auto sediment = std::max(composition.sediment, 0.0);

        std::array<float, 3> attenuation{};
        std::array<float, 3> scattering{};
        std::array<float, 3> color{};
        auto totalScattering = 0.0;

        for (size_t i = 0; i < 3; ++i)
        {
            const auto lambda = wavelength[i];
            const auto absorption = pureAbsorption[i] + phytoplanktonA[i] * std::pow(chlorophyll, phytoplanktonE[i]) +
                                    cdom * std::exp(-cdomSlope * (lambda - 440.0)) +
                                    detritusAbsorption * sediment * std::exp(-detritusSlope * (lambda - 440.0));

            const auto pureScattering = pureScatteringAt550 * std::pow(550.0 / lambda, 4.3);
            const auto phytoplankton = phytoplanktonScattering * std::pow(chlorophyll, 0.62) * (550.0 / lambda);
            const auto mineral = mineralScattering * sediment * std::sqrt(550.0 / lambda);

            const auto backscatter = 0.5 * pureScattering + phytoplanktonBackscatter * phytoplankton + mineralBackscatter * mineral;

            // Lee et al. 2005, sun at the zenith
            attenuation[i] = static_cast<float>(absorption + 4.18 * (1.0 - 0.52 * std::exp(-10.9 * absorption)) * backscatter);
            scattering[i] = static_cast<float>(pureScattering + phytoplankton + mineral);
            color[i] = static_cast<float>(1.0 - std::exp(-colorGain * reflectanceFactor * backscatter / (absorption + backscatter)));
            totalScattering += scattering[i] / 3.0;
        }

        // a tone mapping rather than physics: the turbid waters are bright in reality, but a
        // display-referred colour of a milky sea is darkened with the scattering so that the
        // clear-ocean blue and the coastal green keep a similar brightness
        const auto tone = static_cast<float>(1.0 / (1.0 + 0.3 * totalScattering));
        for (auto& c : color) c *= tone;

        return {{attenuation[0], attenuation[1], attenuation[2]},
                {scattering[0], scattering[1], scattering[2]},
                {color[0], color[1], color[2]}};
    }
}
