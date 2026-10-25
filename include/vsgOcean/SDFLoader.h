#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

// SDF support must be enabled at build time: -DVSGOCEAN_WITH_SDF=ON (default when sdformat is found)
#if defined(VSGOCEAN_WITH_SDF)

#    include <vsgOcean/Export.h>
#    include <vsgOcean/Scene.h>

namespace vsgOcean
{
    /// Read the mesh URIs referenced by an SDF file, as written in the SDF
    /// (e.g. "package://pkg/meshes/m.dae", "file:///abs/path/m.obj" or a relative path).
    /// Both world SDF files and standalone model SDF files are supported.
    /// Returns an empty vector if the file could not be parsed or contains no mesh.
    VSG_OCEAN_EXPORT std::vector<vsg::Path> readMeshPaths(const vsg::Path& sdfPath);

    /// Load an SDF file (world or standalone model) into the given Scene.
    /// Every SDF model becomes a vsgOcean::Model attached to the scene's world model
    /// (nested SDF models become submodels). Each link becomes a named, registered Frame -
    /// settable at runtime via Scene::setPose() - holding its visuals under fixed poses
    /// resolved with SDF semantic poses. Geolocation from the world's spherical coordinates
    /// is applied to the scene's environment. Mesh URIs are resolved to absolute filesystem
    /// paths before loading with vsg::read().
    VSG_OCEAN_EXPORT void loadScene(const vsg::Path& sdfPath, const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Options> options);
}

#endif // VSGOCEAN_WITH_SDF
