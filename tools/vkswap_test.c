// Minimal reproduction of the WSI path a game takes: create an XCB surface, query
// the capabilities, create a swapchain the way an application that ignores
// currentExtent does (full surface size) and fetch the images.
//
// Used to test the layer without rendering, since llvmpipe SIGILLs on this box.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#define CHECK(x) \
    do \
    { \
        VkResult r_ = (x); \
        if (r_ != VK_SUCCESS) \
        { \
            fprintf(stderr, "FAIL %s -> %d\n", #x, (int)r_); \
            return 1; \
        } \
    } while (0)

int main(int argc, char** argv)
{
    xcb_connection_t* conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn))
    {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }

    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
    xcb_screen_t*         screen = iter.data;

    int W = 1920, H = 1080;
    xcb_window_t wid = xcb_generate_id(conn);
    uint32_t     mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t     vals[2] = {screen->black_pixel, XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, wid, screen->root, 0, 0, W, H, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, vals);
    xcb_map_window(conn, wid);
    xcb_flush(conn);

    const char* instExts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME};

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion        = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo        = &app;
    ici.enabledExtensionCount   = 3;
    ici.ppEnabledExtensionNames = instExts;

    VkInstance inst;
    CHECK(vkCreateInstance(&ici, NULL, &inst));

    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(inst, &count, NULL));
    VkPhysicalDevice gpus[8];
    if (count > 8)
        count = 8;
    CHECK(vkEnumeratePhysicalDevices(inst, &count, gpus));
    VkPhysicalDevice gpu = gpus[0];

    VkXcbSurfaceCreateInfoKHR sci = {VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
    sci.connection                = conn;
    sci.window                    = wid;

    VkSurfaceKHR surf;
    CHECK(vkCreateXcbSurfaceKHR(inst, &sci, NULL, &surf));

    // what the layer reports to us
    VkSurfaceCapabilitiesKHR caps;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surf, &caps));
    printf("caps: current %ux%u  min %ux%u  max %ux%u\n",
           caps.currentExtent.width,
           caps.currentExtent.height,
           caps.minImageExtent.width,
           caps.minImageExtent.height,
           caps.maxImageExtent.width,
           caps.maxImageExtent.height);

    // and via the 2KHR variant, which is what DXVK uses
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR pCaps2 =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR) vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    if (pCaps2)
    {
        VkPhysicalDeviceSurfaceInfo2KHR info = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
        info.surface                         = surf;
        VkSurfaceCapabilities2KHR caps2      = {VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
        VkResult                  r          = pCaps2(gpu, &info, &caps2);
        printf("caps2: r=%d current %ux%u\n", (int)r, caps2.surfaceCapabilities.currentExtent.width, caps2.surfaceCapabilities.currentExtent.height);
    }
    else
    {
        printf("caps2: not available\n");
    }

    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, NULL);
    VkQueueFamilyProperties qprops[16];
    if (qcount > 16)
        qcount = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, qprops);

    uint32_t qfam = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < qcount; i++)
    {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surf, &present);
        if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
        {
            qfam = i;
            break;
        }
    }
    if (qfam == 0xFFFFFFFFu)
    {
        fprintf(stderr, "no graphics+present queue family\n");
        return 1;
    }

    float                    prio = 1.0f;
    VkDeviceQueueCreateInfo  qci  = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex          = qfam;
    qci.queueCount                = 1;
    qci.pQueuePriorities          = &prio;

    const char*        devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci       = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount     = 1;
    dci.pQueueCreateInfos        = &qci;
    dci.enabledExtensionCount    = 1;
    dci.ppEnabledExtensionNames  = devExts;

    VkDevice dev;
    CHECK(vkCreateDevice(gpu, &dci, NULL, &dev));

    uint32_t fcount = 0;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &fcount, NULL));
    VkSurfaceFormatKHR fmts[64];
    if (fcount > 64)
        fcount = 64;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &fcount, fmts));
    printf("format %d colorspace %d\n", (int)fmts[0].format, (int)fmts[0].colorSpace);

    // deliberately ask for the full surface size, the way the game does, ignoring
    // whatever currentExtent the layer reported
    VkSwapchainCreateInfoKHR scci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    scci.surface                  = surf;
    scci.minImageCount            = caps.minImageCount + 1;
    scci.imageFormat              = fmts[0].format;
    scci.imageColorSpace          = fmts[0].colorSpace;
    scci.imageExtent.width        = W;
    scci.imageExtent.height       = H;
    scci.imageArrayLayers         = 1;
    scci.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scci.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    scci.preTransform             = caps.currentTransform;
    scci.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scci.presentMode              = VK_PRESENT_MODE_FIFO_KHR;
    scci.clipped                  = VK_TRUE;

    // "honor" mode asks for exactly what the layer reported, the way a well
    // behaved application does; the default asks for the surface size and ignores
    // the capabilities, which is what the reported game does
    bool honor = argc > 1 && strcmp(argv[1], "honor") == 0;
    if (honor)
    {
        W                       = (int)caps.currentExtent.width;
        H                       = (int)caps.currentExtent.height;
        scci.imageExtent.width  = caps.currentExtent.width;
        scci.imageExtent.height = caps.currentExtent.height;
    }
    printf("requesting swapchain %dx%d (%s)\n", W, H, honor ? "honouring caps" : "ignoring caps");
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    VkResult       r  = vkCreateSwapchainKHR(dev, &scci, NULL, &sc);
    printf("vkCreateSwapchainKHR -> %d\n", (int)r);

    if (r == VK_SUCCESS)
    {
        uint32_t icount = 0;
        VkResult r2     = vkGetSwapchainImagesKHR(dev, sc, &icount, NULL);
        printf("vkGetSwapchainImagesKHR -> %d count=%u\n", (int)r2, icount);
        VkImage imgs[8];
        if (icount > 8)
            icount = 8;
        vkGetSwapchainImagesKHR(dev, sc, &icount, imgs);
        vkDestroySwapchainKHR(dev, sc, NULL);
    }

    vkDeviceWaitIdle(dev);
    vkDestroyDevice(dev, NULL);
    vkDestroySurfaceKHR(inst, surf, NULL);
    vkDestroyInstance(inst, NULL);
    xcb_disconnect(conn);
    printf("done\n");
    return 0;
}