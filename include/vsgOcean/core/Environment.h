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

        /// Atmospheric fog density, normalized 0..1: 0 disables foging, 1.0 is a
        /// practical maximum (an extinction of 0.05 1/m - at that point a boat can
        /// barely see its own bow), 0.1 a hazy sea (~600 m visibility), 0.02 a clear
        /// day (~2 km). The haze follows Beer-Lambert over the view distance,
        /// 1 - exp(-extinction * distance), with the extinction from
        /// fogExtinction(). Default 0.02 (the example islands sit hundreds of
        /// metres out; at the hazy 0.1 they disappear into the fog).
        void setFogDensity(double density);
        double fogDensity() const { return fogDensity_; }

        /// The atmospheric extinction coefficient of the fog, in 1/m: the density
        /// scaled to its physical range (density 1.0 -> 0.05 1/m).
        double fogExtinction() const { return fogDensity_ * 0.05; }

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

        /// Marine snow density, in units of the full field: 0 disables the
        /// particles, 1.0 the full particle pool, 10.0 ten times it (a
        /// storm-stirred water column). Scales the number of visible particles
        /// drifting around the camera underwater. Default 0.6.
        void setMarineSnowDensity(double density);
        double marineSnowDensity() const { return marineSnowDensity_; }

        /// Cloud cover above the water on the classical meteorological okta scale,
        /// 0..8: 0 a clear sky, 8 overcast (each okta is one eighth of the sky).
        /// INDEPENDENT of the fog (the distance haze): the clouds dim the sun disc
        /// and its glow, hide the stars, grey the sky gradient and dim the scene's
        /// sun lighting - a cloudy night has no constellations. Default 0 (clear).
        void setCloudiness(double oktas);
        double cloudiness() const { return cloudiness_; }

        /// The underwater depth-of-field blur (as osgOcean's underwater DOF): the
        /// water's turbidity blurs the view with the water path length - objects
        /// near the camera stay sharp, distant ones dissolve into the water colour.
        /// Follows the Jerlov beam attenuation. Default on.
        void setUnderwaterBlur(bool enabled);
        bool underwaterBlur() const { return underwaterBlur_; }

        /// The water current the underwater environment drifts with, in m/s in the
        /// world frame (+X East, +Y North, +Z Up): the marine snow advects along it.
        /// Driven at runtime from the outside (a dynamics simulator / a topic).
        void setWaterCurrent(const vsg::vec3& current);
        const vsg::vec3& waterCurrent() const { return waterCurrent_; }

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

        /// The approximate total per-channel scattering coefficients (1/m, R/G/B)
        /// of the selected Jerlov water type - the singly-scattered light follows
        /// the Henyey-Greenstein phase (g ~ 0.92); used by the volumetric light
        /// transport (lamp cones, sun shafts).
        [[nodiscard]] vsg::vec3 underwaterScattering() const;

        /// Approximate water colour of the current Jerlov water type.
        [[nodiscard]] vsg::vec3 underwaterColor() const;

    protected:
        double sunElevation_{35.0};
        double sunAzimuth_{135.0};
        double fogDensity_{0.02};
        vsg::vec3 fogColor_ = {0.65f, 0.72f, 0.80f}; // light blue grey
        double windSpeed_{5.0};
        double windDirection_{0.0};
        JerlovType jerlovType_ = JerlovType::OceanicI;
        double seaLevel_{0.0};
        double marineSnowDensity_{0.6};
        double cloudiness_{0.0}; // oktas, 0..8
        bool underwaterBlur_{true};
        vsg::vec3 waterCurrent_{0.0f, 0.0f, 0.0f}; // m/s, world frame
        double latitude_{0.0};
        double longitude_{0.0};
    };
}
