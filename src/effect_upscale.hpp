#ifndef EFFECT_UPSCALE_HPP_INCLUDED
#define EFFECT_UPSCALE_HPP_INCLUDED
#include <vector>

#include "config.hpp"
#include "effect.hpp"
#include "logical_device.hpp"

namespace vkBasalt
{
    // Blits a lower resolution image set into a higher resolution one with linear
    // filtering. This is the "render at a fraction of the output size and upscale"
    // path: everything the application draws goes to srcExtent, everything after
    // this effect runs at dstExtent.
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
        LogicalDevice*       pLogicalDevice;
        VkFormat             format;
        VkExtent2D           srcExtent;
        VkExtent2D           dstExtent;
        std::vector<VkImage> inputImages;
        std::vector<VkImage> outputImages;
        Config*              pConfig;
    };
} // namespace vkBasalt

#endif // EFFECT_UPSCALE_HPP_INCLUDED
