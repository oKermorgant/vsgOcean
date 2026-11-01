// SPDX-License-Identifier: MIT
// The spectrum, spreading function and Hermitian amplitude synthesis follow the
// LinearRandomFFTWaveSimulation of asv_wave_sim (Copyright (C) 2019 Rhys Mainwaring,
// Apache-2.0, https://github.com/srmainwaring/asv_wave_sim), with modifications.
// License: http://www.apache.org/licenses/LICENSE-2.0

#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>

#include <vsg/all.h>

#include <complex>
#include <cstdint>
#include <vector>

namespace vsgOcean
{
    /// A periodic, statistically stationary sea surface synthesised by inverse FFT from the
    /// ECKV (Elfouhaily-Chapron-Katsaros-Vandemark) wave spectrum with a cos^2s directional
    /// spreading, as the linear random FFT wave model of the Gazebo asv_wave_sim plugin:
    /// the same spectrum, random draws (std::default_random_engine, default seed) and
    /// choppiness displacement, so the field of a tile matches the Gazebo one
    /// (texel (r, c) lies at x = r * dx, y = c * dy; dx = tileSize / cells).
    ///
    /// The surface is a sum of linear waves with deep water dispersion (omega^2 = g k).
    /// Each update(t) synthesises two maps of cells x cells texels, ready for the GPU:
    /// displacement (dx, dy, height) and slopes (dh/dx, dh/dy of the displaced surface,
    /// Jacobian of the horizontal displacement - below 0 where the surface folds).
    class VSG_OCEAN_EXPORT FftWaves : public vsg::Inherit<vsg::Object, FftWaves>
    {
    public:
        struct Parameters
        {
            uint32_t cells = 128;   ///< texels per tile side, a power of two (asv_wave_sim: 128..256); fixed at construction
            double tileSize = 256.0; ///< tile side [m] (asv_wave_sim: 256..1024)
            double windSpeed = 5.0;  ///< wind speed at 10 m [m/s]
            double windDirection = 0.0; ///< direction the waves travel [rad from +X]
            double capOmegaC = 0.84; ///< inverse wave age (0.84 fully developed .. 5 young sea)
            double spread = 5.0;     ///< cos^2s spreading exponent s
            double choppiness = 0.6; ///< horizontal displacement scale (lambda); 0 gives plain sines
            double gravity = 9.81;

            bool operator==(const Parameters& o) const
            {
                return cells == o.cells && tileSize == o.tileSize && windSpeed == o.windSpeed &&
                       windDirection == o.windDirection && capOmegaC == o.capOmegaC && spread == o.spread &&
                       choppiness == o.choppiness && gravity == o.gravity;
            }
        };

        /// cells: texels per tile side, rounded up to a power of two in [16, 512]; it is
        /// fixed for the lifetime of the object (the GPU textures have that size).
        explicit FftWaves(uint32_t cells = 128);

        /// Change the parameters (but the cell count); the spectrum is rebuilt on the next update only if they changed.
        void setParameters(const Parameters& parameters);
        const Parameters& parameters() const { return parameters_; }

        /// Synthesise the surface at time t [s].
        void update(double t);

        /// (dx, dy, height, 0) per texel, row r = x index, column c = y index.
        vsg::ref_ptr<vsg::vec4Array2D> displacement() const { return displacement_; }
        /// (dh/dx, dh/dy, jacobian, 0) per texel.
        vsg::ref_ptr<vsg::vec4Array2D> slopes() const { return slopes_; }

        /// The largest absolute height of the tile at the last update() [m].
        double maxHeight() const { return maxHeight_; }

        /// Bilinear height of the periodic tile at the horizontal position (x, y) [m].
        /// Valid for the last update(); the horizontal displacement is ignored.
        double height(double x, double y) const;

    protected:
        using Complex = std::complex<double>;

        void rebuildSpectrum();
        static void inverseFft2d(std::vector<Complex>& data, size_t n);

        Parameters parameters_;
        bool dirty_ = true;
        double maxHeight_ = 0.0;

        std::vector<double> psiRoot_; ///< sqrt of the two-sided discrete variance spectrum, fft order
        std::vector<double> rho_;
        std::vector<double> sigma_;
        std::vector<double> kx_;
        std::vector<double> ky_;
        std::vector<double> omega_;

        std::vector<Complex> zhat_;
        std::vector<Complex> work_[4];
        vsg::ref_ptr<vsg::vec4Array2D> displacement_;
        vsg::ref_ptr<vsg::vec4Array2D> slopes_;
    };
}
