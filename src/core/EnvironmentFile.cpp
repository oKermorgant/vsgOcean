/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/EnvironmentFile.h>

#include <vsg/io/Logger.h>

#include <fstream>

#ifdef VSGOCEAN_WITH_ENV_FILE
#    include <yaml-cpp/yaml.h>
#endif

namespace vsgOcean
{
#ifdef VSGOCEAN_WITH_ENV_FILE

    namespace
    {
        // apply "key: value" of a YAML node to a setter when the key is present;
        // a malformed value is warned about and skipped, the other keys still apply
        template<typename Setter>
        void readNumber(const YAML::Node& node, const char* key, Setter&& setter)
        {
            if (!node || !node[key]) return;
            try
            {
                setter(node[key].as<double>());
            }
            catch (const YAML::BadConversion&)
            {
                vsg::warn("vsgOcean::loadEnvironmentFile: '", key, ": ", node[key].as<std::string>(), "' is not a number");
            }
        }

        template<typename Setter>
        void readBool(const YAML::Node& node, const char* key, Setter&& setter)
        {
            if (!node || !node[key]) return;
            try
            {
                setter(node[key].as<bool>());
            }
            catch (const YAML::BadConversion&)
            {
                vsg::warn("vsgOcean::loadEnvironmentFile: '", key, ": ", node[key].as<std::string>(), "' is not true or false");
            }
        }

        template<typename Setter>
        void readText(const YAML::Node& node, const char* key, Setter&& setter)
        {
            if (!node || !node[key]) return;
            setter(node[key].as<std::string>());
        }
    }

    bool writeEnvironmentFile(const vsg::Path& path, const Environment& environment, const Wavefield& waves)
    {
        YAML::Emitter out;
        // enough digits to round-trip the doubles, without the binary noise of
        // the full 17 (0.4 would print as 0.40000000000000002)
        out.SetDoublePrecision(10);
        out.SetFloatPrecision(6);
        out << YAML::BeginMap;

        out << YAML::Key << "sky" << YAML::BeginMap;
        out << YAML::Key << "sun_elevation_deg" << environment.sunElevation();
        out << YAML::Key << "sun_azimuth_deg" << environment.sunAzimuth();
        out << YAML::Key << "cloud_cover_oktas" << environment.cloudiness();
        out << YAML::Key << "rain_rate" << environment.rainRate();
        out << YAML::Key << "fog_density" << environment.fogDensity();
        out << YAML::Key << "latitude_deg" << environment.latitude();
        out << YAML::Key << "longitude_deg" << environment.longitude();
        out << YAML::EndMap;

        out << YAML::Key << "wind" << YAML::BeginMap;
        out << YAML::Key << "speed_m_s" << environment.windSpeed();
        out << YAML::Key << "direction_deg" << environment.windDirection();
        out << YAML::EndMap;

        const auto& composition = environment.waterComposition();
        const auto& current = environment.waterCurrent();
        out << YAML::Key << "water" << YAML::BeginMap;
        // a Jerlov name, or Custom when the composition was set directly; the
        // composition keys can tweak a preset on load (a value equal to the
        // preset's snaps back to its name)
        out << YAML::Key << "type" << environment.jerlovName();
        out << YAML::Key << "chlorophyll_mg_m3" << composition.chlorophyll;
        out << YAML::Key << "cdom_a440_1_m" << composition.cdom;
        out << YAML::Key << "sediment_g_m3" << composition.sediment;
        out << YAML::Key << "sea_level_m" << environment.seaLevel();
        out << YAML::Key << "marine_snow_density" << environment.marineSnowDensity();
        out << YAML::Key << "dof_blur" << environment.underwaterBlur();
        out << YAML::Key << "current_east_m_s" << current.x;
        out << YAML::Key << "current_north_m_s" << current.y;
        out << YAML::EndMap;

        const auto fft = waves.fftParameters();
        out << YAML::Key << "waves" << YAML::BeginMap;
        // the model selects which parameter set the surface uses
        out << YAML::Key << "model" << (waves.model() == Wavefield::Model::FFT ? "fft" : "gerstner");
        out << YAML::Key << "foam" << waves.foam();
        out << YAML::Key << "gerstner" << YAML::BeginMap;
        out << YAML::Key << "amplitude_m" << waves.amplitude();
        out << YAML::Key << "period_s" << waves.period();
        out << YAML::Key << "steepness" << waves.steepness();
        out << YAML::Key << "components" << waves.number();
        out << YAML::Key << "scale" << waves.scale();
        out << YAML::Key << "angle_deg" << vsg::degrees(waves.angle());
        out << YAML::EndMap;
        out << YAML::Key << "fft" << YAML::BeginMap;
        out << YAML::Key << "tile_size_m" << fft.tileSize;
        out << YAML::Key << "choppiness" << fft.choppiness;
        out << YAML::Key << "wave_age" << fft.capOmegaC;
        out << YAML::Key << "directional_spread" << fft.spread;
        out << YAML::Key << "detail_cascade" << waves.fftDetail();
        out << YAML::EndMap;
        out << YAML::EndMap;

        out << YAML::EndMap;

        std::ofstream stream{path};
        if (!stream)
        {
            vsg::warn("vsgOcean::writeEnvironmentFile: cannot write '", path, "'");
            return false;
        }
        stream << out.c_str();
        if (!stream.good())
        {
            vsg::warn("vsgOcean::writeEnvironmentFile: failed writing '", path, "'");
            return false;
        }
        vsg::info("vsgOcean: wrote the environment to '", path, "'");
        return true;
    }

    bool loadEnvironmentFile(const vsg::Path& path, Environment& environment, Wavefield& waves)
    {
        YAML::Node root;
        try
        {
            root = YAML::LoadFile(std::string(path));
        }
        catch (const YAML::Exception& error)
        {
            vsg::warn("vsgOcean::loadEnvironmentFile: cannot parse '", path, "': ", error.what());
            return false;
        }
        if (!root.IsMap())
        {
            vsg::warn("vsgOcean::loadEnvironmentFile: '", path, "' is not a YAML mapping");
            return false;
        }

        // sky, weather and geolocation
        const auto sky = root["sky"];
        readNumber(sky, "sun_elevation_deg", [&environment](double v) { environment.setSunElevation(v); });
        readNumber(sky, "sun_azimuth_deg", [&environment](double v) { environment.setSunAzimuth(v); });
        readNumber(sky, "cloud_cover_oktas", [&environment](double v) { environment.setCloudiness(v); });
        readNumber(sky, "rain_rate", [&environment](double v) { environment.setRainRate(v); });
        readNumber(sky, "fog_density", [&environment](double v) { environment.setFogDensity(v); });
        readNumber(sky, "latitude_deg", [&environment](double v) { environment.setLatitude(v); });
        readNumber(sky, "longitude_deg", [&environment](double v) { environment.setLongitude(v); });

        // wind
        const auto wind = root["wind"];
        readNumber(wind, "speed_m_s", [&environment](double v) { environment.setWindSpeed(v); });
        readNumber(wind, "direction_deg", [&environment](double v) { environment.setWindDirection(v); });

        // water colour: a Jerlov preset, then the composition keys as overrides on
        // top of it (a tweaked preset; identical values snap back to the preset)
        const auto water = root["water"];
        readText(water, "type", [&environment](const std::string& name) {
            if (const auto type = Environment::jerlovTypeFromName(name)) environment.setJerlovType(*type);
            else if (name != "Custom") vsg::warn("vsgOcean::loadEnvironmentFile: unknown water type '", name, "'");
        });
        auto composition = environment.waterComposition();
        auto composed = false;
        readNumber(water, "chlorophyll_mg_m3", [&composition, &composed](double v) {
            composition.chlorophyll = v;
            composed = true;
        });
        readNumber(water, "cdom_a440_1_m", [&composition, &composed](double v) {
            composition.cdom = v;
            composed = true;
        });
        readNumber(water, "sediment_g_m3", [&composition, &composed](double v) {
            composition.sediment = v;
            composed = true;
        });
        if (composed) environment.setWaterComposition(composition);

        // the rest of the underwater environment
        readNumber(water, "sea_level_m", [&environment](double v) { environment.setSeaLevel(v); });
        readNumber(water, "marine_snow_density", [&environment](double v) { environment.setMarineSnowDensity(v); });
        readBool(water, "dof_blur", [&environment](bool v) { environment.setUnderwaterBlur(v); });
        auto current = environment.waterCurrent();
        readNumber(water, "current_east_m_s", [&environment, &current](double v) {
            current.x = static_cast<float>(v);
            environment.setWaterCurrent(current);
        });
        readNumber(water, "current_north_m_s", [&environment, &current](double v) {
            current.y = static_cast<float>(v);
            environment.setWaterCurrent(current);
        });

        // waves: the model first (it selects which parameter set the surface
        // uses), then both parameter sets - absent keys leave the sea untouched
        const auto wavesNode = root["waves"];
        readText(wavesNode, "model", [&waves](const std::string& model) {
            waves.setModel(model == "fft" ? Wavefield::Model::FFT : Wavefield::Model::CWR);
        });
        const auto gerstner = wavesNode["gerstner"];
        readNumber(gerstner, "amplitude_m", [&waves](double v) { waves.setAmplitude(v); });
        readNumber(gerstner, "period_s", [&waves](double v) { waves.setPeriod(v); });
        readNumber(gerstner, "steepness", [&waves](double v) { waves.setSteepness(v); });
        readNumber(gerstner, "components", [&waves](double v) { waves.setNumber(static_cast<size_t>(std::max(1.0, v))); });
        readNumber(gerstner, "scale", [&waves](double v) { waves.setScale(v); });
        readNumber(gerstner, "angle_deg", [&waves](double v) { waves.setAngle(vsg::radians(v)); });

        const auto fftNode = wavesNode["fft"];
        auto fft = waves.fftParameters();
        auto changed = false;
        readNumber(fftNode, "tile_size_m", [&fft, &changed](double v) {
            fft.tileSize = v;
            changed = true;
        });
        readNumber(fftNode, "choppiness", [&fft, &changed](double v) {
            fft.choppiness = v;
            changed = true;
        });
        readNumber(fftNode, "wave_age", [&fft, &changed](double v) {
            fft.capOmegaC = v;
            changed = true;
        });
        readNumber(fftNode, "directional_spread", [&fft, &changed](double v) {
            fft.spread = v;
            changed = true;
        });
        if (changed) waves.setFftParameters(fft);
        readBool(fftNode, "detail_cascade", [&waves](bool v) { waves.setFftDetail(v); });
        readNumber(wavesNode, "foam", [&waves](double v) { waves.setFoam(v); });
        // the legacy location of the foam key (it rode the FFT map before the
        // Gerstner sea grew whitecaps of its own)
        readNumber(wavesNode["fft"], "foam", [&waves](double v) { waves.setFoam(v); });

        vsg::info("vsgOcean: loaded the environment from '", path, "'");
        return true;
    }

#else

    bool writeEnvironmentFile(const vsg::Path& path, const Environment&, const Wavefield&)
    {
        vsg::warn("vsgOcean was built without environment file support (yaml-cpp not found); cannot write '", path, "'");
        return false;
    }

    bool loadEnvironmentFile(const vsg::Path& path, Environment&, Wavefield&)
    {
        vsg::warn("vsgOcean was built without environment file support (yaml-cpp not found); cannot read '", path, "'");
        return false;
    }

#endif
}
