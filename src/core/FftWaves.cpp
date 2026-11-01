// SPDX-License-Identifier: MIT
// The spectrum, spreading function and Hermitian amplitude synthesis follow the
// LinearRandomFFTWaveSimulation of asv_wave_sim (Copyright (C) 2019 Rhys Mainwaring,
// Apache-2.0, https://github.com/srmainwaring/asv_wave_sim), with modifications.
// License: http://www.apache.org/licenses/LICENSE-2.0

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/FftWaves.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace vsgOcean
{
    namespace
    {
        using Complex = std::complex<double>;

        // ECKV omnidirectional spectrum S(k) (Elfouhaily et al. 1997), as asv_wave_sim
        double eckvSpectrum(double k, double u10, double capOmegaC, double g)
        {
            if (std::abs(k) < 1.0e-8 || std::abs(u10) < 1.0e-8) return 0.0;

            constexpr double cd10n = 0.00144;
            constexpr double km = 370.0;
            constexpr double cm = 0.23;

            const double uStar = std::sqrt(cd10n) * u10;
            const double gamma = capOmegaC < 1.0 ? 1.7 : 1.7 + 6.0 * std::log10(capOmegaC);
            const double sigma = 0.08 * (1.0 + 4.0 * std::pow(capOmegaC, -3.0));
            const double alphaP = 0.006 * std::pow(capOmegaC, 0.55);
            const double alphaM = 0.01 * (1.0 + (uStar <= cm ? 1.0 : 3.0) * std::log(uStar / cm));

            const double ko = g / u10 / u10;
            const double kp = ko * capOmegaC * capOmegaC;
            const double cp = std::sqrt(g / kp);
            const double c = std::sqrt((g / k) * (1.0 + (k / km) * (k / km)));

            const double lPm = std::exp(-1.25 * (kp / k) * (kp / k));
            const double r = std::sqrt(k / kp) - 1.0;
            const double jp = std::pow(gamma, std::exp(-r * r / (2.0 * sigma * sigma)));
            const double fp = lPm * jp * std::exp(-0.3162 * capOmegaC * r);
            const double fm = lPm * jp * std::exp(-0.25 * (k / km - 1.0) * (k / km - 1.0));

            const double bl = 0.5 * alphaP * (cp / c) * fp;
            const double bh = 0.5 * alphaM * (cm / c) * fm;
            return (bl + bh) / (k * k * k);
        }

        uint32_t nextPowerOfTwo(uint32_t v)
        {
            uint32_t n = 1;
            while (n < v) n <<= 1;
            return n;
        }

        // in-place radix-2 transform with the e^{+i} kernel (FFTW_BACKWARD), unnormalised
        void fft1d(Complex* a, size_t n, size_t stride)
        {
            for (size_t i = 1, j = 0; i < n; ++i)
            {
                size_t bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap(a[i * stride], a[j * stride]);
            }
            for (size_t len = 2; len <= n; len <<= 1)
            {
                const double angle = 2.0 * vsg::PI / static_cast<double>(len);
                const Complex wlen(std::cos(angle), std::sin(angle));
                for (size_t i = 0; i < n; i += len)
                {
                    Complex w(1.0, 0.0);
                    for (size_t j = 0; j < len / 2; ++j)
                    {
                        const auto u = a[(i + j) * stride];
                        const auto v = a[(i + j + len / 2) * stride] * w;
                        a[(i + j) * stride] = u + v;
                        a[(i + j + len / 2) * stride] = u - v;
                        w *= wlen;
                    }
                }
            }
        }
    }

    FftWaves::FftWaves(uint32_t cells)
    {
        Parameters p;
        p.cells = cells;
        setParameters(p);
    }

    void FftWaves::setParameters(const Parameters& parameters)
    {
        auto p = parameters;
        p.cells = displacement_ ? parameters_.cells : nextPowerOfTwo(std::clamp(p.cells, 16u, 512u));
        p.tileSize = std::max(p.tileSize, 1.0);
        p.windSpeed = std::max(p.windSpeed, 0.0);
        p.spread = std::max(p.spread, 0.0);
        p.choppiness = std::max(p.choppiness, 0.0);

        if (p == parameters_ && displacement_) return;
        const bool resized = !displacement_;
        parameters_ = p;
        dirty_ = true;

        if (resized)
        {
            displacement_ = vsg::vec4Array2D::create(p.cells, p.cells, vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            slopes_ = vsg::vec4Array2D::create(p.cells, p.cells, vsg::vec4(0.0f, 0.0f, 1.0f, 0.0f));
            for (auto& data : {displacement_, slopes_})
            {
                data->properties.dataVariance = vsg::DYNAMIC_DATA;
                data->properties.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            for (auto& w : work_) w.assign(size_t(p.cells) * p.cells, Complex{});
            zhat_.assign(size_t(p.cells) * p.cells, Complex{});
        }
    }

    void FftWaves::rebuildSpectrum()
    {
        const size_t n = parameters_.cells;
        const size_t n2 = n * n;
        const double g = parameters_.gravity;
        const double kf = 2.0 * vsg::PI / parameters_.tileSize;

        // normalisation of the cos^2s spreading: Gamma(s+1) / Gamma(s+1/2) / (2 sqrt(pi))
        const double s = parameters_.spread;
        const double cs = std::tgamma(s + 1.0) / std::tgamma(s + 0.5) / (2.0 * std::sqrt(vsg::PI));

        psiRoot_.assign(n2, 0.0);
        rho_.assign(n2, 0.0);
        sigma_.assign(n2, 0.0);
        omega_.assign(n2, 0.0);
        kx_.resize(n);
        ky_.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            kx_[i] = (i < n / 2 ? static_cast<double>(i) : static_cast<double>(i) - static_cast<double>(n)) * kf;
            ky_[i] = kx_[i];
        }

        // the draw order of asv_wave_sim: ikx outer, iky inner, rho then sigma
        std::default_random_engine generator(std::default_random_engine::default_seed);
        std::normal_distribution<double> distribution(0.0, 1.0);

        for (size_t ikx = 0; ikx < n; ++ikx)
        {
            for (size_t iky = 0; iky < n; ++iky)
            {
                const size_t idx = ikx * n + iky;
                const double k = std::hypot(kx_[ikx], ky_[iky]);
                const double phi = std::atan2(ky_[iky], kx_[ikx]) - parameters_.windDirection;

                if (k > 1.0e-8)
                {
                    const double spreading = cs * std::pow(std::abs(std::cos(phi / 2.0)), 2.0 * s);
                    const double psi = eckvSpectrum(k, parameters_.windSpeed, parameters_.capOmegaC, g) * spreading / k;
                    psiRoot_[idx] = 0.5 * std::sqrt(psi * kf * kf);
                    omega_[idx] = std::sqrt(g * k);
                }
                rho_[idx] = distribution(generator);
                sigma_[idx] = distribution(generator);
            }
        }
        dirty_ = false;
    }

    void FftWaves::inverseFft2d(std::vector<Complex>& data, size_t n)
    {
        for (size_t r = 0; r < n; ++r) fft1d(&data[r * n], n, 1);
        for (size_t c = 0; c < n; ++c) fft1d(&data[c], n, n);
    }

    void FftWaves::update(double t)
    {
        if (dirty_) rebuildSpectrum();

        const size_t n = parameters_.cells;
        const double lambda = parameters_.choppiness;
        const auto at = [n](size_t ikx, size_t iky) { return ikx * n + iky; };

        // amplitude of the mode (ikx, iky) at time t from the draws of the mode and of its
        // conjugate (cx, cy) - the two-sided Hermitian construction of asv_wave_sim
        const auto amplitude = [&](size_t ikx, size_t iky, size_t cx, size_t cy) {
            const size_t i = at(ikx, iky);
            const size_t c = at(cx, cy);
            const double wt = omega_[i] * t;
            const double co = std::cos(wt);
            const double si = std::sin(wt);
            const double ri = rho_[i] * psiRoot_[i];
            const double rc = rho_[c] * psiRoot_[c];
            const double sI = sigma_[i] * psiRoot_[i];
            const double sc = sigma_[c] * psiRoot_[c];
            return Complex((ri + rc) * co + (sI + sc) * si, -(ri - rc) * si + (sI - sc) * co);
        };

        std::fill(zhat_.begin(), zhat_.end(), Complex{});
        for (size_t ikx = 1; ikx < n; ++ikx)
        {
            for (size_t iky = 1; iky <= n / 2; ++iky) zhat_[at(ikx, iky)] = amplitude(ikx, iky, n - ikx, n - iky);
        }
        for (size_t iky = 1; iky <= n / 2; ++iky) zhat_[at(0, iky)] = amplitude(0, iky, 0, n - iky);
        for (size_t ikx = 1; ikx <= n / 2; ++ikx) zhat_[at(ikx, 0)] = amplitude(ikx, 0, n - ikx, 0);

        // Hermitian completion of the half spectrum
        for (size_t ikx = 0; ikx < n; ++ikx)
        {
            for (size_t iky = n / 2 + 1; iky < n; ++iky) zhat_[at(ikx, iky)] = std::conj(zhat_[at((n - ikx) % n, n - iky)]);
        }
        for (size_t ikx = n / 2 + 1; ikx < n; ++ikx) zhat_[at(ikx, 0)] = std::conj(zhat_[at(n - ikx, 0)]);

        // eight real fields in four complex transforms: (h, Dx) (Dy, h_x) (h_y, Dxx) (Dyy, Dxy)
        // with D the choppy displacement; derivatives of the Nyquist rows are zero
        const Complex i1(0.0, 1.0);
        for (size_t ikx = 0; ikx < n; ++ikx)
        {
            for (size_t iky = 0; iky < n; ++iky)
            {
                const size_t idx = at(ikx, iky);
                const double kx = kx_[ikx];
                const double ky = ky_[iky];
                const double k = std::hypot(kx, ky);
                const Complex h = zhat_[idx];
                const Complex hi = h * i1;

                const Complex hx = ikx == n / 2 ? Complex{} : hi * kx;
                const Complex hy = iky == n / 2 ? Complex{} : hi * ky;

                Complex dx, dy, dxx, dyy, dxy;
                if (k > 1.0e-8)
                {
                    dx = ikx == n / 2 ? Complex{} : hi * (lambda * kx / k);
                    dy = iky == n / 2 ? Complex{} : hi * (lambda * ky / k);
                    dxx = h * (-lambda * kx * kx / k);
                    dyy = h * (-lambda * ky * ky / k);
                    dxy = h * (-lambda * kx * ky / k);
                }

                work_[0][idx] = h + i1 * dx;
                work_[1][idx] = dy + i1 * hx;
                work_[2][idx] = hy + i1 * dxx;
                work_[3][idx] = dyy + i1 * dxy;
            }
        }
        for (auto& w : work_) inverseFft2d(w, n);

        auto& disp = *displacement_;
        auto& slope = *slopes_;
        for (size_t idx = 0; idx < n * n; ++idx)
        {
            // the textures are laid out with x along the width: transpose the (r = x, c = y) order
            const size_t texel = (idx % n) * n + idx / n;
            const double h = work_[0][idx].real();
            const double dx = work_[0][idx].imag();
            const double dy = work_[1][idx].real();
            const double hx = work_[1][idx].imag();
            const double hy = work_[2][idx].real();
            const double dxx = work_[2][idx].imag();
            const double dyy = work_[3][idx].real();
            const double dxy = work_[3][idx].imag();

            // tangent (1 + Dxx, Dxy, h_x) x bitangent (Dxy, 1 + Dyy, h_y) of the displaced surface
            const double nx = dxy * hy - hx * (1.0 + dyy);
            const double ny = hx * dxy - (1.0 + dxx) * hy;
            const double nz = (1.0 + dxx) * (1.0 + dyy) - dxy * dxy;
            const double nzSafe = std::max(nz, 0.05);

            disp.data()[texel] = vsg::vec4(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(h), 0.0f);
            slope.data()[texel] = vsg::vec4(static_cast<float>(-nx / nzSafe), static_cast<float>(-ny / nzSafe), static_cast<float>(nz), 0.0f);
        }
        displacement_->dirty();
        slopes_->dirty();
    }

    double FftWaves::height(double x, double y) const
    {
        const auto n = static_cast<double>(parameters_.cells);
        const double d = parameters_.tileSize / n;
        const double u = std::fmod(std::fmod(x / d, n) + n, n);
        const double v = std::fmod(std::fmod(y / d, n) + n, n);
        const auto r0 = static_cast<size_t>(u);
        const auto c0 = static_cast<size_t>(v);
        const size_t cells = parameters_.cells;
        const size_t r1 = (r0 + 1) % cells;
        const size_t c1 = (c0 + 1) % cells;
        const double fu = u - static_cast<double>(r0);
        const double fv = v - static_cast<double>(c0);

        const auto h = [&](size_t r, size_t c) { return static_cast<double>(displacement_->at(r, c).z); };
        return (1 - fu) * ((1 - fv) * h(r0, c0) + fv * h(r0, c1)) + fu * ((1 - fv) * h(r1, c0) + fv * h(r1, c1));
    }
}
