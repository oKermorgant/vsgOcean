/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/SDFLoader.h>
#include <vsgOcean/UriResolver.h>

#include <sdf/sdf.hh>
#include <sdf/Root.hh>
#include <sdf/World.hh>
#include <sdf/Model.hh>
#include <sdf/Link.hh>
#include <sdf/Visual.hh>
#include <sdf/Mesh.hh>

#include <gz/math/Pose3.hh>

namespace vsgOcean
{
    namespace
    {
        // gz::math pose to a vsg column-major matrix
        vsg::dmat4 toVsgMatrix(const gz::math::Pose3d& pose)
        {
            const auto& pos = pose.Pos();
            const auto& rot = pose.Rot();

            const double x = rot.X(), y = rot.Y(), z = rot.Z(), w = rot.W();

            vsg::dmat4 matrix;
            matrix[0][0] = 1.0 - 2.0 * (y * y + z * z);
            matrix[1][0] = 2.0 * (x * y - z * w);
            matrix[2][0] = 2.0 * (x * z + y * w);
            matrix[0][1] = 2.0 * (x * y + z * w);
            matrix[1][1] = 1.0 - 2.0 * (x * x + z * z);
            matrix[2][1] = 2.0 * (y * z - x * w);
            matrix[0][2] = 2.0 * (x * z - y * w);
            matrix[1][2] = 2.0 * (y * z + x * w);
            matrix[2][2] = 1.0 - 2.0 * (x * x + y * y);
            matrix[3][0] = pos.X();
            matrix[3][1] = pos.Y();
            matrix[3][2] = pos.Z();
            return matrix;
        }

        // pose of an SDF element relative to its parent frame (SDF semantic pose resolution:
        // relative_to and joint frames are taken into account)
        vsg::dmat4 resolvedPose(const sdf::SemanticPose& semanticPose, const std::string& what)
        {
            gz::math::Pose3d pose;
            const auto errors = semanticPose.Resolve(pose);
            if (!errors.empty())
            {
                for (const auto& error : errors)
                {
                    vsg::warn("vsgOcean::SDFLoader - ", what, ": ", error.Message());
                }
                return {};
            }
            return toVsgMatrix(pose);
        }

        // resolve a mesh URI to an absolute filesystem path and load it;
        // vsgXchange then handles resources referenced inside the mesh file
        vsg::ref_ptr<vsg::Node> loadMeshNode(const std::string& uri, const vsg::Path& baseDir,
                                             const UriResolver& resolver, vsg::ref_ptr<vsg::Options> options)
        {
            const auto filename = resolver.resolve(uri, baseDir);
            if (filename.empty()) return {};

            auto object = vsg::read(filename, options);
            if (auto node = object.cast<vsg::Node>()) return node;

            if (object) vsg::warn("Unable to view object of type ", object->className());
            else vsg::warn("Unable to load mesh ", filename);

            return {};
        }

        vsg::ref_ptr<Model> loadModelFromSdf(const sdf::Model& sdfModel, const vsg::Path& baseDir,
                                             const UriResolver& resolver, vsg::ref_ptr<vsg::Options> options)
        {
            auto model = Model::create(sdfModel.Name(), resolvedPose(sdfModel.SemanticPose(), sdfModel.Name()));

            for (size_t l = 0; l < sdfModel.LinkCount(); ++l)
            {
                const sdf::Link* link = sdfModel.LinkByIndex(l);

                // links are named frames whose pose can be changed at runtime
                auto linkFrame = Frame::create(link->Name(), resolvedPose(link->SemanticPose(), link->Name()));

                for (size_t v = 0; v < link->VisualCount(); ++v)
                {
                    const sdf::Visual* visual = link->VisualByIndex(v);

                    if (!visual->Geom() || visual->Geom()->Type() != sdf::GeometryType::MESH) continue;

                    auto mesh = visual->Geom()->MeshShape();
                    if (!mesh || mesh->Uri().empty()) continue;

                    auto node = loadMeshNode(mesh->Uri(), baseDir, resolver, options);
                    if (!node) continue;

                    // the visual pose within its link is fixed; the mesh scale applies
                    // in the visual frame, on top of that pose
                    auto visualMatrix = resolvedPose(visual->SemanticPose(), visual->Name());
                    const auto& scale = mesh->Scale();
                    visualMatrix = visualMatrix * vsg::scale(scale.X(), scale.Y(), scale.Z());

                    auto visualTransform = vsg::MatrixTransform::create(visualMatrix);
                    visualTransform->addChild(node);
                    linkFrame->addChild(visualTransform);
                }

                model->addFrame(linkFrame);
            }

            // nested models become submodels of this model
            for (size_t m = 0; m < sdfModel.ModelCount(); ++m)
            {
                if (auto nested = loadModelFromSdf(*sdfModel.ModelByIndex(m), baseDir, resolver, options))
                {
                    model->addModel(nested);
                }
            }

            return model;
        }
    }

    std::vector<vsg::Path> readMeshPaths(const vsg::Path& sdfPath)
    {
        std::vector<vsg::Path> meshPaths;

        sdf::Root root;
        const auto errors = root.Load(sdfPath.string());
        if (!errors.empty())
        {
            vsg::warn("Failed to read SDF: ", sdfPath);
            for (const auto& error : errors)
            {
                vsg::warn("  Error: ", error.Message());
            }
            return meshPaths;
        }

        for (size_t w = 0; w < root.WorldCount(); ++w)
        {
            const sdf::World* world = root.WorldByIndex(w);

            for (size_t m = 0; m < world->ModelCount(); ++m)
            {
                const sdf::Model* model = world->ModelByIndex(m);

                for (size_t l = 0; l < model->LinkCount(); ++l)
                {
                    const sdf::Link* link = model->LinkByIndex(l);

                    for (size_t v = 0; v < link->VisualCount(); ++v)
                    {
                        const sdf::Visual* visual = link->VisualByIndex(v);

                        if (!visual->Geom() || visual->Geom()->Type() != sdf::GeometryType::MESH) continue;

                        auto mesh = visual->Geom()->MeshShape();
                        if (!mesh || mesh->Uri().empty()) continue;

                        vsg::info("Found mesh: ", mesh->Uri());
                        meshPaths.push_back(vsg::Path(mesh->Uri()));
                    }
                }
            }
        }

        // standalone model SDF files (no world element)
        if (const sdf::Model* model = root.Model())
        {
            for (size_t l = 0; l < model->LinkCount(); ++l)
            {
                const sdf::Link* link = model->LinkByIndex(l);

                for (size_t v = 0; v < link->VisualCount(); ++v)
                {
                    const sdf::Visual* visual = link->VisualByIndex(v);

                    if (!visual->Geom() || visual->Geom()->Type() != sdf::GeometryType::MESH) continue;

                    auto mesh = visual->Geom()->MeshShape();
                    if (!mesh || mesh->Uri().empty()) continue;

                    vsg::info("Found mesh: ", mesh->Uri());
                    meshPaths.push_back(vsg::Path(mesh->Uri()));
                }
            }
        }

        if (meshPaths.empty())
        {
            vsg::warn("No mesh found in SDF: ", sdfPath);
        }

        return meshPaths;
    }

    void loadScene(const vsg::Path& sdfPath, const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Options> options)
    {

        sdf::Root root;
        const auto errors = root.Load(sdfPath.string());
        if (!errors.empty())
        {
            vsg::warn("Failed to read SDF: ", sdfPath);
            for (const auto& error : errors)
            {
                vsg::warn("  Error: ", error.Message());
            }
            return;
        }

        const auto baseDir = vsg::filePath(sdfPath);
        const UriResolver resolver;

        const auto addSdfModel = [&](const sdf::Model& sdfModel)
        {
            if (auto model = loadModelFromSdf(sdfModel, baseDir, resolver, options))
            {
                scene->addModel(model);
            }
        };

        for (size_t w = 0; w < root.WorldCount(); ++w)
        {
            const sdf::World* world = root.WorldByIndex(w);

            // geolocation of the scene, when the world provides spherical coordinates
            if (const auto* spherical = world->SphericalCoordinates())
            {
                scene->environment()->setLatitude(spherical->LatitudeReference().Degree());
                scene->environment()->setLongitude(spherical->LongitudeReference().Degree());
            }

            for (size_t m = 0; m < world->ModelCount(); ++m)
            {
                addSdfModel(*world->ModelByIndex(m));
            }
        }

        // standalone model SDF files (no world element)
        if (const sdf::Model* model = root.Model())
        {
            addSdfModel(*model);
        }

        if (scene->world()->children.empty())
        {
            vsg::warn("No model found in SDF: ", sdfPath);
        }


    }
}
