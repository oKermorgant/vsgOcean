#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Export.h>

#include <vsg/all.h>

#include <functional>
#include <string>
#include <vector>

namespace vsgOcean
{
    /// Resolves SDF/URDF style resource URIs to absolute filesystem paths.
    /// Shared by the SDF loader and the URDF loader.
    class VSG_OCEAN_EXPORT UriResolver
    {
    public:
        /// Resolves the relative path of a package:// URI to an absolute path, or an empty path if not found.
        using PackageResolver = std::function<vsg::Path(const std::string& package, const vsg::Path& relativePath)>;

        /// Constructs a resolver with default search paths:
        /// package roots from AMENT_PREFIX_PATH and model roots from GAZEBO_MODEL_PATH.
        UriResolver();

        /// Register an additional package root, searched as <root>/share/<package>/<relativePath>
        /// and <root>/<package>/<relativePath> for package:// URIs.
        void addPackageRoot(const vsg::Path& root);

        /// Register a custom resolver for package:// URIs, checked before the package roots.
        void addPackageResolver(PackageResolver resolver);

        /// Register an additional model root, searched as <root>/<model>/<relativePath> for model:// URIs.
        void addModelRoot(const vsg::Path& root);

        /// Resolve a resource URI to an absolute filesystem path.
        /// Handles package://, model://, file:// and plain relative/absolute paths
        /// (relative paths are resolved against baseDir when they do not exist as given).
        /// URIs with any other scheme (e.g. https://) are returned unchanged so that vsg::read()
        /// can handle them. An unresolvable package:// or model:// URI yields an empty path.
        vsg::Path resolve(const std::string& uri, const vsg::Path& baseDir = {}) const;

    private:
        vsg::Path resolvePackage(const std::string& packageName, const vsg::Path& relativePath) const;
        vsg::Path resolveModel(const std::string& modelName, const vsg::Path& relativePath) const;

        std::vector<PackageResolver> packageResolvers_;
        std::vector<vsg::Path> packageRoots_;
        std::vector<vsg::Path> modelRoots_;
    };
}
