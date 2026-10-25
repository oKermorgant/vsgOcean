/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/UriResolver.h>

#include <utility>

namespace vsgOcean
{
    namespace
    {
        const std::string packageScheme = "package://";
        const std::string modelScheme = "model://";
        const std::string fileScheme = "file://";

        // split "<name>/<relative path>" into its two components
        std::pair<std::string, vsg::Path> splitRemainder(const std::string& remainder)
        {
            const auto separator = remainder.find('/');
            if (separator == std::string::npos) return {remainder, {}};
            return {remainder.substr(0, separator), vsg::Path(remainder.substr(separator + 1))};
        }
    }

    UriResolver::UriResolver()
    {
        // ROS install prefixes, giving <prefix>/share/<package>/<relativePath>
        for (const auto& prefix : vsg::getEnvPaths("AMENT_PREFIX_PATH"))
        {
            packageRoots_.push_back(prefix);
        }

        // Gazebo model paths, giving <path>/<model>/<relativePath>
        for (const auto& root : vsg::getEnvPaths("GAZEBO_MODEL_PATH"))
        {
            modelRoots_.push_back(root);
        }
    }

    void UriResolver::addPackageRoot(const vsg::Path& root)
    {
        packageRoots_.push_back(root);
    }

    void UriResolver::addPackageResolver(PackageResolver resolver)
    {
        packageResolvers_.push_back(std::move(resolver));
    }

    void UriResolver::addModelRoot(const vsg::Path& root)
    {
        modelRoots_.push_back(root);
    }

    vsg::Path UriResolver::resolvePackage(const std::string& packageName, const vsg::Path& relativePath) const
    {
        for (const auto& resolver : packageResolvers_)
        {
            if (auto resolved = resolver(packageName, relativePath); !resolved.empty())
            {
                return resolved;
            }
        }

        for (const auto& root : packageRoots_)
        {
            auto candidate = root / vsg::Path("share") / vsg::Path(packageName) / relativePath;
            if (vsg::fileExists(candidate)) return candidate;

            candidate = root / vsg::Path(packageName) / relativePath;
            if (vsg::fileExists(candidate)) return candidate;
        }

        return {};
    }

    vsg::Path UriResolver::resolveModel(const std::string& modelName, const vsg::Path& relativePath) const
    {
        for (const auto& root : modelRoots_)
        {
            auto candidate = root / vsg::Path(modelName) / relativePath;
            if (vsg::fileExists(candidate)) return candidate;
        }

        return {};
    }

    vsg::Path UriResolver::resolve(const std::string& uri, const vsg::Path& baseDir) const
    {
        if (uri.rfind(packageScheme, 0) == 0)
        {
            auto [packageName, relativePath] = splitRemainder(uri.substr(packageScheme.size()));
            auto resolved = resolvePackage(packageName, relativePath);
            if (resolved.empty()) vsg::warn("UriResolver: failed to resolve URI: ", uri);
            return resolved;
        }

        if (uri.rfind(modelScheme, 0) == 0)
        {
            auto [modelName, relativePath] = splitRemainder(uri.substr(modelScheme.size()));
            auto resolved = resolveModel(modelName, relativePath);
            if (resolved.empty()) vsg::warn("UriResolver: failed to resolve URI: ", uri);
            return resolved;
        }

        auto path = (uri.rfind(fileScheme, 0) == 0) ? vsg::Path(uri.substr(fileScheme.size())) : vsg::Path(uri);

        // pass URIs with an unhandled scheme (e.g. https://) through so that vsg::read() can deal with them
        if (uri.find("://") != std::string::npos) return path;

        if (vsg::fileExists(path)) return path;

        auto joined = baseDir / path;
        if (vsg::fileExists(joined)) return joined;

        // keep the most likely candidate so that vsg::read() reports the failure
        return path;
    }
}
