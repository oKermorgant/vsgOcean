#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>
#include <vsgOcean/core/Environment.h>
#include <vsgOcean/core/Wavefield.h>

namespace vsgOcean
{
    /// The ocean surface: a mesh displaced by the same Gerstner component waves as its
    /// Wavefield (the Gazebo wave model), so that the rendered waves and the waves evaluated
    /// on the CPU for dynamics are the same. The shading uses the Environment (sun, fog,
    /// Jerlov water colour).
    ///
    /// The surface owns its Wavefield; tune it through wavefield() and call update() every
    /// frame with the current Environment, Camera and simulation time.
    class VSG_OCEAN_EXPORT OceanSurface : public vsg::Inherit<vsg::Group, OceanSurface>
    {
    public:
        /// rings, segments: the resolution of the surface mesh. The mesh is a
        /// camera-following disc of concentric rings with an exponentially growing radius,
        /// from sub-metre cells around the camera (resolving the shortest waves) to
        /// horizon-scale cells far away (sub-pixel there) - the ocean is endless, and the
        /// wave field is sampled at world positions, so the waves stay fixed in the world
        /// as the mesh follows the camera.
        ///
        /// fftCells: the resolution of the FFT tile of the Wavefield (see FftWaves), fixed
        /// for the lifetime of the surface.
        explicit OceanSurface(uint32_t rings = 96, uint32_t segments = 128, uint32_t fftCells = 128);

        /// The wave field of the surface (the Gazebo wave model), tunable at any time.
        vsg::ref_ptr<Wavefield> wavefield() const { return wavefield_; }

        /// Refresh the surface from the environment, camera and simulation time t in seconds.
        /// The wave field follows the environment wind direction. Call every frame, before record.
        /// Refresh the surface from the environment and simulation time t in seconds.
        /// The wave field follows the environment wind direction; the camera state comes
        /// through push constants at record time, so one surface renders correctly from
        /// any number of cameras. Call every frame, before record.
        void update(const Environment& environment, double t);

        /// The sun's visibility to the camera (0 occluded by world geometry ..
        /// 1 clear sky): the specular glitter, the glare lobe and the reflected
        /// sun disc require the sun to actually reach the viewer. Computed by
        /// the Scene against the world geometry; defaults to fully visible.
        void setSunVisibility(float visibility) { sunVisibility_ = visibility; }

        /// The rain rate (0..1) driving the surface response: the rain adds
        /// capillary roughness (impact-plussed ripples) and flattens the
        /// specular glitter. Set by the Scene from the environment each frame.
        void setRainRate(float rate) { rainRate_ = rate; }

    protected:
        vsg::ref_ptr<Wavefield> wavefield_;
        float sunVisibility_ = 1.0f;
        float rainRate_ = 0.0f;

        // uniform block layout: [0] sun direction, [1] fog color + density, [2] water color,
        // [3] params (t, tau, number, water depth), [4] attenuation + sea level,
        // [5..44] component wave arrays, [45] FFT sea (enabled, tile size)
        vsg::ref_ptr<vsg::vec4Array> uniform_;
    };
}
