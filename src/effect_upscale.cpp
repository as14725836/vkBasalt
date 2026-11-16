#include "effect_upscale.hpp"

#include <cstring>

#include "descriptor_set.hpp"
#include "framebuffer.hpp"
#include "graphics_pipeline.hpp"
#include "image_view.hpp"
#include "logger.hpp"
#include "renderpass.hpp"
#include "sampler.hpp"
#include "shader.hpp"
#include "shader_sources.hpp"
#include "util.hpp"

namespace vkBasalt
{
    UpscaleEffect::UpscaleEffect(LogicalDevice*       pLogicalDevice,
                                 VkFormat             format,
                                 VkExtent2D           srcExtent,
                                 VkExtent2D           dstExtent,
                                 std::vector<VkImage> inputImages,
                                 std::vector<VkImage> outputImages,
                                 Config*              pConfig)
    {
        init(pLogicalDevice, format, srcExtent, dstExtent, inputImages, outputImages, pConfig);
    }

    void UpscaleEffect::init(LogicalDevice*       pLogicalDevice,
                             VkFormat             format,
                             VkExtent2D           srcExtent,
                             VkExtent2D           dstExtent,
                             std::vector<VkImage> inputImages,
                             std::vector<VkImage> outputImages,
                             Config*              pConfig)
    {
        Logger::debug("in creating UpscaleEffect " + std::to_string(srcExtent.width) + "x" + std::to_string(srcExtent.height) + " -> "
                      + std::to_string(dstExtent.width) + "x" + std::to_string(dstExtent.height));
        this->pLogicalDevice = pLogicalDevice;
        this->format         = format;
        this->srcExtent      = srcExtent;
        this->dstExtent      = dstExtent;
        this->inputImages    = inputImages;
        this->outputImages   = outputImages;
        this->pConfig        = pConfig;

        inputImageViews  = createImageViews(pLogicalDevice, format, inputImages);
        outputImageViews = createImageViews(pLogicalDevice, format, outputImages);
        sampler          = createSampler(pLogicalDevice);

        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, 1);

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = inputImages.size() + 10;
        std::vector<VkDescriptorPoolSize> poolSizes = {imagePoolSize};
        descriptorPool                              = createDescriptorPool(pLogicalDevice, poolSizes);

        createShaderModule(pLogicalDevice, full_screen_triangle_vert, &vertexModule);
        createShaderModule(pLogicalDevice, easu_frag, &fragmentModule);

        renderPass = createRenderPass(pLogicalDevice, format);
        descriptorSetLayouts.insert(descriptorSetLayouts.begin(), imageSamplerDescriptorSetLayout);
        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts);

        // the shader needs the output size for the EASU constants, the input size
        // comes from textureSize()
        specData[0] = static_cast<float>(dstExtent.width);
        specData[1] = static_cast<float>(dstExtent.height);

        fragmentSpecEntries[0].constantID = 0;
        fragmentSpecEntries[0].offset     = 0;
        fragmentSpecEntries[0].size       = sizeof(float);
        fragmentSpecEntries[1].constantID = 1;
        fragmentSpecEntries[1].offset     = sizeof(float);
        fragmentSpecEntries[1].size       = sizeof(float);

        fragmentSpecInfo.mapEntryCount = 2;
        fragmentSpecInfo.pMapEntries   = fragmentSpecEntries;
        fragmentSpecInfo.dataSize      = sizeof(specData);
        fragmentSpecInfo.pData         = specData;

        graphicsPipeline = createGraphicsPipeline(pLogicalDevice,
                                                  vertexModule,
                                                  nullptr,
                                                  "main",
                                                  fragmentModule,
                                                  &fragmentSpecInfo,
                                                  "main",
                                                  dstExtent,
                                                  renderPass,
                                                  pipelineLayout);

        imageDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(
            pLogicalDevice, descriptorPool, imageSamplerDescriptorSetLayout, {sampler}, std::vector<std::vector<VkImageView>>(1, inputImageViews));

        framebuffers = createFramebuffers(pLogicalDevice, renderPass, dstExtent, {outputImageViews});
    }

    void UpscaleEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        VkImageMemoryBarrier memoryBarrier;
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.pNext               = nullptr;
        memoryBarrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];
        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.baseMipLevel   = 0;
        memoryBarrier.subresourceRange.levelCount     = 1;
        memoryBarrier.subresourceRange.baseArrayLayer = 0;
        memoryBarrier.subresourceRange.layerCount     = 1;

        VkImageMemoryBarrier secondBarrier = memoryBarrier;
        secondBarrier.srcAccessMask        = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask        = 0;
        secondBarrier.oldLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout            = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        VkRenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext             = nullptr;
        renderPassBeginInfo.renderPass        = renderPass;
        renderPassBeginInfo.framebuffer       = framebuffers[imageIndex];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = dstExtent;
        VkClearValue clearValue               = {0.0f, 0.0f, 0.0f, 1.0f};
        renderPassBeginInfo.clearValueCount   = 1;
        renderPassBeginInfo.pClearValues      = &clearValue;

        pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        pLogicalDevice->vkd.CmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &(imageDescriptorSets[imageIndex]), 0, nullptr);
        pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        pLogicalDevice->vkd.CmdDraw(commandBuffer, 3, 1, 0, 0);
        pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               1,
                                               &secondBarrier);
    }

    UpscaleEffect::~UpscaleEffect()
    {
        Logger::debug("destroying UpscaleEffect " + convertToString(this));
        pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, graphicsPipeline, nullptr);
        pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, vertexModule, nullptr);
        pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, fragmentModule, nullptr);
        pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);
        for (unsigned int i = 0; i < framebuffers.size(); i++)
        {
            pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, framebuffers[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, inputImageViews[i], nullptr);
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, outputImageViews[i], nullptr);
        }
        pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);
    }

    UpscaleBlitEffect::UpscaleBlitEffect(LogicalDevice*       pLogicalDevice,
                                         VkFormat             format,
                                         VkExtent2D           srcExtent,
                                         VkExtent2D           dstExtent,
                                         std::vector<VkImage> inputImages,
                                         std::vector<VkImage> outputImages,
                                         Config*              pConfig)
    {
        this->pLogicalDevice = pLogicalDevice;
        this->format         = format;
        this->srcExtent      = srcExtent;
        this->dstExtent      = dstExtent;
        this->inputImages    = inputImages;
        this->outputImages   = outputImages;
        this->pConfig        = pConfig;
    }

    void UpscaleBlitEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        VkImageBlit imageBlit = {};
        imageBlit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.srcSubresource.layerCount     = 1;
        imageBlit.srcOffsets[1]                 = {(int32_t)srcExtent.width, (int32_t)srcExtent.height, 1};
        imageBlit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.dstSubresource.layerCount     = 1;
        imageBlit.dstOffsets[1]                 = {(int32_t)dstExtent.width, (int32_t)dstExtent.height, 1};

        VkImageMemoryBarrier memoryBarrier     = {};
        memoryBarrier.sType                    = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask            = 0;
        memoryBarrier.dstAccessMask            = VK_ACCESS_TRANSFER_READ_BIT;
        memoryBarrier.oldLayout                = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        memoryBarrier.newLayout                = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex      = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex      = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image                    = inputImages[imageIndex];
        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.levelCount     = 1;
        memoryBarrier.subresourceRange.layerCount     = 1;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        memoryBarrier.image         = outputImages[imageIndex];
        memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        memoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        pLogicalDevice->vkd.CmdBlitImage(commandBuffer,
                                         inputImages[imageIndex],
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                         outputImages[imageIndex],
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         1,
                                         &imageBlit,
                                         VK_FILTER_LINEAR);

        memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.dstAccessMask = 0;
        memoryBarrier.image         = outputImages[imageIndex];
        memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        memoryBarrier.image         = inputImages[imageIndex];
        memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
    }

    UpscaleBlitEffect::~UpscaleBlitEffect()
    {
    }

    std::shared_ptr<Effect> createUpscaleEffect(LogicalDevice*       pLogicalDevice,
                                                VkFormat             format,
                                                VkExtent2D           srcExtent,
                                                VkExtent2D           dstExtent,
                                                std::vector<VkImage> inputImages,
                                                std::vector<VkImage> outputImages,
                                                Config*              pConfig)
    {
        std::string filter = pConfig->getOption<std::string>("upscaleFilter", "easu");
        if (filter == "bilinear")
        {
            Logger::debug("using bilinear upscale filter");
            return std::shared_ptr<Effect>(
                new UpscaleBlitEffect(pLogicalDevice, format, srcExtent, dstExtent, inputImages, outputImages, pConfig));
        }
        Logger::debug("using EASU upscale filter");
        return std::shared_ptr<Effect>(new UpscaleEffect(pLogicalDevice, format, srcExtent, dstExtent, inputImages, outputImages, pConfig));
    }
} // namespace vkBasalt