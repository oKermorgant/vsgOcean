#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>

#include <vsg/all.h>

#include <string>

namespace vsgOcean
{
    /// A named transform node. The matrix is the pose of the frame relative to its parent frame
    /// in the scene graph (the parent model's base frame by default).
    class VSG_OCEAN_EXPORT Frame : public vsg::Inherit<vsg::MatrixTransform, Frame>
    {
    public:
        std::string name;

        explicit Frame(const std::string& in_name, const vsg::dmat4& in_matrix = {}) :
            Inherit(),
            name(in_name)
        {
            matrix = in_matrix;
        }
    };
}
