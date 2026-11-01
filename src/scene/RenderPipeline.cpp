/* <editor-fold desc="MIT License">

Copyright(c) 2025 vsgOcean contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/RenderPipeline.h>

#include <fstream>
#include <vector>

#include <zlib.h>

namespace vsgOcean
{
    namespace
    {
        // minimal PNG writer (8-bit RGB, no interlace), so screenshots do not depend on
        // image libraries at runtime; the byte order of the captured image is handled by
        // the caller (the composited image is BGRA on B8G8R8A8 formats)
        void writeChunk(std::ostream& out, const char type[4], const std::vector<uint8_t>& data)
        {
            const auto length = uint32_t(data.size());
            out.put(static_cast<char>(length >> 24));
            out.put(static_cast<char>(length >> 16));
            out.put(static_cast<char>(length >> 8));
            out.put(static_cast<char>(length));
            out.write(type, 4);
            if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), data.size());

            auto crc = crc32(0L, Z_NULL, 0);
            crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
            if (!data.empty()) crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), uInt(data.size()));
            out.put(static_cast<char>(crc >> 24));
            out.put(static_cast<char>(crc >> 16));
            out.put(static_cast<char>(crc >> 8));
            out.put(static_cast<char>(crc));
        }

        bool writePng(const vsg::Path& filename, uint32_t width, uint32_t height, const uint8_t* rgba)
        {
            std::ofstream fout(filename.c_str(), std::ios::binary);
            if (!fout) return false;

            const std::vector<uint8_t> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
            fout.write(reinterpret_cast<const char*>(signature.data()), signature.size());

            // IHDR: width, height, 8-bit RGB
            std::vector<uint8_t> ihdr(13);
            ihdr[0] = static_cast<uint8_t>(width >> 24);
            ihdr[1] = static_cast<uint8_t>(width >> 16);
            ihdr[2] = static_cast<uint8_t>(width >> 8);
            ihdr[3] = static_cast<uint8_t>(width);
            ihdr[4] = static_cast<uint8_t>(height >> 24);
            ihdr[5] = static_cast<uint8_t>(height >> 16);
            ihdr[6] = static_cast<uint8_t>(height >> 8);
            ihdr[7] = static_cast<uint8_t>(height);
            ihdr[8] = 8;  // bit depth
            ihdr[9] = 2;  // colour type: truecolour RGB
            ihdr[10] = 0; // compression: deflate
            ihdr[11] = 0; // filter method
            ihdr[12] = 0; // interlace: none
            writeChunk(fout, "IHDR", ihdr);

            // IDAT: RGB rows, each prefixed with filter byte 0, deflated with zlib
            std::vector<uint8_t> rows(size_t(height) * (1 + 3 * width));
            for (uint32_t y = 0; y < height; ++y)
            {
                auto row = rows.data() + size_t(y) * (1 + 3 * width);
                row[0] = 0; // filter: none
                auto src = rgba + size_t(y) * width * 4;
                for (uint32_t x = 0; x < width; ++x)
                {
                    auto pixel = row + 1 + size_t(x) * 3;
                    pixel[0] = src[0];
                    pixel[1] = src[1];
                    pixel[2] = src[2];
                    src += 4;
                }
            }

            uLongf compressedSize = compressBound(uLongf(rows.size()));
            std::vector<uint8_t> compressed(compressedSize);
            if (compress2(compressed.data(), &compressedSize, rows.data(), uLongf(rows.size()), Z_BEST_SPEED) != Z_OK)
                return false;
            compressed.resize(compressedSize);
            writeChunk(fout, "IDAT", compressed);
            writeChunk(fout, "IEND", {});

            return static_cast<bool>(fout);
        }

        auto graphicsQueueFamily(const vsg::ref_ptr<vsg::Device>& device) -> uint32_t
        {
            const auto families = device->getPhysicalDevice()->getQueueFamilyProperties();
            for (uint32_t i = 0; i < families.size(); ++i)
            {
                if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) return i;
            }
            return 0;
        }
    }

    void RenderPipeline::buildScenePass(const vsg::vec4& clearColor)
    {
        // the scene pass: sky, lighting, models and ocean surface into the offscreen target
        sceneView_ = vsg::View::create(camera_);
        sceneView_->addChild(scene_);
        scene_ = {}; // the view holds the reference from here on

        sceneRenderGraph_ = vsg::RenderGraph::create();
        sceneRenderGraph_->renderPass = target_->renderPass();
        sceneRenderGraph_->framebuffer = target_->framebuffer();
        sceneRenderGraph_->renderArea = VkRect2D{{0, 0}, target_->extent()};
        sceneRenderGraph_->viewportState = vsg::ViewportState::create(0, 0, target_->extent().width, target_->extent().height);
        sceneRenderGraph_->clearValues.resize(2);
        sceneRenderGraph_->clearValues[0].color = clearColor;
        sceneRenderGraph_->clearValues[1].depthStencil = {0.0f, 0}; // reversed depth: 0 is far
        sceneRenderGraph_->windowResizeHandler = {};                 // the pipeline handles its own resize
        sceneRenderGraph_->addChild(sceneView_);
    }

    void RenderPipeline::createOutputImage(VkExtent2D extent)
    {
        // the composited image: the pipeline's output, sampled by the present pass (main
        // pipelines), the GUI (sensor previews) and the screenshot capture
        outputImage_ = vsg::Image::create();
        outputImage_->imageType = VK_IMAGE_TYPE_2D;
        outputImage_->format = colorFormat_;
        outputImage_->extent = VkExtent3D{extent.width, extent.height, 1};
        outputImage_->mipLevels = 1;
        outputImage_->arrayLayers = 1;
        outputImage_->samples = VK_SAMPLE_COUNT_1_BIT;
        outputImage_->tiling = VK_IMAGE_TILING_OPTIMAL;
        outputImage_->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        outputImage_->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        outputImage_->compile(device_.get());
        outputImage_->allocateAndBindMemory(device_.get());
        outputView_ = vsg::ImageView::create(outputImage_, VK_IMAGE_ASPECT_COLOR_BIT);
        outputView_->compile(device_.get());
    }

    void RenderPipeline::buildCompositePass()
    {
        // the composite of this pipeline, sampling the target; wrapped in a view, so it
        // compiles reliably alongside other graph content
        composite_ = Underwater::create(target_->colorView(), target_->depthView());

        // the composite renders into the pipeline's output image (a colour attachment
        // with no depth, ending in a sampled layout)
        auto outputAttachment = vsg::defaultColorAttachment(colorFormat_);
        outputAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        outputAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        const auto attachments = vsg::RenderPass::Attachments{outputAttachment};
        vsg::SubpassDescription subpass{};
        subpass.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        auto renderPass = vsg::RenderPass::create(device_.get(), attachments, vsg::RenderPass::Subpasses{subpass}, vsg::RenderPass::Dependencies{});

        compositeView_ = vsg::View::create(camera_);
        compositeView_->addChild(composite_);

        compositeRenderGraph_ = vsg::RenderGraph::create();
        compositeRenderGraph_->renderPass = renderPass;
        compositeRenderGraph_->framebuffer = vsg::Framebuffer::create(renderPass, vsg::ImageViews{outputView_}, extent_.width, extent_.height, 1);
        compositeRenderGraph_->renderArea = VkRect2D{{0, 0}, extent_};
        compositeRenderGraph_->viewportState = vsg::ViewportState::create(0, 0, extent_.width, extent_.height);
        compositeRenderGraph_->clearValues.resize(1);
        compositeRenderGraph_->clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        compositeRenderGraph_->windowResizeHandler = {};
        compositeRenderGraph_->addChild(compositeView_);
    }

    RenderPipeline::RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Window> window, vsg::ref_ptr<vsg::Camera> camera) :
        scene_(scene),
        device_(window->getOrCreateDevice()),
        camera_(std::move(camera))
    {
        extent_ = window->extent2D();
        colorFormat_ = window->surfaceFormat().format;
        depthFormat_ = window->depthFormat();

        target_ = RenderTarget::create(window);
        buildScenePass(vsg::vec4(0.2f, 0.2f, 0.4f, 1.0f));
        createOutputImage(extent_);
        buildCompositePass();

        presentPass_ = PresentPass::create(outputView_);
        presentPass_->setWindowExtent(window->extent2D());
    }

    RenderPipeline::RenderPipeline(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Device> device, vsg::ref_ptr<vsg::Camera> camera,
                                   VkExtent2D extent, VkFormat colorFormat, VkFormat depthFormat) :
        scene_(scene),
        device_(std::move(device)),
        camera_(std::move(camera)),
        extent_(extent),
        colorFormat_(colorFormat),
        depthFormat_(depthFormat)
    {
        target_ = RenderTarget::create(device_, extent, colorFormat_, depthFormat_);
        buildScenePass(vsg::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        createOutputImage(extent_);
        buildCompositePass();
    }

    bool RenderPipeline::resize(VkExtent2D extent, vsg::Viewer& viewer)
    {
        if (extent.width == 0 || extent.height == 0) return false;
        if (extent.width == extent_.width && extent.height == extent_.height) return false;

        // nothing in flight may still use the attachments released below
        viewer.deviceWaitIdle();

        const auto previous = extent_;
        extent_ = extent;

        target_->resize(extent);
        sceneRenderGraph_->framebuffer = target_->framebuffer();
        sceneRenderGraph_->renderArea = VkRect2D{{0, 0}, extent};
        sceneRenderGraph_->viewportState->set(0, 0, extent.width, extent.height);

        createOutputImage(extent);
        compositeRenderGraph_->framebuffer = vsg::Framebuffer::create(compositeRenderGraph_->renderPass, vsg::ImageViews{outputView_}, extent.width, extent.height, 1);
        compositeRenderGraph_->renderArea = VkRect2D{{0, 0}, extent};
        compositeRenderGraph_->viewportState->set(0, 0, extent.width, extent.height);

        // the camera keeps its field of view over the height: the width follows the aspect
        camera_->projectionMatrix->changeExtent(previous, extent);
        if (camera_->viewportState) camera_->viewportState->set(0, 0, extent.width, extent.height);

        composite_->setSceneTextures(target_->colorView(), target_->depthView());
        if (presentPass_)
        {
            presentPass_->setComposite(outputView_);
            presentPass_->setWindowExtent(extent);
        }

        // rebind the released descriptor sets (the pipelines are untouched)
        if (viewer.compileManager)
        {
            const vsg::ref_ptr<vsg::Node> nodes[] = {composite_, presentPass_};
            for (const auto& node : nodes)
            {
                if (!node) continue;
                const auto result = viewer.compileManager->compile(node);
                if (result.requiresViewerUpdate()) vsg::updateViewer(viewer, result);
            }
        }
        return true;
    }

    void RenderPipeline::setEnabled(bool enabled)
    {
        const auto mask = enabled ? vsg::MASK_ALL : vsg::MASK_OFF;
        if (sceneView_) sceneView_->mask = mask;
        if (compositeView_) compositeView_->mask = mask;
    }

    void RenderPipeline::update(const vsg::ref_ptr<Scene>& scene, const vsg::Camera* camera)
    {
        // this camera's submersion below the surface: each pipeline tracks its own camera.
        // The MEAN sea level is the underwater criterion (stable across waves, no
        // per-frame wave sampling); the exact Wavefield::height stays available for
        // features that need it
        auto submersion = 0.0;
        if (camera && scene && scene->surfaceEnabled())
        {
            if (auto lookAt = camera->viewMatrix.cast<vsg::LookAt>())
            {
                submersion = scene->environment()->seaLevel() - lookAt->eye.z; // positive below the surface
            }
        }

        // the lamps are collected once per frame by the Scene and shared
        composite_->update(*scene->environment(), camera, submersion, &scene->lampLights());
    }

    bool RenderPipeline::capture(Capture& image) const
    {
        if (!outputImage_ || extent_.width == 0 || extent_.height == 0) return false;

        // a tightly packed transfer of 4-byte-per-pixel formats (rows need no padding)
        auto swapRedBlue = false;
        switch (colorFormat_)
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            swapRedBlue = true;
            break;
        default:
            vsg::warn("vsgOcean::RenderPipeline::capture - unsupported output format ", colorFormat_);
            return false;
        }

        const auto width = extent_.width;
        const auto height = extent_.height;
        const auto bufferSize = VkDeviceSize(width) * height * 4;

        const auto family = graphicsQueueFamily(device_);
        const auto queue = device_->getQueue(family);
        const VkDevice vkDevice = *device_;
        const VkQueue vkQueue = *queue;
        const VkImage vkImage = outputImage_->vk(0);

        // a staging buffer in host-visible memory
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        auto bufferInfo = VkBufferCreateInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) return false;

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(vkDevice, stagingBuffer, &requirements);

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(*device_->getPhysicalDevice(), &memoryProperties);
        uint32_t memoryType = 0;
        for (; memoryType < memoryProperties.memoryTypeCount; ++memoryType)
        {
            if ((requirements.memoryTypeBits & (1u << memoryType)) &&
                (memoryProperties.memoryTypes[memoryType].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                break;
        }
        if (memoryType == memoryProperties.memoryTypeCount)
        {
            vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
            return false;
        }

        auto memoryInfo = VkMemoryAllocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memoryInfo.allocationSize = requirements.size;
        memoryInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(vkDevice, &memoryInfo, nullptr, &stagingMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
            return false;
        }
        vkBindBufferMemory(vkDevice, stagingBuffer, stagingMemory, 0);

        // a one-off command buffer: transition the composited image to a transfer
        // source, copy it to the staging buffer, transition it back
        auto poolInfo = VkCommandPoolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = family;
        VkCommandPool pool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        {
            vkFreeMemory(vkDevice, stagingMemory, nullptr);
            vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
            return false;
        }

        auto commandInfo = VkCommandBufferAllocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandInfo.commandPool = pool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(vkDevice, &commandInfo, &command);

        const auto colorRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        auto beginInfo = VkCommandBufferBeginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command, &beginInfo);

        auto toTransferSrc = VkImageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransferSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        toTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransferSrc.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toTransferSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransferSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferSrc.image = vkImage;
        toTransferSrc.subresourceRange = colorRange;
        vkCmdPipelineBarrier(command,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransferSrc);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(command, vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

        auto toShaderRead = VkImageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.image = vkImage;
        toShaderRead.subresourceRange = colorRange;
        vkCmdPipelineBarrier(command,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

        vkEndCommandBuffer(command);

        // submit and wait: queued after the frame submissions on the same queue, so the
        // composite has finished writing the image
        auto submitInfo = VkSubmitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command;

        auto fenceInfo = VkFenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence);
        if (vkQueueSubmit(vkQueue, 1, &submitInfo, fence) != VK_SUCCESS || vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        {
            vkDestroyFence(vkDevice, fence, nullptr);
            vkDestroyCommandPool(vkDevice, pool, nullptr);
            vkFreeMemory(vkDevice, stagingMemory, nullptr);
            vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
            return false;
        }

        // map and copy out as tightly packed RGBA
        void* mapped = nullptr;
        const auto ok = vkMapMemory(vkDevice, stagingMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS;
        if (ok)
        {
            image.width = width;
            image.height = height;
            image.rgba.assign(static_cast<const uint8_t*>(mapped), static_cast<const uint8_t*>(mapped) + bufferSize);
            if (swapRedBlue)
            {
                for (size_t i = 0; i < image.rgba.size(); i += 4) std::swap(image.rgba[i], image.rgba[i + 2]);
            }
            vkUnmapMemory(vkDevice, stagingMemory);
        }

        vkDestroyFence(vkDevice, fence, nullptr);
        vkDestroyCommandPool(vkDevice, pool, nullptr);
        vkFreeMemory(vkDevice, stagingMemory, nullptr);
        vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
        return ok;
    }

    bool RenderPipeline::writeScreenshot(const vsg::Path& filename) const
    {
        Capture image;
        if (!capture(image)) return false;
        const auto ok = writePng(filename, image.width, image.height, image.rgba.data());
        if (ok) vsg::info("vsgOcean - wrote screenshot ", filename);
        return ok;
    }
}
