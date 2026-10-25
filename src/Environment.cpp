/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Environment.h>

#include <cmath>

namespace vsgOcean
{
    namespace
    {
        struct JerlovData
        {
            const char* name;
            vsg::vec3 attenuation; // per-channel diffuse attenuation coefficients (1/m), R/G/B
            vsg::vec3 color;       // approximate water colour, R/G/B
        };

        // Approximate per-channel (R 650nm, G 530nm, B 470nm) values after Jerlov's optical
        // classification of marine waters (N. G. Jerlov, Marine Optics, 1976).
        static const JerlovData jerlovTable[] = {
            /* OceanicI    */ {"OceanicI", {0.29f, 0.051f, 0.033f}, {0.00f, 0.18f, 0.42f}},
            /* OceanicIA   */ {"OceanicIA", {0.29f, 0.056f, 0.040f}, {0.00f, 0.19f, 0.40f}},
            /* OceanicIB   */ {"OceanicIB", {0.30f, 0.065f, 0.050f}, {0.01f, 0.21f, 0.38f}},
            /* OceanicII   */ {"OceanicII", {0.32f, 0.083f, 0.071f}, {0.02f, 0.24f, 0.34f}},
            /* OceanicIII  */ {"OceanicIII", {0.43f, 0.115f, 0.117f}, {0.03f, 0.27f, 0.30f}},
            /* Coastal1    */ {"Coastal1", {0.49f, 0.147f, 0.159f}, {0.05f, 0.30f, 0.27f}},
            /* Coastal3    */ {"Coastal3", {0.58f, 0.203f, 0.246f}, {0.08f, 0.32f, 0.23f}},
            /* Coastal5    */ {"Coastal5", {0.70f, 0.290f, 0.349f}, {0.12f, 0.34f, 0.20f}},
            /* Coastal9    */ {"Coastal9", {1.35f, 0.642f, 0.730f}, {0.20f, 0.36f, 0.18f}},
        };
    }

    Environment::Environment() :
        sunElevation_(35.0),
        sunAzimuth_(135.0),
        fogDensity_(0.001),
        fogColor_(0.65f, 0.72f, 0.80f),
        windSpeed_(5.0),
        windDirection_(0.0),
        jerlovType_(JerlovType::OceanicI),
        submersionDepth_(0.0),
        latitude_(0.0),
        longitude_(0.0)
    {
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
        fogColor_ = color;
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

    void Environment::setJerlovType(JerlovType type)
    {
        jerlovType_ = type;
    }

    void Environment::setSubmersionDepth(double metres)
    {
        submersionDepth_ = std::max(metres, 0.0);
    }

    std::string Environment::jerlovName() const
    {
        return jerlovTable[static_cast<uint8_t>(jerlovType_)].name;
    }

    vsg::vec3 Environment::underwaterAttenuation() const
    {
        return jerlovTable[static_cast<uint8_t>(jerlovType_)].attenuation;
    }

    vsg::vec3 Environment::underwaterColor() const
    {
        return jerlovTable[static_cast<uint8_t>(jerlovType_)].color;
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
