/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#pragma once

#include <vsg/maths/vec3.h>

#include <vsgOcean/core/Export.h>

namespace vsgOcean
{
    /// What is in the water: the three optically active constituents that make a water body
    /// clear or turbid, blue, green or brown. This is the continuous description behind the
    /// discrete Jerlov water types (see jerlovComposition()).
    struct WaterComposition
    {
        double chlorophyll = 0.1; ///< phytoplankton chlorophyll-a [mg/m3]: ~0.03 clear ocean, 1 coastal, 10+ bloom
        double cdom = 0.01;       ///< coloured dissolved organic matter, absorption at 440 nm [1/m]: yellow-brown "gelbstoff"
        double sediment = 0.0;    ///< suspended mineral and detritus particles [g/m3]

        bool operator==(const WaterComposition& other) const
        {
            return chlorophyll == other.chlorophyll && cdom == other.cdom && sediment == other.sediment;
        }
    };

    /// The inherent and apparent optical properties of a water body at the three rendered
    /// wavelengths, R = 650 nm, G = 530 nm, B = 470 nm.
    struct WaterOptics
    {
        vsg::vec3 attenuation; ///< diffuse attenuation coefficient Kd [1/m]
        vsg::vec3 scattering;  ///< total scattering coefficient b [1/m]
        vsg::vec3 color;       ///< colour of the water seen from above (sub-surface reflectance)
    };

    /// A simple bio-optical model: pure-water absorption and scattering plus the absorption and
    /// scattering of the three constituents (spectral shapes after Bricaud et al. 1998 for
    /// phytoplankton, an exponential for CDOM and detritus, Loisel & Morel 1998 for particle
    /// scattering), combined into Kd with the Lee et al. 2005 closure. Approximate: it follows
    /// the trends of the published models, not their coefficients to the digit.
    [[nodiscard]] VSG_OCEAN_EXPORT WaterOptics computeWaterOptics(const WaterComposition& composition);
}
