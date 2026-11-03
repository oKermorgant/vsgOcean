/* <editor-fold desc="MIT License">

Copyright(c) 2026 Olivier Kermorgant

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgOcean/scene/SemanticPass.h>

#include "semantic_frag.h"
#include "semantic_vert.h"

#include <vsg/io/Logger.h>

#include <algorithm>
#include <map>
#include <utility>

namespace vsgOcean
{
    namespace
    {
        // Rebuild a model's subtree for the semantic pass: the containers are
        // cloned (so the model's own pipelines and material binds can be
        // swapped or dropped), while vertex / index / draw commands are reused
        // as they are - the shadow shares the original geometry buffers.
        class ShadowBuilder : public vsg::Visitor
        {
        public:
            explicit ShadowBuilder(vsg::ref_ptr<vsg::BindGraphicsPipeline> semanticPipeline) :
                semanticPipeline_{std::move(semanticPipeline)}
            {
            }

            vsg::ref_ptr<vsg::Node> takeRoot()
            {
                return std::move(root_);
            }

            // the (dynamic frame, shadow transform) pairs for pose sync
            std::vector<std::pair<vsg::ref_ptr<Frame>, vsg::ref_ptr<vsg::MatrixTransform>>> frames;

            void apply(vsg::Group& group) override
            {
                descend(vsg::Group::create(), group);
            }

            void apply(vsg::MatrixTransform& transform) override
            {
                auto copy{vsg::MatrixTransform::create(transform.matrix)};
                // the dynamic frames (vsgOcean's registered Frame tree) are
                // cloned but kept in sync by SemanticPass::update()
                if (const auto frame = dynamic_cast<Frame*>(&transform))
                {
                    frames.emplace_back(vsg::ref_ptr<Frame>(frame), vsg::ref_ptr<vsg::MatrixTransform>(copy));
                }
                descend(copy, transform);
            }

            void apply(vsg::StateGroup& stateGroup) override
            {
                auto copy{vsg::StateGroup::create()};
                for (const auto& state : stateGroup.stateCommands)
                {
                    if (dynamic_cast<const vsg::BindGraphicsPipeline*>(state.get()))
                    {
                        copy->add(semanticPipeline_); // every model pipeline becomes the semantic one
                    }
                    else if (dynamic_cast<const vsg::BindDescriptorSet*>(state.get()) ||
                             dynamic_cast<const vsg::BindDescriptorSets*>(state.get()))
                    {
                        // the materials, textures and lights of the color pass
                        // are not part of the shadow
                    }
                    else
                    {
                        copy->add(state); // e.g. dynamic or vertex-binding states
                    }
                }
                descend(copy, stateGroup);
            }

            void apply(vsg::CullNode& cullNode) override
            {
                // flattened: the culling is an optimization the shadow does not
                // need - the child's state groups must still be rebuilt
                if (cullNode.child) cullNode.child->accept(*this);
            }

            void apply(vsg::Object& object) override
            {
                // everything the builder does not rewrite (a Commands node
                // with its draw calls, a VertexIndexDraw, ...) is reused as it
                // is: the shadow shares the original geometry buffers
                if (const auto node = dynamic_cast<vsg::Node*>(&object))
                {
                    attach(vsg::ref_ptr<vsg::Node>(node));
                }
                else
                {
                    vsg::warn("vsgOcean::SemanticPass - a non-node object is skipped in the shadow");
                }
            }

        protected:
            void attach(const vsg::ref_ptr<vsg::Node>& node)
            {
                if (stack_.empty())
                    root_ = node;
                else
                    stack_.back()->addChild(node);
            }

            template<typename NodeType, typename Original>
            void descend(const vsg::ref_ptr<NodeType>& copy, Original& original)
            {
                attach(copy);
                stack_.push_back(copy);
                original.traverse(*this);
                stack_.pop_back();
            }

            vsg::ref_ptr<vsg::BindGraphicsPipeline> semanticPipeline_;
            vsg::ref_ptr<vsg::Node> root_;
            std::vector<vsg::ref_ptr<vsg::Group>> stack_;
        };

        // check no model pipeline survived the rebuild: a lit pipeline in the
        // shadow would corrupt the IDs (a warning, not a silent defect)
        class PipelineAudit : public vsg::Visitor
        {
        public:
            explicit PipelineAudit(vsg::ref_ptr<vsg::BindGraphicsPipeline> semanticPipeline) :
                semanticPipeline_{std::move(semanticPipeline)}
            {
            }

            size_t foreign = 0;

            void apply(vsg::StateGroup& stateGroup) override
            {
                for (const auto& state : stateGroup.stateCommands)
                {
                    if (const auto bind = dynamic_cast<const vsg::BindGraphicsPipeline*>(state.get());
                        bind && bind != semanticPipeline_.get())
                        ++foreign;
                }
                stateGroup.traverse(*this);
            }

            void apply(vsg::Object& object) override { object.traverse(*this); }

        protected:
            vsg::ref_ptr<vsg::BindGraphicsPipeline> semanticPipeline_;
        };
    }

    SemanticPass::SemanticPass(const vsg::ref_ptr<Scene>& scene, vsg::ref_ptr<vsg::Device> device,
                               vsg::ref_ptr<vsg::Camera> camera, VkExtent2D extent) :
        device_{std::move(device)},
        extent_{extent}
    {
        if (!scene || !device_ || !camera || extent_.width == 0 || extent_.height == 0) return;

        // --- the semantic pipeline: position in, ID out, depth-tested with the
        // scene's reversed depth; the ID rides the push constants behind the
        // camera block (projection, modelview) the record traversal provides
        const vsg::PushConstantRanges pushConstants{{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(vsg::mat4)},
                                                    {VK_SHADER_STAGE_FRAGMENT_BIT, 2 * sizeof(vsg::mat4), sizeof(vsg::vec4)}};

        const auto vertexInput = vsg::VertexInputState::create(
            vsg::VertexInputState::Bindings{VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}},
            vsg::VertexInputState::Attributes{VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}});

        auto depthStencil = vsg::DepthStencilState::create();
        depthStencil->depthTestEnable = VK_TRUE;
        depthStencil->depthWriteEnable = VK_TRUE;
        depthStencil->depthCompareOp = VK_COMPARE_OP_GREATER; // the scene renders reversed depth

        auto rasterization = vsg::RasterizationState::create();
        rasterization->cullMode = VK_CULL_MODE_NONE; // correctness over speed: no winding surprises

        const auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{}, pushConstants);
        const auto pipeline = vsg::GraphicsPipeline::create(
            pipelineLayout,
            vsg::ShaderStages{
                vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", semantic_vert),
                vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", semantic_frag)},
            vsg::GraphicsPipelineStates{vertexInput, vsg::InputAssemblyState::create(), rasterization,
                                        vsg::MultisampleState::create(), vsg::ColorBlendState::create(), depthStencil});
        const auto semanticBind = vsg::BindGraphicsPipeline::create(pipeline);

        // --- the shadow scene: one state group per model binding its ID
        auto shadowRoot = vsg::Group::create();
        for (const auto& name : scene->modelNames())
        {
            const auto model = scene->model(name);
            if (!model || name.empty()) continue;

            ShadowBuilder builder{semanticBind};
            builder.apply(*model);
            if (const auto modelShadow = builder.takeRoot())
            {
                // the model ID: 1..N, encoded as the RGB the fragment writes
                const auto id = static_cast<uint32_t>(names_.size() + 1);
                auto modelGroup = vsg::StateGroup::create();
                // the ID bytes as normalized color: the UNORM target keeps
                // round(c * 255) = the byte back, exactly
                const auto color = vsg::vec4(static_cast<float>(id & 0xffu) / 255.0f,
                                             static_cast<float>((id >> 8) & 0xffu) / 255.0f,
                                             static_cast<float>((id >> 16) & 0xffu) / 255.0f, 1.0f);
                modelGroup->add(vsg::PushConstants::create(VK_SHADER_STAGE_FRAGMENT_BIT, 2 * sizeof(vsg::mat4),
                                                           vsg::vec4Value::create(color)));
                modelGroup->addChild(modelShadow);
                shadowRoot->addChild(modelGroup);
                names_.push_back(name);
                frames_.insert(frames_.end(), builder.frames.begin(), builder.frames.end());
            }
        }

        // any pipeline the builder did not rewrite would corrupt the IDs
        PipelineAudit audit{semanticBind};
        shadowRoot->accept(audit);
        if (audit.foreign > 0)
        {
            vsg::warn("vsgOcean::SemanticPass - ", audit.foreign,
                      " color pipelines remain in the shadow (an unsupported container type?); "
                      "their pixels will not carry model IDs");
        }

        // --- the offscreen target: a UNORM color attachment (the ID bytes are
        // exact, no sRGB conversion) readable back, and a depth attachment
        // for the occlusion between models
        auto colorImage = vsg::Image::create();
        colorImage->imageType = VK_IMAGE_TYPE_2D;
        colorImage->format = VK_FORMAT_R8G8B8A8_UNORM;
        colorImage->extent = VkExtent3D{extent_.width, extent_.height, 1};
        colorImage->mipLevels = 1;
        colorImage->arrayLayers = 1;
        colorImage->samples = VK_SAMPLE_COUNT_1_BIT;
        colorImage->tiling = VK_IMAGE_TILING_OPTIMAL;
        colorImage->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        colorImage->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorImage->compile(device_.get());
        colorImage->allocateAndBindMemory(device_.get());
        colorImage_ = colorImage;

        auto depthImage = vsg::Image::create();
        depthImage->imageType = VK_IMAGE_TYPE_2D;
        depthImage->format = VK_FORMAT_D24_UNORM_S8_UINT;
        depthImage->extent = VkExtent3D{extent_.width, extent_.height, 1};
        depthImage->mipLevels = 1;
        depthImage->arrayLayers = 1;
        depthImage->samples = VK_SAMPLE_COUNT_1_BIT;
        depthImage->tiling = VK_IMAGE_TILING_OPTIMAL;
        depthImage->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthImage->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthImage->compile(device_.get());
        depthImage->allocateAndBindMemory(device_.get());

        const auto colorView = vsg::ImageView::create(colorImage, VK_IMAGE_ASPECT_COLOR_BIT);
        const auto depthView = vsg::ImageView::create(depthImage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        colorView->compile(device_.get());
        depthView->compile(device_.get());

        auto colorAttachment = vsg::defaultColorAttachment(VK_FORMAT_R8G8B8A8_UNORM);
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // read back after the pass
        auto depthAttachment = vsg::defaultDepthAttachment(VK_FORMAT_D24_UNORM_S8_UINT);
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // never read back

        vsg::SubpassDescription subpass{};
        subpass.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        subpass.depthStencilAttachments = {{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
        renderPass_ = vsg::RenderPass::create(device_.get(), vsg::RenderPass::Attachments{colorAttachment, depthAttachment},
                                              vsg::RenderPass::Subpasses{subpass}, vsg::RenderPass::Dependencies{});
        framebuffer_ = vsg::Framebuffer::create(renderPass_, vsg::ImageViews{colorView, depthView},
                                                extent_.width, extent_.height, 1);

        // the shadow renders from the shared camera
        view_ = vsg::View::create(camera);
        view_->addChild(shadowRoot);

        renderGraph_ = vsg::RenderGraph::create();
        renderGraph_->renderPass = renderPass_;
        renderGraph_->framebuffer = framebuffer_;
        renderGraph_->renderArea = VkRect2D{{0, 0}, extent_};
        renderGraph_->viewportState = vsg::ViewportState::create(0, 0, extent_.width, extent_.height);
        renderGraph_->clearValues.resize(2);
        renderGraph_->clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // the background ID: 0
        renderGraph_->clearValues[1].depthStencil = {0.0f, 0};           // reversed depth: 0 is far
        renderGraph_->addChild(view_);
    }

    void SemanticPass::update()
    {
        // the dynamic frames' current poses, carried into the shadow
        for (const auto& [source, shadow] : frames_)
        {
            shadow->matrix = source->matrix;
        }
    }

    bool SemanticPass::capture(Image& image) const
    {
        if (!colorImage_) return false;

        const auto width = extent_.width;
        const auto height = extent_.height;
        const auto bufferSize = VkDeviceSize(width) * height * 4;

        const auto families = device_->getPhysicalDevice()->getQueueFamilyProperties();
        uint32_t family = 0;
        for (; family < families.size(); ++family)
        {
            if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
        }
        const auto queue = device_->getQueue(family);
        const VkDevice vkDevice = *device_;
        const VkQueue vkQueue = *queue;
        const VkImage vkImage = colorImage_->vk(0);

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

        // the render pass ends with the color in TRANSFER_SRC layout: make the
        // writes visible to the copy, no layout change
        auto beginInfo = VkCommandBufferBeginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command, &beginInfo);

        auto barrier = VkImageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vkImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(command, vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);
        vkEndCommandBuffer(command);

        auto submitInfo = VkSubmitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command;

        auto fenceInfo = VkFenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence);
        if (vkQueueSubmit(vkQueue, 1, &submitInfo, fence) != VK_SUCCESS ||
            vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        {
            vkDestroyFence(vkDevice, fence, nullptr);
            vkDestroyCommandPool(vkDevice, pool, nullptr);
            vkFreeMemory(vkDevice, stagingMemory, nullptr);
            vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
            return false;
        }

        void* mapped = nullptr;
        const auto ok = vkMapMemory(vkDevice, stagingMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS;
        if (!ok)
        {
            vkDestroyFence(vkDevice, fence, nullptr);
            vkDestroyCommandPool(vkDevice, pool, nullptr);
            vkFreeMemory(vkDevice, stagingMemory, nullptr);
            vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
            return false;
        }
        image.width = width;
        image.height = height;
        image.ids.assign(width * height, 0);
        const auto* bytes = static_cast<const uint8_t*>(mapped);
        for (uint32_t pixel = 0; pixel < width * height; ++pixel)
        {
            image.ids[pixel] = bytes[4 * pixel] | (uint32_t(bytes[4 * pixel + 1]) << 8) | (uint32_t(bytes[4 * pixel + 2]) << 16);
        }
        vkUnmapMemory(vkDevice, stagingMemory);

        vkDestroyFence(vkDevice, fence, nullptr);
        vkDestroyCommandPool(vkDevice, pool, nullptr);
        vkFreeMemory(vkDevice, stagingMemory, nullptr);
        vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
        return ok;
    }

    std::vector<SemanticPass::Annotation> SemanticPass::boxes(const Image& image) const
    {
        std::vector<Annotation> annotations;
        if (image.width == 0 || image.height == 0) return annotations;

        // the running bounds per model ID: min/max on both axes + pixel counts
        std::map<uint32_t, Annotation> found;
        for (uint32_t y = 0; y < image.height; ++y)
        {
            const auto row = y * image.width;
            for (uint32_t x = 0; x < image.width; ++x)
            {
                const auto id = image.ids[row + x];
                if (id == 0 || id > names_.size()) continue;
                auto [entry, inserted] = found.try_emplace(id);
                auto& annotation = entry->second;
                if (inserted)
                {
                    annotation.id = id;
                    annotation.model = names_[id - 1];
                    annotation.x0 = annotation.x1 = x;
                    annotation.y0 = annotation.y1 = y;
                }
                else
                {
                    annotation.x0 = std::min(annotation.x0, x);
                    annotation.y0 = std::min(annotation.y0, y);
                    annotation.x1 = std::max(annotation.x1, x);
                    annotation.y1 = std::max(annotation.y1, y);
                }
                ++annotation.pixels;
            }
        }

        annotations.reserve(found.size());
        for (const auto& [id, annotation] : found) annotations.push_back(annotation);
        return annotations;
    }
}
