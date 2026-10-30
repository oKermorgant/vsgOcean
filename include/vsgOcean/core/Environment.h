#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>

#include <vsg/all.h>

#include <cstdint>
#include <string>

namespace vsgOcean
{
    /// Environmental state shared by the sky, ocean surface and underwater rendering:
    /// sun position and atmospheric fog, wave parameters, and the underwater water type.
    ///
    /// All values have reasonable defaults and can be changed at any time through the set functions.
    class VSG_OCEAN_EXPORT Environment : public vsg::Inherit<vsg::Object, Environment>
    {
    public:
        /// Jerlov optical water types, from the clearest open ocean to the most turbid coastal water
        /// (after N. G. Jerlov's classification of marine waters).
        enum class JerlovType : uint8_t
        {
            OceanicI,
            OceanicIA,
            OceanicIB,
            OceanicII,
            OceanicIII,
            Coastal1,
            Coastal3,
            Coastal5,
            Coastal9
        };

        Environment() = default;

        // --- sky ---

        /// Sun elevation in degrees above the horizon [-90, 90]. Default 35.
        void setSunElevation(double degrees);
        double sunElevation() const { return sunElevation_; }

        /// Sun azimuth in degrees in the horizontal plane, measured from the +X axis [0, 360). Default 135.
        void setSunAzimuth(double degrees);
        double sunAzimuth() const { return sunAzimuth_; }

        /// Unit vector pointing towards the sun, computed from elevation and azimuth (+Z up).
        [[nodiscard]] vsg::dvec3 sunDirection() const;

        /// Atmospheric fog density; 0 disables fogging. Default 0.001.
        void setFogDensity(double density);
        double fogDensity() const { return fogDensity_; }

        /// Atmospheric fog color. Default light blue grey.
        void setFogColor(const vsg::vec3& color);
        const vsg::vec3& fogColor() const { return fogColor_; }

        // --- ocean surface ---

        /// Wind speed in m/s driving the waves. Default 5.
        void setWindSpeed(double speed);
        double windSpeed() const { return windSpeed_; }

        /// Wind direction in degrees in the horizontal plane, measured from the +X axis [0, 360). Default 0.
        void setWindDirection(double degrees);
        double windDirection() const { return windDirection_; }

        // --- underwater ---

        /// Jerlov water type controlling underwater light attenuation and water colour. Default OceanicI.
        void setJerlovType(JerlovType type);
        JerlovType jerlovType() const { return jerlovType_; }

        /// Name of the current Jerlov water type (e.g. "Coastal5"), for logging and UIs.
        [[nodiscard]] std::string jerlovName() const;

        /// Mean water level (sea level) in metres above the origin z = 0. Default 0.
        /// Raises or lowers the ocean surface, and the underwater rendering with it.
        void setSeaLevel(double metres);
        double seaLevel() const { return seaLevel_; }

        // --- geolocation ---
        // The horizontal frame convention of vsgOcean: +X East, +Y North, +Z Up.

        /// Latitude in degrees, North positive. Default 0.
        void setLatitude(double degrees);
        double latitude() const { return latitude_; }

        /// Longitude in degrees, East positive. Default 0.
        void setLongitude(double degrees);
        double longitude() const { return longitude_; }

        /// Approximate per-channel diffuse attenuation coefficients (1/m) of the current Jerlov
        /// water type, after Jerlov's optical classification tables; used by underwater rendering.
        [[nodiscard]] vsg::vec3 underwaterAttenuation() const;

        /// Approximate water colour of the current Jerlov water type.
        [[nodiscard]] vsg::vec3 underwaterColor() const;

    protected:
        double sunElevation_{35.0};
        double sunAzimuth_{135.0};
        double fogDensity_{0.001};
        vsg::vec3 fogColor_ = {0.65f, 0.72f, 0.80f}; // light blue grey
        double windSpeed_{5.0};
        double windDirection_{0.0};
        JerlovType jerlovType_ = JerlovType::OceanicI;
        double seaLevel_{0.0};
        double latitude_{0.0};
        double longitude_{0.0};
    };
}
