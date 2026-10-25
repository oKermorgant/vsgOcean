// SPDX-License-Identifier: MIT
// This file is derived from the Wavefield wave model of asv_wave_sim
// (Copyright (C) 2019 Rhys Mainwaring, Apache-2.0, https://github.com/srmainwering/asv_wave_sim),
// with modifications. License: http://www.apache.org/licenses/LICENSE-2.0

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Wavefield.h>

#include <algorithm>
#include <cmath>

namespace vsgOcean
{
    Wavefield::Wavefield() :
        number_(3),
        scale_(1.1),
        angle_(0.4),
        steepness_(0.8),
        amplitude_(0.4),
        period_(5.0),
        phase_(0.0),
        direction_(0.0),
        model_(Model::CWR),
        gain_(1.0),
        tau_(2.0)
    {
        recalculate();
    }

    void Wavefield::setNumber(size_t number)
    {
        number_ = std::clamp(number, size_t(1), maxWaves);
    }

    void Wavefield::setScale(double scale)
    {
        scale_ = std::max(scale, 1.0);
    }

    void Wavefield::setAngle(double angle)
    {
        angle_ = angle;
    }

    void Wavefield::setSteepness(double steepness)
    {
        steepness_ = std::clamp(steepness, 0.0, 1.0);
    }

    void Wavefield::setAmplitude(double amplitude)
    {
        amplitude_ = std::max(amplitude, 0.0);
    }

    void Wavefield::setPeriod(double period)
    {
        period_ = std::max(period, 0.1);
    }

    void Wavefield::setPhase(double phase)
    {
        phase_ = phase;
    }

    void Wavefield::setDirection(double direction)
    {
        direction_ = direction;
    }

    void Wavefield::setModel(Model model)
    {
        model_ = model;
    }

    void Wavefield::setGain(double gain)
    {
        gain_ = std::max(gain, 0.0);
    }

    void Wavefield::setTau(double tau)
    {
        tau_ = std::max(tau, 0.01);
    }

    double Wavefield::pm(double omega, double omegaPeak) const
    {
        constexpr double alpha = 0.0081;
        constexpr double g = 9.81;
        return alpha * g * g / std::pow(omega, 5.0) * std::exp(-(5.0 / 4.0) * std::pow(omegaPeak / omega, 4.0));
    }

    double Wavefield::deepWaterDispersionToOmega(double wavenumber)
    {
        constexpr double g = 9.81;
        return std::sqrt(g * wavenumber);
    }

    double Wavefield::deepWaterDispersionToWavenumber(double omega)
    {
        constexpr double g = 9.81;
        return omega * omega / g;
    }

    void Wavefield::recalculatePms()
    {
        // derived mean values
        const double omega = 2.0 * vsg::PI / period_;
        const double wavenumber = deepWaterDispersionToWavenumber(omega);

        // frequency spacing between components, as in asv_wave_sim (defined for 3 components)
        const double omegaSpacing[] = {
            omega * (1.0 - 1.0 / scale_),
            omega * (scale_ - 1.0 / scale_) / 2.0,
            omega * (scale_ - 1.0)};

        amplitudes_.clear();
        wavenumbers_.clear();
        angularFrequencies_.clear();
        phases_.clear();
        steepnesses_.clear();
        directions_.clear();

        for (size_t i = 0; i < number_; ++i)
        {
            const int n = static_cast<int>(i) - 1;
            const double scaleFactor = std::pow(scale_, n);
            const double omega_i = omega * scaleFactor;
            const double a = gain_ * std::sqrt(2.0 * pm(omega_i, omega) * omegaSpacing[std::min(i, size_t(2))]);
            const double k = deepWaterDispersionToWavenumber(omega_i);
            const double q = (a > 0.0) ? std::min(1.0, steepness_ / (a * k * number_)) : 0.0;

            amplitudes_.push_back(a);
            angularFrequencies_.push_back(omega_i);
            phases_.push_back(phase_);
            steepnesses_.push_back(q);
            wavenumbers_.push_back(k);
            directions_.push_back(vsg::dvec2(std::cos(n * angle_ + direction_), std::sin(n * angle_ + direction_)));
        }
    }

    void Wavefield::recalculateCwr()
    {
        // derived mean values
        const double omega = 2.0 * vsg::PI / period_;
        const double wavenumber = deepWaterDispersionToWavenumber(omega);

        amplitudes_.clear();
        wavenumbers_.clear();
        angularFrequencies_.clear();
        phases_.clear();
        steepnesses_.clear();
        directions_.clear();

        for (size_t i = 0; i < number_; ++i)
        {
            const int n = static_cast<int>(i) - static_cast<int>(number_ / 2);
            const double scaleFactor = std::pow(scale_, n);
            const double a = scaleFactor * amplitude_;
            const double k = wavenumber / scaleFactor;
            const double omega_i = deepWaterDispersionToOmega(k);
            const double q = (a > 0.0) ? std::min(1.0, steepness_ / (a * k * number_)) : 0.0;

            amplitudes_.push_back(a);
            angularFrequencies_.push_back(omega_i);
            phases_.push_back(phase_);
            steepnesses_.push_back(q);
            wavenumbers_.push_back(k);
            directions_.push_back(vsg::dvec2(std::cos(n * angle_ + direction_), std::sin(n * angle_ + direction_)));
        }
    }

    void Wavefield::recalculate()
    {
        if (model_ == Model::PMS)
        {
            recalculatePms();
        }
        else
        {
            recalculateCwr();
        }
    }

    double Wavefield::growFactor(double t) const
    {
        return 1.0 - std::exp(-t / tau_);
    }

    double Wavefield::height(double x, double y, double t) const
    {
        // sum of the component waves, as in Wavefield::ComputeDepthSimply of boating_gz
        double h = 0.0;
        for (size_t i = 0; i < amplitudes_.size(); ++i)
        {
            const double dot = x * directions_[i].x + y * directions_[i].y;
            const double theta = wavenumbers_[i] * dot - angularFrequencies_[i] * t;
            h += amplitudes_[i] * std::cos(theta);
        }

        // exponentially grow the waves
        return h * growFactor(t);
    }
}
