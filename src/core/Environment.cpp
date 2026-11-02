/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Environment.h>

#include <array>
#include <cmath>

namespace vsgOcean
{
    namespace
    {
        struct JerlovData
        {
            const char* name;
            WaterComposition composition;
        };

        // the Jerlov types as compositions, fitted so that the bio-optical model reproduces the
        // Jerlov attenuation (N. G. Jerlov, Marine Optics, 1976; the five coastal types missing from
        // the usual three-band tables are interpolated) to about 5 % in green and blue and 20-30 %
        // low in the red of the most turbid types; the chlorophyll follows the clarity axis
        constexpr std::array<JerlovData, Environment::jerlovTypeCount> jerlovTable{{
            /* OceanicI    */ {"OceanicI", {0.0246, 0.0142, 0.0}},
            /* OceanicIA   */ {"OceanicIA", {0.0375, 0.0234, 0.0}},
            /* OceanicIB   */ {"OceanicIB", {0.064, 0.0372, 0.0}},
            /* OceanicII   */ {"OceanicII", {0.134, 0.0662, 0.0113}},
            /* OceanicIII  */ {"OceanicIII", {0.239, 0.121, 0.129}},
            /* Coastal1    */ {"Coastal1", {0.45, 0.12, 0.51}},
            /* Coastal2    */ {"Coastal2", {0.809, 0.105, 0.889}},
            /* Coastal3    */ {"Coastal3", {1.49, 0.0829, 1.33}},
            /* Coastal4    */ {"Coastal4", {2.62, 0.0204, 2.01}},
            /* Coastal5    */ {"Coastal5", {3.9, 0.0, 2.55}},
            /* Coastal6    */ {"Coastal6", {5.57, 0.0, 3.16}},
            /* Coastal7    */ {"Coastal7", {8.24, 0.0, 3.88}},
            /* Coastal8    */ {"Coastal8", {14.2, 0.0, 4.57}},
            /* Coastal9    */ {"Coastal9", {30.0, 0.0, 4.97}},
        }};
    }

    void Environment::setSunElevation(double degrees)
    {
        sunElevation_ = std::clamp(degrees, -90.0, 90.0);
    }

    void Environment::setSunAzimuth(double degrees)
    {
        sunAzimuth_ = std::fmod(degrees, 360.0);
        if (sunAzimuth_ < 0.0) sunAzimuth_ += 360.0;
    }

    vsg::dvec3 Environment::sunDirection() const
    {
        const auto elevation = vsg::radians(sunElevation_);
        const auto azimuth = vsg::radians(sunAzimuth_);
        return {std::cos(elevation) * std::cos(azimuth), std::cos(elevation) * std::sin(azimuth), std::sin(elevation)};
    }

    void Environment::setFogDensity(double density)
    {
        fogDensity_ = std::max(density, 0.0);
    }

    void Environment::setFogColor(const vsg::vec3& color)
    {
        // an explicit override, valid until the sun or the cloud cover moves again
        fogColorOverride_ = color;
    }

    vsg::vec3 Environment::fogColor() const
    {
        if (fogColorOverride_) return *fogColorOverride_;

        // the haze is the aerial perspective of the sky's own horizon band: keep
        // this palette in sync with sky.frag's skyColor() (the night floors and
        // the day zenith/horizon), evaluated at the horizon and averaged over
        // the azimuth - the dusk glow contributes its azimuthal average (the
        // pow(towardsSun, 4) lobe averages to ~0.2 of its peak)
        const auto sunHeight = static_cast<float>(sunDirection().z);
        const auto ramp = std::clamp((sunHeight + 0.05f) / 0.30f, 0.0f, 1.0f);
        const float dayF = ramp * ramp * (3.0f - 2.0f * ramp);
        const float dusk = 1.0f - std::clamp(std::abs(sunHeight) / 0.2f, 0.0f, 1.0f);

        const vsg::vec3 horizonDay{0.75f, 0.85f, 0.95f};
        const vsg::vec3 horizonNight{0.0005f, 0.0008f, 0.0015f};
        vsg::vec3 haze = vsg::mix(horizonNight, horizonDay, dayF);

        // a hint of the dusk glow, averaged over the azimuth
        haze += vsg::vec3(0.9f, 0.45f, 0.15f) * (0.8f * 0.2f * dusk);

        // overcast flattens the sky to a daylight grey (the same overcast term
        // as sky.frag), dark at night
        const float clouds = static_cast<float>(cloudiness_ / 8.0);
        const vsg::vec3 overcast{0.42f, 0.45f, 0.50f};
        haze = vsg::mix(haze, overcast * dayF, clouds * 0.9f);
        return haze;
    }

    void Environment::setWindSpeed(double speed)
    {
        windSpeed_ = std::max(speed, 0.0);
    }

    void Environment::setWindDirection(double degrees)
    {
        windDirection_ = std::fmod(degrees, 360.0);
        if (windDirection_ < 0.0) windDirection_ += 360.0;
    }

    Environment::Environment()
    {
        setJerlovType(jerlovType_);
    }

    void Environment::setJerlovType(JerlovType type)
    {
        jerlovType_ = type;
        customWater_ = false;
        composition_ = jerlovTable[static_cast<uint8_t>(type)].composition;
        optics_ = computeWaterOptics(composition_);
    }

    void Environment::setWaterComposition(const WaterComposition& composition)
    {
        composition_ = composition;
        customWater_ = true;
        optics_ = computeWaterOptics(composition_);
    }

    void Environment::setSeaLevel(double metres)
    {
        seaLevel_ = metres;
    }

    void Environment::setMarineSnowDensity(double density)
    {
        marineSnowDensity_ = std::clamp(density, 0.0, 100.0);
    }

    void Environment::setRainRate(double rate)
    {
        rainRate_ = std::clamp(rate, 0.0, 1.0);
    }

    void Environment::setWaterCurrent(const vsg::vec3& current)
    {
        waterCurrent_ = current;
    }

    void Environment::setCloudiness(double oktas)
    {
        cloudiness_ = std::clamp(oktas, 0.0, 8.0);
    }

    void Environment::setUnderwaterBlur(bool enabled)
    {
        underwaterBlur_ = enabled;
    }

    std::string Environment::jerlovName() const
    {
        return customWater_ ? "Custom" : jerlovTable[static_cast<uint8_t>(jerlovType_)].name;
    }

    vsg::vec3 Environment::underwaterScattering() const
    {
        return optics_.scattering;
    }

    vsg::vec3 Environment::underwaterAttenuation() const
    {
        return optics_.attenuation;
    }

    vsg::vec3 Environment::underwaterColor() const
    {
        return optics_.color;
    }

    void Environment::setLatitude(double degrees)
    {
        latitude_ = std::clamp(degrees, -90.0, 90.0);
    }

    void Environment::setLongitude(double degrees)
    {
        longitude_ = std::clamp(degrees, -180.0, 180.0);
    }
}
