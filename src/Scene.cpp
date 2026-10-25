/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/Scene.h>

#include <algorithm>

#if defined(VSGOCEAN_WITH_SDF)
#    include <vsgOcean/SDFLoader.h>
#endif

namespace vsgOcean
{
    namespace
    {
        bool isSdfFile(const vsg::Path& filename)
        {
            const auto extension = vsg::lowerCaseFileExtension(filename);
            return extension == ".sdf" || extension == ".world";
        }
    }

    Scene::Scene() :
        world_(Model::create("world")),
        environment_(Environment::create()),
        sky_(Sky::create()),
        surface_(OceanSurface::create()),
        underwater_(Underwater::create()),
        ambientLight_(vsg::AmbientLight::create()),
        sunLight_(vsg::DirectionalLight::create())
    {
        world_->setParentName({});

        // the children order defines the render order: the sky first (no depth writes),
        // then the lighting, the models, the transparent ocean surface, and finally the
        // underwater pass blended over everything when the camera is submerged
        addChild(sky_);
        addChild(ambientLight_);
        addChild(sunLight_);
        addChild(world_);
        addChild(surface_);
        addChild(underwater_);
    }

    vsg::ref_ptr<Model> Scene::model(const std::string& modelName) const
    {
        return world_->model(modelName);
    }

    vsg::ref_ptr<Frame> Scene::frame(const std::string& frameName) const
    {
        return world_->frame(frameName);
    }

    void Scene::addModel(const vsg::ref_ptr<Model>& model, const std::string& parentModelName)
    {
        if (!model) return;

        auto parent = (parentModelName.empty() || parentModelName == "world") ? world_ : world_->model(parentModelName);
        if (!parent)
        {
            vsg::warn("vsgOcean::Scene::addModel - unknown parent model ", parentModelName, ", attaching ", model->name, " to the world model");
            parent = world_;
        }

        parent->addModel(model);
    }

    void Scene::setPose(const std::string& frameName, const vsg::dmat4& pose, const std::string& parentFrameName)
    {
        world_->setPose(frameName, pose, parentFrameName);
    }

    void Scene::update(const vsg::Camera* camera, double t)
    {
        // the camera is submerged when it is below the wave field at its position
        double submersion = 0.0;
        if (camera && camera->viewMatrix)
        {
            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                const double waveHeight = surface_->wavefield()->height(lookAt->eye.x, lookAt->eye.y, t);
                submersion = waveHeight - lookAt->eye.z; // positive below the surface
            }
        }
        environment_->setSubmersionDepth(submersion);

        sky_->update(*environment_, camera);

        // scene lighting follows the sun: bright and neutral at noon, dim and warm at
        // dawn and dusk, almost dark at night
        const auto sun = environment_->sunDirection();
        const double day = std::clamp((sun.z - -0.05) / (0.25 - -0.05), 0.0, 1.0);
        const double dayFactor = day * day * (3.0 - 2.0 * day); // smoothstep

        sunLight_->direction = sun;
        sunLight_->intensity = static_cast<float>(0.05 + 0.95 * dayFactor);
        sunLight_->color = vsg::mix(vsg::vec3(1.0f, 0.55f, 0.25f), vsg::vec3(1.0f, 0.97f, 0.90f), static_cast<float>(dayFactor));
        ambientLight_->intensity = static_cast<float>(0.03 + 0.07 * dayFactor);

        // underwater, the water filters the light per channel following the Jerlov
        // attenuation coefficients - red is absorbed first, blue-green last - and dims
        // it with depth; the ambient is kept readable at shallow depth so vehicles
        // stay visible
        const double depth = environment_->submersionDepth();
        if (depth > 0.0)
        {
            const auto attenuation = environment_->underwaterAttenuation();
            const vsg::vec3 transmit(static_cast<float>(std::exp(-attenuation.x * depth)),
                                     static_cast<float>(std::exp(-attenuation.y * depth)),
                                     static_cast<float>(std::exp(-attenuation.z * depth)));

            sunLight_->color = sunLight_->color * transmit;
            sunLight_->intensity *= (transmit.x + transmit.y + transmit.z) / 3.0f;

            const float immersed = static_cast<float>(std::min(depth / 3.0, 1.0));
            ambientLight_->color = vsg::mix(vsg::vec3(1.0f, 1.0f, 1.0f), transmit, immersed);
            ambientLight_->intensity = std::max(ambientLight_->intensity, 0.06f + 0.04f * static_cast<float>(dayFactor));
        }
        else
        {
            ambientLight_->color = vsg::vec3(1.0f, 1.0f, 1.0f);
        }

        surface_->update(*environment_, camera, t);
        underwater_->update(*environment_);
    }

    vsg::ref_ptr<Scene> Scene::load(const std::vector<vsg::Path>& filenames, vsg::ref_ptr<vsg::Options> options)
    {
        auto scene = Scene::create();

        for (const auto& filename : filenames)
        {
#if defined(VSGOCEAN_WITH_SDF)
            if (isSdfFile(filename))
            {
                loadScene(filename, scene, options);
                continue;
            }
#endif
            // any other supported model / scene file is loaded directly through vsg::read()
            if (auto node = vsg::read_cast<vsg::Node>(filename, options))
            {
                scene->world()->addChild(node);
            }
            else
            {
                vsg::warn("Unable to load scene ", filename);
            }
        }

        return scene;
    }

    vsg::ref_ptr<Scene> Scene::load(const vsg::Path& filename, vsg::ref_ptr<vsg::Options> options)
    {
        return load(std::vector<vsg::Path>{filename}, options);
    }
}
