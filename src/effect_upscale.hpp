#ifndef EFFECT_UPSCALE_HPP_INCLUDED
#define EFFECT_UPSCALE_HPP_INCLUDED
#include <memory>
#include <vector>

#include "config.hpp"
#include "effect.hpp"
#include "logical_device.hpp"

namespace vkBasalt
{
    // EASU (FidelityFX Super Resolution 1.0) upscaler. Samples the reduced render
    // target and writes an edge directed upscale into the output images, which are
    // larger than the input ones.
    class UpscaleEffect : public Effect
    {
    public:
        UpscaleEffect(LogicalDevice*       pLogicalDevice,
                      VkFormat             format,
                      VkExtent2D           srcExtent,
                      VkExtent2D           dstExtent,
                      std::vector<VkImage> inputImages,
                      std::vector<VkImage> outputImages,
                      Config*              pConfig);

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;

        ~UpscaleEffect() override;

    private:
        void init(LogicalDevice*       pLogicalDevice,
                  VkFormat             format,
                  VkExtent2D           srcExtent,
                  VkExtent2D           dstExtent,
                  std::vector<VkImage> inputImages,
                  std::vector<VkImage> outputImages,
                  Config*              pConfig);

        LogicalDevice*                     pLogicalDevice = nullptr;
        Config*                            pConfig        = nullptr;
        VkFormat                           format{};
        VkExtent2D                         srcExtent{};
        VkExtent2D                         dstExtent{};
        std::vector<VkImage>               inputImages;
        std::vector<VkImage>               outputImages;
        std::vector<VkImageView>           inputImageViews;
        std::vector<VkImageView>           outputImageViews;
        VkSampler                          sampler                         = VK_NULL_HANDLE;
        VkDescriptorSetLayout              imageSamplerDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool                   descriptorPool                  = VK_NULL_HANDLE;
        VkShaderModule                     vertexModule                    = VK_NULL_HANDLE;
        VkShaderModule                     fragmentModule                  = VK_NULL_HANDLE;
        VkRenderPass                       renderPass                      = VK_NULL_HANDLE;
        VkPipelineLayout                   pipelineLayout                  = VK_NULL_HANDLE;
        VkPipeline                         graphicsPipeline                = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet>       imageDescriptorSets;
        std::vector<VkFramebuffer>         framebuffers;
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        VkSpecializationInfo               fragmentSpecInfo{};
        VkSpecializationMapEntry           fragmentSpecEntries[2]{};
        float                              specData[2]{};
    };

    // Plain linear blit, the cheap fallback (upscaleFilter = bilinear).
    class UpscaleBlitEffect : public Effect
    {
    public:
        UpscaleBlitEffect(LogicalDevice*       pLogicalDevice,
                          VkFormat             format,
                          VkExtent2D           srcExtent,
                          VkExtent2D           dstExtent,
                          std::vector<VkImage> inputImages,
                          std::vector<VkImage> outputImages,
                          Config*              pConfig);

        void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;

        ~UpscaleBlitEffect() override;

    private:
        LogicalDevice*       pLogicalDevice;
        VkFormat             format;
        VkExtent2D           srcExtent;
        VkExtent2D           dstExtent;
        std::vector<VkImage> inputImages;
        std::vector<VkImage> outputImages;
        Config*              pConfig;
    };

    std::shared_ptr<Effect> createUpscaleEffect(LogicalDevice*       pLogicalDevice,
                                                VkFormat             format,
                                                VkExtent2D           srcExtent,
                                                VkExtent2D           dstExtent,
                                                std::vector<VkImage> inputImages,
                                                std::vector<VkImage> outputImages,
                                                Config*              pConfig);
} // namespace vkBasalt

#endif // EFFECT_UPSCALE_HPP_INCLUDED