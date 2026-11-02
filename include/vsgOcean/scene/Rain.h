#pragma once

/* <editor-fold desc="MIT License">

Copyright (c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to copy, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/core/Export.h>
#include <vsgOcean/core/Environment.h>

#include <vsg/commands/Draw.h>

namespace vsgOcean
{
    /// Rain: the weather layer above the surface - falling streaks bent by the
    /// wind, and expanding splash rings where the drops hit the water, both in
    /// camera-following boxes so a fixed pool of particles covers the view.
    /// The streaks fall at the terminal velocity, advected by the wind; the
    /// number drawn follows the environment's rain rate (0 disables both), and
    /// the veiling tone follows the sky (dimmed by the cloud cover, black at
    /// night). The camera state comes through the push constants at record
    /// time, so one rain field renders from any number of cameras; it fades
    /// out with the camera's submersion (rain is a surface phenomenon).
    class VSG_OCEAN_EXPORT Rain : public vsg::Inherit<vsg::Group, Rain>
    {
    public:
        /// streaks: the size of the falling-drop pool in the camera-following
        /// box (1.0 rain rate = the unit field of 8192 streaks, the pool is
        /// 2x that); rings: the splash pool at the sea level (1.0 rain rate =
        /// the unit field of 1024 rings, the pool is 4x that). box: the edge
        /// of the horizontal box, metres (20 m: the near field carries the
        /// streak density, the far rain adds little to the veil).
        explicit Rain(uint32_t streaks = 2 * 8192, uint32_t rings = 4 * 1024, float box = 20.0f);

        /// Refresh the rain from the environment (rate, wind, sea level, the
        /// sky brightness for the veiling tone) and the simulation time t in
        /// seconds. Call every frame, before record.
        void update(const Environment& environment, double t);

    protected:
        const float box_;
        const uint32_t streaks_, rings_;

        // the drawn commands, so the visible counts follow the rain rate
        vsg::ref_ptr<vsg::Draw> streakDraw_, ringDraw_;

        // uniform block: [0] (t, rate, box, cloudiness), [1] wind + sea level
        // + brightness, [2] the camera eye (the submersion fade)
        vsg::ref_ptr<vsg::vec4Array> uniform_;
    };
}
