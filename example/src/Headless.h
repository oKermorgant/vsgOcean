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
#include <vsgOcean/semantic/SemanticPass.h>
#include <vsgOcean/semantic/SemanticOverlay.h>

#include <vsg/app/CommandGraph.h>
#include <vsg/app/Camera.h>
#include <vsg/app/Viewer.h>
#include <vsg/io/Logger.h>
#include <vsg/vk/Device.h>
#include <vsg/vk/Instance.h>
#include <vsg/vk/PhysicalDevice.h>


#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

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
        // a PNG chunk with its CRC (zlib)
        inline bool writeChunk(std::FILE* file, const char type[4], const std::vector<uint8_t>& data)
        {
            const uint32_t length = static_cast<uint32_t>(data.size());
            const uint8_t header[8] = {static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(length >> 16),
                                      static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length),
                                      static_cast<uint8_t>(type[0]), static_cast<uint8_t>(type[1]),
                                      static_cast<uint8_t>(type[2]), static_cast<uint8_t>(type[3])};
            if (std::fwrite(header, 1, sizeof(header), file) != sizeof(header)) return false;
            if (!data.empty() && std::fwrite(data.data(), 1, data.size(), file) != data.size()) return false;

            auto crc = static_cast<uint32_t>(crc32(0L, Z_NULL, 0));
            crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
            if (!data.empty()) crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
            const uint8_t footer[4] = {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
                                       static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
            return std::fwrite(footer, 1, sizeof(footer), file) == sizeof(footer);
        }

        // write the semantic image as an RGB PNG: the bytes ARE the model IDs,
        // so the file doubles as a human-viewable segmentation (each model a
        // distinct colour) and as exact data (read the bytes back)
        inline bool writeIdImage(const vsg::Path& filename, const vsgOcean::SemanticPass::Image& image)
        {
            std::vector<uint8_t> rgba(image.ids.size() * 4);
            for (size_t pixel = 0; pixel < image.ids.size(); ++pixel)
            {
                rgba[4 * pixel] = image.ids[pixel] & 0xff;
                rgba[4 * pixel + 1] = (image.ids[pixel] >> 8) & 0xff;
                rgba[4 * pixel + 2] = (image.ids[pixel] >> 16) & 0xff;
                rgba[4 * pixel + 3] = 255;
            }

            std::FILE* file = std::fopen(filename.c_str(), "wb");
            if (!file) return false;
            auto write = [file](const void* data, size_t size) { return std::fwrite(data, 1, size, file) == size; };

            const uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
            if (!write(signature, sizeof(signature))) return false;

            std::vector<uint8_t> ihdrData(13);
            ihdrData[0] = static_cast<uint8_t>(image.width >> 24); // PNG is big-endian
            ihdrData[1] = static_cast<uint8_t>(image.width >> 16);
            ihdrData[2] = static_cast<uint8_t>(image.width >> 8);
            ihdrData[3] = static_cast<uint8_t>(image.width);
            ihdrData[4] = static_cast<uint8_t>(image.height >> 24);
            ihdrData[5] = static_cast<uint8_t>(image.height >> 16);
            ihdrData[6] = static_cast<uint8_t>(image.height >> 8);
            ihdrData[7] = static_cast<uint8_t>(image.height);
            ihdrData[8] = 8;  // bit depth
            ihdrData[9] = 6;  // RGBA
            ihdrData[10] = 0; // compression
            ihdrData[11] = 0; // filter
            ihdrData[12] = 0; // interlace
            if (!writeChunk(file, "IHDR", ihdrData)) return false;

            // one filter byte per row, then the packed RGBA, zlib-compressed
            std::vector<uint8_t> scanlines(image.ids.size() * 4 + image.height);
            for (uint32_t row = 0; row < image.height; ++row)
            {
                scanlines[row * (image.width * 4 + 1)] = 0; // no filter
                std::memcpy(&scanlines[row * (image.width * 4 + 1) + 1], &rgba[row * image.width * 4], image.width * 4);
            }
            std::vector<uint8_t> compressed(scanlines.size());
            uLongf compressedSize = static_cast<uLongf>(compressed.size());
            if (compress2(compressed.data(), &compressedSize, scanlines.data(), static_cast<uLong>(scanlines.size()), Z_BEST_SPEED) != Z_OK)
            {
                std::fclose(file);
                return false;
            }
            compressed.resize(compressedSize);
            if (!writeChunk(file, "IDAT", compressed)) return false;
            if (!writeChunk(file, "IEND", {})) return false;

            std::fclose(file);
            return true;
        }

    namespace
    {
        // true at each Nth frame boundary of --semantic-cycle (N = 0: never)
        inline bool semanticCycleAdvanced(uint32_t frameIndex, int every)
        {
            return every > 0 && frameIndex > 0 && frameIndex % static_cast<uint32_t>(every) == 0;
        }
    }

    inline int run(const vsg::ref_ptr<Scene>& scene, const vsgOceanExample::Options& settings,
                   const std::function<int(double, vsg::Camera*)>& perFrame)
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
        if (auto boat = scene->model("wamv"))
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

        // no window pass: the command graph carries the scene and composite
        // passes, and the semantic pass when the annotation is requested (the
        // --semantic ID image and/or the on-screen semantic view)
        auto commandGraph = vsg::CommandGraph::create(device, queueFamily);
        commandGraph->addChild(pipeline->sceneRenderGraph());
        commandGraph->addChild(pipeline->compositeRenderGraph());
        const bool wantsSemantic = settings.semantic || settings.semanticView || settings.semanticOverlay
            || settings.semanticCycleFrames > 0;
        vsg::ref_ptr<vsgOcean::SemanticPass> semantic;
        vsg::ref_ptr<vsgOcean::SemanticOverlay> semanticOverlay;
        if (wantsSemantic)
        {
            semantic = vsgOcean::SemanticPass::create(scene, device, camera, extent);
            if (semantic->modelNames().empty()) semantic = {}; // nothing to annotate
        }
        if (semantic)
        {
            // BEFORE the composite: the overlay (drawn inside the composite
            // pass) samples this pass's image in the same frame
            commandGraph->children.insert(commandGraph->children.end() - 1, semantic->renderGraph());
            // the on-screen semantic view, drawn over the composited photo into
            // the screenshot image - the same draw the windowed path presents
            semanticOverlay = vsgOcean::SemanticOverlay::create(semantic, extent);
            pipeline->compositeRenderGraph()->addChild(semanticOverlay);
        }

        // the display mode of the run: 1 the annotation view, 2 the 50%
        // overlay; --semantic-cycle advances the mode every N frames (photo,
        // annotation, overlay, photo...), the same steps as the R key
        int semanticMode = settings.semanticOverlay ? 2 : (settings.semanticView ? 1 : 0);

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
        // --max-time gates the headless run too (the wall-clock simulationTime is
        // what develops the sea and the demo poses): with a large -f it renders
        // until the simulated time, not the whole frame budget
        while (frames > 0 && viewer->advanceToNextFrame()
               && viewer->getFrameStamp()->simulationTime < settings.maxTime)
        {
            --frames;
            const auto t = viewer->getFrameStamp()->simulationTime;

            // the same per-frame work as the windowed loop, minus the presentation
            // the per-frame callback may script the semantic display mode
            // (the "semantic" showcase tour): negative leaves the flag mode
            if (perFrame)
            {
                if (const int scripted = perFrame(t, camera.get()); scripted >= 0) semanticMode = scripted;
            }
            scene->update(camera.get(), t);

            // the semantic view (the R key in the windowed app): the overlay
            // culls its triangle while off - no cost. The PASS itself renders
            // whenever its DATA is wanted too (--semantic captures the ID
            // image at the end), not only when the view is on: a mode-0 run
            // with --semantic still needs the IDs
            if (semanticCycleAdvanced(frameIndex, settings.semanticCycleFrames)) semanticMode = (semanticMode + 1) % 3;
            const bool semanticRenders = semanticMode != 0 || settings.semantic;
            if (semantic) semantic->setEnabled(semanticRenders);
            if (semanticOverlay) semanticOverlay->setMode(semanticMode);
            if (semanticRenders && semantic) semantic->update();

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

        // the semantic image and its derived boxes (--semantic)
        if (semantic)
        {
            vsgOcean::SemanticPass::Image image;
            if (!semantic->capture(image))
            {
                vsg::warn("vsgOcean headless - the semantic capture failed");
                return 1;
            }
            auto idsPath = settings.screenshotPath;
            if (idsPath.empty()) idsPath = "/tmp/vsgOcean_semantic.png";
            std::string pathText{idsPath.string()};
            pathText = pathText.substr(0, pathText.rfind('.')) + "_ids.png";
            idsPath = pathText;
            if (!writeIdImage(idsPath, image))
            {
                vsg::warn("vsgOcean headless - cannot write ", idsPath);
                return 1;
            }
            vsg::info("vsgOcean headless - the semantic image is ", idsPath, " (0 = background)");
            for (size_t id = 0; id < semantic->modelNames().size(); ++id)
            {
                vsg::info("   id ", id + 1, ": ", semantic->modelNames()[id]);
            }
            for (const auto& annotation : semantic->boxes(image))
            {
                vsg::info("   ", annotation.model, " (id ", annotation.id, "): box [", annotation.x0, ", ", annotation.y0,
                          "] - [", annotation.x1, ", ", annotation.y1, "], ", annotation.pixels, " visible pixels");
            }
        }
        return 0;
    }
}

#endif
