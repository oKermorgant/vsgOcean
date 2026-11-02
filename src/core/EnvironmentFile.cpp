/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/EnvironmentFile.h>

#include <vsg/io/Logger.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vsgOcean
{
    namespace
    {
        // trim the whitespace of both ends of a view
        std::string_view trim(std::string_view text)
        {
            const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
            while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
            while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
            return text;
        }

        // A flat YAML scalar: "key: value" pairs, one per line, '#' comments and blank
        // lines skipped - deliberately the subset the writer emits, enough for the
        // hand-edited files too (no nesting, no quoting, no flow sequences).
        using KeyValue = std::unordered_map<std::string, std::string>;

        [[nodiscard]] bool parseFlatYaml(const vsg::Path& path, KeyValue& values)
        {
            std::ifstream in{path};
            if (!in)
            {
                vsg::warn("vsgOcean::loadEnvironmentFile: cannot open '", path, "'");
                return false;
            }

            std::string line;
            size_t lineNumber{0};
            while (std::getline(in, line))
            {
                ++lineNumber;
                const auto text = trim(line);
                if (text.empty() || text.front() == '#') continue;

                const auto separator = text.find(':');
                if (separator == std::string_view::npos)
                {
                    vsg::warn("vsgOcean::loadEnvironmentFile: '", path, ":", lineNumber,
                              "' is not a 'key: value' line: '", text, "'");
                    return false;
                }
                values.emplace(std::string(trim(text.substr(0, separator))),
                                std::string(trim(text.substr(separator + 1))));
            }
            return true;
        }

        [[nodiscard]] std::optional<double> asNumber(const KeyValue& values, std::string_view key)
        {
            const auto it = values.find(std::string(key));
            if (it == values.end()) return {};
            const auto text = std::string_view(it->second);
            double value{0.0};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                vsg::warn("vsgOcean::loadEnvironmentFile: '", key, ": ", text, "' is not a number");
                return {};
            }
            return value;
        }

        [[nodiscard]] std::optional<bool> asBool(const KeyValue& values, std::string_view key)
        {
            const auto it = values.find(std::string(key));
            if (it == values.end()) return {};
            if (it->second == "true") return true;
            if (it->second == "false") return false;
            vsg::warn("vsgOcean::loadEnvironmentFile: '", key, ": ", it->second, "' is not true or false");
            return {};
        }

        [[nodiscard]] std::optional<std::string> asText(const KeyValue& values, std::string_view key)
        {
            const auto it = values.find(std::string(key));
            if (it == values.end()) return {};
            return it->second;
        }

        // write "key: value" with a comment header, in the reading order of the loader
        class YamlWriter
        {
        public:
            explicit YamlWriter(std::ofstream& out) : out_(out) { out_ << std::setprecision(10); }

            void comment(std::string_view text)
            {
                out_ << "# " << text << '\n';
            }

            void number(std::string_view key, double value)
            {
                out_ << key << ": " << value << '\n';
            }

            void boolean(std::string_view key, bool value)
            {
                out_ << key << ": " << (value ? "true" : "false") << '\n';
            }

            void text(std::string_view key, std::string_view value)
            {
                out_ << key << ": " << value << '\n';
            }

            [[nodiscard]] bool valid() const { return out_.good(); }

        private:
            std::ofstream& out_;
        };
    }

    bool writeEnvironmentFile(const vsg::Path& path, const Environment& environment, const Wavefield& waves)
    {
        std::ofstream out{path};
        if (!out)
        {
            vsg::warn("vsgOcean::writeEnvironmentFile: cannot write '", path, "'");
            return false;
        }

        const auto& composition = environment.waterComposition();
        const auto& current = environment.waterCurrent();
        const auto fft = waves.fftParameters();
        const auto fftSea = waves.model() == Wavefield::Model::FFT;

        YamlWriter yaml(out);
        yaml.comment("vsgOcean environment, exported from the runtime GUI; reload with the example's --env flag");
        yaml.comment("sky and weather");
        yaml.number("sun_elevation_deg", environment.sunElevation());
        yaml.number("sun_azimuth_deg", environment.sunAzimuth());
        yaml.number("cloud_cover_oktas", environment.cloudiness());
        yaml.number("rain_rate", environment.rainRate());
        yaml.number("fog_density", environment.fogDensity());
        yaml.number("latitude_deg", environment.latitude());
        yaml.number("longitude_deg", environment.longitude());
        yaml.comment("wind");
        yaml.number("wind_speed_m_s", environment.windSpeed());
        yaml.number("wind_direction_deg", environment.windDirection());
        yaml.comment("water colour and optics: water_type is a Jerlov name, or Custom when composed directly");
        yaml.text("water_type", environment.jerlovName());
        yaml.number("chlorophyll_mg_m3", composition.chlorophyll);
        yaml.number("cdom_a440_1_m", composition.cdom);
        yaml.number("sediment_g_m3", composition.sediment);
        yaml.number("sea_level_m", environment.seaLevel());
        yaml.number("marine_snow_density", environment.marineSnowDensity());
        yaml.boolean("underwater_dof_blur", environment.underwaterBlur());
        yaml.number("water_current_east_m_s", current.x);
        yaml.number("water_current_north_m_s", current.y);
        yaml.comment("waves: wave_model is gerstner or fft");
        yaml.text("wave_model", fftSea ? "fft" : "gerstner");
        yaml.number("wave_amplitude_m", waves.amplitude());
        yaml.number("wave_period_s", waves.period());
        yaml.number("wave_steepness", waves.steepness());
        yaml.number("wave_components", static_cast<double>(waves.number()));
        yaml.number("wave_scale", waves.scale());
        yaml.number("wave_angle_deg", vsg::degrees(waves.angle()));
        yaml.number("fft_tile_size_m", fft.tileSize);
        yaml.number("fft_choppiness", fft.choppiness);
        yaml.number("fft_wave_age", fft.capOmegaC);
        yaml.number("fft_directional_spread", fft.spread);
        yaml.boolean("fft_detail_cascade", waves.fftDetail());
        yaml.number("fft_foam", waves.foam());

        if (!yaml.valid())
        {
            vsg::warn("vsgOcean::writeEnvironmentFile: failed writing '", path, "'");
            return false;
        }
        vsg::info("vsgOcean: wrote the environment to '", path, "'");
        return true;
    }

    bool loadEnvironmentFile(const vsg::Path& path, Environment& environment, Wavefield& waves)
    {
        KeyValue values;
        if (!parseFlatYaml(path, values)) return false;

        // sky, weather and geolocation
        if (const auto value = asNumber(values, "sun_elevation_deg")) environment.setSunElevation(*value);
        if (const auto value = asNumber(values, "sun_azimuth_deg")) environment.setSunAzimuth(*value);
        if (const auto value = asNumber(values, "cloud_cover_oktas")) environment.setCloudiness(*value);
        if (const auto value = asNumber(values, "rain_rate")) environment.setRainRate(*value);
        if (const auto value = asNumber(values, "fog_density")) environment.setFogDensity(*value);
        if (const auto value = asNumber(values, "latitude_deg")) environment.setLatitude(*value);
        if (const auto value = asNumber(values, "longitude_deg")) environment.setLongitude(*value);

        // wind
        if (const auto value = asNumber(values, "wind_speed_m_s")) environment.setWindSpeed(*value);
        if (const auto value = asNumber(values, "wind_direction_deg")) environment.setWindDirection(*value);

        // water colour: a Jerlov preset, then the composition keys as overrides on
        // top of it (a tweaked preset; identical values snap back to the preset)
        if (const auto name = asText(values, "water_type"))
        {
            if (const auto type = Environment::jerlovTypeFromName(*name)) environment.setJerlovType(*type);
        }
        auto composition = environment.waterComposition();
        auto composed = false;
        if (const auto value = asNumber(values, "chlorophyll_mg_m3"))
        {
            composition.chlorophyll = *value;
            composed = true;
        }
        if (const auto value = asNumber(values, "cdom_a440_1_m"))
        {
            composition.cdom = *value;
            composed = true;
        }
        if (const auto value = asNumber(values, "sediment_g_m3"))
        {
            composition.sediment = *value;
            composed = true;
        }
        if (composed) environment.setWaterComposition(composition);

        // the rest of the underwater environment
        if (const auto value = asNumber(values, "sea_level_m")) environment.setSeaLevel(*value);
        if (const auto value = asNumber(values, "marine_snow_density")) environment.setMarineSnowDensity(*value);
        if (const auto value = asBool(values, "underwater_dof_blur")) environment.setUnderwaterBlur(*value);
        auto current = environment.waterCurrent();
        if (const auto value = asNumber(values, "water_current_east_m_s"))
        {
            current.x = static_cast<float>(*value);
            environment.setWaterCurrent(current);
        }
        if (const auto value = asNumber(values, "water_current_north_m_s"))
        {
            current.y = static_cast<float>(*value);
            environment.setWaterCurrent(current);
        }

        // waves: the model first (it selects which parameter set the surface uses),
        // then both parameter sets - absent keys leave the current sea untouched
        if (const auto model = asText(values, "wave_model"))
        {
            waves.setModel(*model == "fft" ? Wavefield::Model::FFT : Wavefield::Model::CWR);
        }
        if (const auto value = asNumber(values, "wave_amplitude_m")) waves.setAmplitude(*value);
        if (const auto value = asNumber(values, "wave_period_s")) waves.setPeriod(*value);
        if (const auto value = asNumber(values, "wave_steepness")) waves.setSteepness(*value);
        if (const auto value = asNumber(values, "wave_components")) waves.setNumber(static_cast<size_t>(std::max(1.0, *value)));
        if (const auto value = asNumber(values, "wave_scale")) waves.setScale(*value);
        if (const auto value = asNumber(values, "wave_angle_deg")) waves.setAngle(vsg::radians(*value));

        auto fft = waves.fftParameters();
        auto changed = false;
        if (const auto value = asNumber(values, "fft_tile_size_m"))
        {
            fft.tileSize = *value;
            changed = true;
        }
        if (const auto value = asNumber(values, "fft_choppiness"))
        {
            fft.choppiness = *value;
            changed = true;
        }
        if (const auto value = asNumber(values, "fft_wave_age"))
        {
            fft.capOmegaC = *value;
            changed = true;
        }
        if (const auto value = asNumber(values, "fft_directional_spread"))
        {
            fft.spread = *value;
            changed = true;
        }
        if (changed) waves.setFftParameters(fft);
        if (const auto value = asBool(values, "fft_detail_cascade")) waves.setFftDetail(*value);
        if (const auto value = asNumber(values, "fft_foam")) waves.setFoam(*value);

        vsg::info("vsgOcean: loaded the environment from '", path, "'");
        return true;
    }
}
