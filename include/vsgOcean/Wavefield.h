// SPDX-License-Identifier: MIT
// This file is derived from the Wavefield wave model of asv_wave_sim
// (Copyright (C) 2019 Rhys Mainwaring, Apache-2.0, https://github.com/srmainwering/asv_wave_sim),
// with modifications. License: http://www.apache.org/licenses/LICENSE-2.0

#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>

#include <vsg/all.h>

#include <cstdint>
#include <vector>

namespace vsgOcean
{
    /// A linear combination of Gerstner component waves: a faithful port of the wave model
    /// used in Gazebo by the boating_gz / asv_wave_sim plugins, so that the waves rendered
    /// by vsgOcean and the waves driving a dynamics simulation are the same and can be
    /// synchronised.
    ///
    /// The field is the sum of a small number of component waves, derived either by
    /// sampling the Pierson-Moskowitz spectrum around the mean wave period (PMS), or by
    /// scaling the mean wave by a component wave ratio (CWR). Component wavenumbers follow
    /// the deep water dispersion relation omega^2 = g k. All parameters use SI units;
    /// angles and the direction are in radians, as in Gazebo.
    class VSG_OCEAN_EXPORT Wavefield : public vsg::Inherit<vsg::Object, Wavefield>
    {
    public:
        /// Component wave model
        enum class Model : uint8_t
        {
            PMS, ///< Pierson-Moskowitz spectrum sampling
            CWR  ///< component wave ratio
        };

        /// Maximum number of component waves supported by the ocean surface shader
        static constexpr size_t maxWaves = 8;

        Wavefield();

        // --- parameters (Gazebo semantics, SI units, angles in radians) ---

        /// Number of component waves. Default 3.
        void setNumber(size_t number);
        size_t number() const { return number_; }

        /// Scale between the mean and the largest / smallest component waves. Default 1.1.
        void setScale(double scale);
        double scale() const { return scale_; }

        /// Angle between the mean wave direction and the largest / smallest components. Default 0.4.
        void setAngle(double angle);
        double angle() const { return angle_; }

        /// Steepness parameter in [0, 1] controlling the Gerstner chop (0: rolling sine waves). Default 0.8.
        void setSteepness(double steepness);
        double steepness() const { return steepness_; }

        /// Amplitude of the mean wave in metres, used by the CWR model. Default 0.4.
        void setAmplitude(double amplitude);
        double amplitude() const { return amplitude_; }

        /// Period of the mean wave in seconds. Default 5.
        void setPeriod(double period);
        double period() const { return period_; }

        /// Phase of the mean wave. Default 0.
        void setPhase(double phase);
        double phase() const { return phase_; }

        /// Mean wave direction in radians, measured from the +X axis (+X East, +Y North). Default 0.
        void setDirection(double direction);
        double direction() const { return direction_; }

        /// Component wave model. Default CWR.
        void setModel(Model model);
        Model model() const { return model_; }

        /// Multiplier applied to the component amplitudes (PMS model). Default 1.
        void setGain(double gain);
        double gain() const { return gain_; }

        /// Time constant of the exponential wave grow-in at startup, in seconds. Default 2.
        void setTau(double tau);
        double tau() const { return tau_; }

        /// Recalculate the component waves after parameter changes.
        void recalculate();

        // --- component waves (valid after recalculate) ---

        const std::vector<double>& amplitudes() const { return amplitudes_; }
        const std::vector<double>& wavenumbers() const { return wavenumbers_; }
        const std::vector<double>& angularFrequencies() const { return angularFrequencies_; }
        const std::vector<double>& steepnesses() const { return steepnesses_; }
        const std::vector<vsg::dvec2>& directions() const { return directions_; }

        /// Mean water level at (x, y) at time t in seconds: the sum of the component waves,
        /// including the exponential grow-in from t = 0.
        double height(double x, double y, double t) const;

        /// Exponential grow-in factor of the wave field at time t.
        double growFactor(double t) const;

    protected:
        /// Pierson-Moskowitz spectrum value at angular frequency omega with peak omegaPeak.
        double pm(double omega, double omegaPeak) const;

        static double deepWaterDispersionToOmega(double wavenumber);
        static double deepWaterDispersionToWavenumber(double omega);

        void recalculatePms();
        void recalculateCwr();

        size_t number_;
        double scale_;
        double angle_;
        double steepness_;
        double amplitude_;
        double period_;
        double phase_;
        double direction_;
        Model model_;
        double gain_;
        double tau_;

        std::vector<double> amplitudes_;
        std::vector<double> wavenumbers_;
        std::vector<double> angularFrequencies_;
        std::vector<double> phases_;
        std::vector<double> steepnesses_;
        std::vector<vsg::dvec2> directions_;
    };
}
