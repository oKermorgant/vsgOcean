// SPDX-License-Identifier: MIT
// This file is derived from the Wavefield wave model of asv_wave_sim
// (Copyright (C) 2019 Rhys Mainwaring, Apache-2.0, https://github.com/srmainwering/asv_wave_sim),
// with modifications. License: http://www.apache.org/licenses/LICENSE-2.0

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Wavefield.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace vsgOcean
{
    Wavefield::Wavefield(uint32_t fftCells) :
        fft_(FftWaves::create(fftCells)),
        detailFft_(FftWaves::create(fftCells))
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

    void Wavefield::setDirection(double direction)
    {
        direction_ = direction;
    }

    void Wavefield::setModel(Model model)
    {
        model_ = model;
    }

    void Wavefield::setTau(double tau)
    {
        tau_ = std::max(tau, 0.01);
    }

    double Wavefield::deepWaterDispersionToOmega(double wavenumber)
    {
        constexpr double g{9.81};
        return std::sqrt(g * wavenumber);
    }

    double Wavefield::deepWaterDispersionToWavenumber(double omega)
    {
        constexpr double g{9.81};
        return omega * omega / g;
    }

    void Wavefield::recalculateCwr()
    {
        // derived mean values
        const auto omega = 2.0 * vsg::PI / period_;
        const auto wavenumber = deepWaterDispersionToWavenumber(omega);

        amplitudes_.clear();
        wavenumbers_.clear();
        angularFrequencies_.clear();
        steepnesses_.clear();
        directions_.clear();

        for (size_t i = 0; i < number_; ++i)
        {
            const auto n = static_cast<int>(i) - static_cast<int>(number_ / 2);
            const auto scaleFactor = std::pow(scale_, n);
            const auto a = scaleFactor * amplitude_;
            const auto k = wavenumber / scaleFactor;
            const auto omega_i = deepWaterDispersionToOmega(k);
            const auto q = (a > 0.0) ? std::min(1.0, steepness_ / (a * k * number_)) : 0.0;

            amplitudes_.push_back(a);
            angularFrequencies_.push_back(omega_i);
            steepnesses_.push_back(q);
            wavenumbers_.push_back(k);
            directions_.push_back(vsg::dvec2(std::cos(n * angle_ + direction_), std::sin(n * angle_ + direction_)));
        }
    }

    void Wavefield::recalculate()
    {
        recalculateCwr();
    }

    void Wavefield::setWindSpeed(double windSpeed)
    {
        auto parameters = fft_->parameters();
        parameters.windSpeed = windSpeed;
        fft_->setParameters(parameters);
    }

    void Wavefield::setFftParameters(const FftWaves::Parameters& parameters)
    {
        auto p = parameters;
        p.windSpeed = fft_->parameters().windSpeed;
        p.windDirection = direction_;
        fft_->setParameters(p);
    }

    void Wavefield::setFftDetail(bool enabled)
    {
        fftDetail_ = enabled;
        syncCascades();
    }

    void Wavefield::setFoam(double amount)
    {
        foam_ = std::clamp(amount, 0.0, 1.0);
    }

    void Wavefield::syncCascades()
    {
        // the cascades share the spectrum: the main tile keeps the wavenumbers below half its
        // Nyquist limit, the detail tile (an eighth of the size, another seed) the ones above
        constexpr double detailRatio = 8.0;
        auto main = fft_->parameters();
        const auto split = 0.5 * vsg::PI * main.cells / main.tileSize;
        main.kMax = fftDetail_ ? split : 0.0;
        fft_->setParameters(main);

        auto detail = main;
        detail.tileSize = main.tileSize / detailRatio;
        detail.kMin = split;
        detail.kMax = 0.0;
        detail.seed = main.seed + 1;
        detailFft_->setParameters(detail);
    }

    void Wavefield::updateFft(double t)
    {
        if (model_ != Model::FFT) return;
        auto parameters = fft_->parameters();
        parameters.windDirection = direction_;
        fft_->setParameters(parameters);
        syncCascades();
        fft_->update(t);
        if (fftDetail_) detailFft_->update(t);
    }

    double Wavefield::heightScale() const
    {
        if (model_ == Model::FFT)
        {
            const auto detail = fftDetail_ ? detailFft_->maxHeight() : 0.0;
            return (fft_->maxHeight() + detail) * (1.0 + fft_->parameters().choppiness);
        }
        return std::accumulate(amplitudes_.begin(), amplitudes_.end(), 0.0);
    }

    double Wavefield::growFactor(double t) const
    {
        return 1.0 - std::exp(-t / tau_);
    }

    double Wavefield::height(double x, double y, double t) const
    {
        if (model_ == Model::FFT)
        {
            // the surface is displaced horizontally (choppiness): find the undisplaced point that
            // lands on (x, y) by fixed-point iteration, as the vertex shader moves it
            const auto grow = growFactor(t);
            const auto sample = [&](const vsg::dvec2& p) {
                auto d = fft_->displacement(p.x, p.y);
                if (fftDetail_) d += detailFft_->displacement(p.x, p.y);
                return d * grow;
            };
            vsg::dvec2 p(x, y);
            for (int i = 0; i < 4; ++i)
            {
                const auto d = sample(p);
                p = vsg::dvec2(x - d.x, y - d.y);
            }
            return sample(p).z;
        }

        // sum of the component waves, as in Wavefield::ComputeDepthSimply of boating_gz
        auto h = 0.0;
        for (size_t i = 0; i < amplitudes_.size(); ++i)
        {
            const auto dot = x * directions_[i].x + y * directions_[i].y;
            const auto theta = wavenumbers_[i] * dot - angularFrequencies_[i] * t;
            h += amplitudes_[i] * std::cos(theta);
        }

        // exponentially grow the waves
        return h * growFactor(t);
    }
}
