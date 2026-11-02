// vsgOcean headless rendering: run the ocean scene offscreen without a window or
// swapchain, writing a screenshot at the end. This exists because over ssh -X the
// hardware GPU cannot present (the X proxy does not forward DRI3), while the
// offscreen rendering itself runs fine on it - the render group membership makes
// /dev/dri/renderD128 (the Intel device) available, roughly 50x faster than the
// software rasterizer that ssh presentation requires.
//
// The device selection prefers a hardware GPU (any physical device that is not
// VK_PHYSICAL_DEVICE_TYPE_CPU), falling back to whatever the loader offers, so
// `VK_ICD_FILE_NAMES` does not need to be set by hand.
#ifndef VSGOCEAN_EXAMPLE_HEADLESS_H
#define VSGOCEAN_EXAMPLE_HEADLESS_H

#include "ExampleOptions.h"

#include <vsgOcean/scene/Scene.h>
#include <vsgOcean/scene/RenderPipeline.h>

#include <vsg/app/CommandGraph.h>
#include <vsg/app/Camera.h>
#include <vsg/app/Viewer.h>
#include <vsg/io/Logger.h>
#include <vsg/vk/Device.h>
#include <vsg/vk/Instance.h>
#include <vsg/vk/PhysicalDevice.h>


#include <cstdio>
#include <functional>
#include <string>

namespace vsgOcean::Headless
{
    namespace
    {
        // a graphics-capable device that is not the software rasterizer, and its graphics queue family
        std::pair<vsg::ref_ptr<vsg::Device>, int> createDevice(bool debugLayer)
        {
            vsg::Names instanceExtensions;
            vsg::Names requestedLayers;
            if (debugLayer)
            {
                requestedLayers.push_back("VK_LAYER_KHRONOS_validation");
            }
            const auto instance = vsg::Instance::create(instanceExtensions, vsg::validateInstanceLayerNames(requestedLayers));

            auto [physicalDevice, queueFamily] = instance->getPhysicalDeviceAndQueueFamily(VK_QUEUE_GRAPHICS_BIT);
            for (const auto& candidate : instance->getPhysicalDevices())
            {
                if (candidate->getProperties().deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
                {
                    physicalDevice = candidate;
                    break;
                }
            }
            if (!physicalDevice || queueFamily < 0)
            {
                vsg::warn("vsgOcean headless - no graphics device available");
                return {{}, -1};
            }

            vsg::info("vsgOcean headless - device: ", physicalDevice->getProperties().deviceName);

            const vsg::Names validatedNames = vsg::validateInstanceLayerNames(requestedLayers);
            const vsg::QueueSettings queueSettings{{queueFamily, {1}}};
            const vsg::Names deviceExtensions;
            auto device = vsg::Device::create(physicalDevice, queueSettings, validatedNames, deviceExtensions, vsg::DeviceFeatures::create());
            return {device, queueFamily};
        }
    }

    /// Render the scene offscreen for the configured number of frames and write the
    /// screenshot. The fallback camera is built here (the bounds framing, matching the
    /// windowed path); perFrame runs the demo animation (model poses, link poses) for
    /// each frame's simulation time, and receives the camera so a scripted tour can
    /// drive it (the --showcase maritime video). With --video, every frame is written
    /// as a PNG (the pattern's %05d is the frame index) instead of a single end
    /// screenshot. Returns the process exit code.
    inline int run(const vsg::ref_ptr<Scene>& scene, const vsgOceanExample::Options& settings,
                   const std::function<void(double, vsg::Camera*)>& perFrame)
    {
        const auto [device, queueFamily] = createDevice(settings.windowTraits->debugLayer);
        if (!device) return 1;

        // the fallback camera: the scene bounds from radius*3.5 away, a close view of
        // the boat when present (the same framing as the windowed path)
        vsg::ComputeBounds computeBounds;
        scene->world()->accept(computeBounds);
        const vsg::dvec3 centre = (computeBounds.bounds.min + computeBounds.bounds.max) * 0.5;
        const double radius = vsg::length(computeBounds.bounds.max - computeBounds.bounds.min) * 0.6;
        vsg::dvec3 eye = centre + vsg::dvec3(0.0, -radius * 3.5, 0.0);
        if (auto boat = scene->model("aquabot"))
        {
            vsg::ComputeBounds boatBounds;
            boat->accept(boatBounds);
            if (boatBounds.bounds.valid())
            {
                const vsg::dvec3 boatCentre = (boatBounds.bounds.min + boatBounds.bounds.max) * 0.5;
                const double boatRadius = vsg::length(boatBounds.bounds.max - boatBounds.bounds.min) * 0.6;
                eye = boatCentre + vsg::dvec3(0.0, -boatRadius * 3.5, boatRadius * 1.5);
            }
        }
        const auto extent = settings.headlessExtent;
        const auto aspect = static_cast<double>(extent.width) / static_cast<double>(extent.height);
        auto lookAt = vsg::LookAt::create(eye, centre, vsg::dvec3(0.0, 0.0, 1.0));
        if (settings.overrideEye) lookAt->eye = settings.eye;
        if (settings.overrideCenter) lookAt->center = settings.center;
        auto camera = vsg::Camera::create(vsg::Perspective::create(30.0, aspect, 0.05, radius * 10.5),
                                          lookAt,
                                          vsg::ViewportState::create(extent));

        // the offscreen render target: the same formats the windowed path uses
        const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
        const VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
        auto pipeline = RenderPipeline::create(scene, device, camera, extent, colorFormat, depthFormat);
        scene->setUnderwater(pipeline->composite());

        // no window pass: the command graph carries the scene and composite passes only
        auto commandGraph = vsg::CommandGraph::create(device, queueFamily);
        commandGraph->addChild(pipeline->sceneRenderGraph());
        commandGraph->addChild(pipeline->compositeRenderGraph());

        auto viewer = vsg::Viewer::create();
        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        if (!viewer->compile())
        {
            vsg::warn("vsgOcean headless - compilation failed");
            return 1;
        }

        viewer->start_point() = vsg::clock::now();
        auto frames = settings.numFrames < 0 ? 20 : settings.numFrames;
        uint32_t frameIndex = 0;
        while (frames > 0 && viewer->advanceToNextFrame())
        {
            --frames;
            const auto t = viewer->getFrameStamp()->simulationTime;

            // the same per-frame work as the windowed loop, minus the presentation
            if (perFrame) perFrame(t, camera.get());
            scene->update(camera.get(), t);

            viewer->recordAndSubmit();

            // video mode: every frame goes to its own PNG (a printf pattern, e.g.
            // `--video frames/f%05d.png`)
            if (!settings.videoPattern.empty())
            {
                char path[512];
                std::snprintf(path, sizeof(path), "%s", settings.videoPattern.string().c_str());
                std::string filename{path};
                if (const auto percent = filename.find('%'); percent != std::string::npos)
                {
                    std::snprintf(path, sizeof(path), filename.c_str(), frameIndex);
                    filename = path;
                }
                if (!pipeline->writeScreenshot(filename)) return 1;
            }
            ++frameIndex;
        }

        if (!settings.screenshotPath.empty())
        {
            if (!pipeline->writeScreenshot(settings.screenshotPath)) return 1;
        }
        return 0;
    }
}

#endif
