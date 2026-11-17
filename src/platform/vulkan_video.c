#ifdef PLATFORM_VULKAN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include "gba/defines.h"
#include "platform/vulkan_video.h"

struct VulkanVideo
{
    SDL_Window *window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamily;
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    VkImage *swapchainImages;
    VkSemaphore *renderFinishedSemaphores;
    uint32_t swapchainImageCount;
    VkImage frameImage;
    VkDeviceMemory frameImageMemory;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    void *stagingMapping;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailable;
    VkFence frameFence;
    bool frameImageInitialized;
    bool recreateSwapchain;
    bool initialized;
};

static struct VulkanVideo sVulkan;

static bool VulkanCheck(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS)
        return true;

    fprintf(stderr, "Vulkan: %s failed with VkResult %d\n", operation, result);
    return false;
}

static bool FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required, uint32_t *index)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(sVulkan.physicalDevice, &properties);

    for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
    {
        if ((typeBits & (1u << i)) != 0
            && (properties.memoryTypes[i].propertyFlags & required) == required)
        {
            *index = i;
            return true;
        }
    }

    fprintf(stderr, "Vulkan: no compatible memory type for flags 0x%x\n", required);
    return false;
}

static bool HasDeviceExtension(VkPhysicalDevice device, const char *name)
{
    uint32_t count = 0;
    if (!VulkanCheck(vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL),
                     "vkEnumerateDeviceExtensionProperties"))
        return false;

    VkExtensionProperties *extensions = malloc(sizeof(*extensions) * count);
    if (extensions == NULL)
        return false;

    bool found = false;
    if (VulkanCheck(vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions),
                    "vkEnumerateDeviceExtensionProperties"))
    {
        for (uint32_t i = 0; i < count; i++)
        {
            if (strcmp(extensions[i].extensionName, name) == 0)
            {
                found = true;
                break;
            }
        }
    }
    free(extensions);
    return found;
}

static bool SupportsBlitFormat(VkPhysicalDevice device, VkFormat format,
                               VkFormatFeatureFlags required)
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}

static bool FindPhysicalDevice(void)
{
    uint32_t count = 0;
    if (!VulkanCheck(vkEnumeratePhysicalDevices(sVulkan.instance, &count, NULL),
                     "vkEnumeratePhysicalDevices") || count == 0)
    {
        fprintf(stderr, "Vulkan: no physical devices found\n");
        return false;
    }

    VkPhysicalDevice *devices = malloc(sizeof(*devices) * count);
    if (devices == NULL)
        return false;
    if (!VulkanCheck(vkEnumeratePhysicalDevices(sVulkan.instance, &count, devices),
                     "vkEnumeratePhysicalDevices"))
    {
        free(devices);
        return false;
    }

    bool found = false;
    for (uint32_t d = 0; d < count && !found; d++)
    {
        if (!HasDeviceExtension(devices[d], VK_KHR_SWAPCHAIN_EXTENSION_NAME)
            || !SupportsBlitFormat(devices[d], VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            continue;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, NULL);
        VkQueueFamilyProperties *families = malloc(sizeof(*families) * familyCount);
        if (families == NULL)
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, families);

        for (uint32_t q = 0; q < familyCount; q++)
        {
            VkBool32 presentSupported = VK_FALSE;
            if ((families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0
                || vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], q, sVulkan.surface,
                                                        &presentSupported) != VK_SUCCESS
                || !presentSupported)
                continue;

            uint32_t formatCount = 0;
            if (vkGetPhysicalDeviceSurfaceFormatsKHR(devices[d], sVulkan.surface,
                                                     &formatCount, NULL) != VK_SUCCESS
                || formatCount == 0)
                continue;

            VkSurfaceFormatKHR *formats = malloc(sizeof(*formats) * formatCount);
            if (formats == NULL)
                continue;
            if (vkGetPhysicalDeviceSurfaceFormatsKHR(devices[d], sVulkan.surface,
                                                     &formatCount, formats) == VK_SUCCESS)
            {
                for (uint32_t f = 0; f < formatCount; f++)
                {
                    VkFormat format = formats[f].format;
                    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_BLIT_DST_BIT;
                    if ((format == VK_FORMAT_UNDEFINED
                         || SupportsBlitFormat(devices[d], format, features)))
                    {
                        sVulkan.physicalDevice = devices[d];
                        sVulkan.queueFamily = q;
                        found = true;
                        break;
                    }
                }
            }
            free(formats);
            if (found)
                break;
        }
        free(families);
    }
    free(devices);

    if (!found)
        fprintf(stderr, "Vulkan: no graphics/present device supports the required blit formats\n");
    return found;
}

static bool CreateFrameResources(void)
{
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { DISPLAY_WIDTH, DISPLAY_HEIGHT, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VulkanCheck(vkCreateImage(sVulkan.device, &imageInfo, NULL, &sVulkan.frameImage),
                     "vkCreateImage"))
        return false;

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(sVulkan.device, sVulkan.frameImage, &requirements);
    uint32_t memoryType;
    if (!FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        &memoryType))
        return false;

    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType,
    };
    if (!VulkanCheck(vkAllocateMemory(sVulkan.device, &allocation, NULL,
                                      &sVulkan.frameImageMemory), "vkAllocateMemory")
        || !VulkanCheck(vkBindImageMemory(sVulkan.device, sVulkan.frameImage,
                                          sVulkan.frameImageMemory, 0), "vkBindImageMemory"))
        return false;

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = DISPLAY_WIDTH * DISPLAY_HEIGHT * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VulkanCheck(vkCreateBuffer(sVulkan.device, &bufferInfo, NULL,
                                    &sVulkan.stagingBuffer), "vkCreateBuffer"))
        return false;

    vkGetBufferMemoryRequirements(sVulkan.device, sVulkan.stagingBuffer, &requirements);
    if (!FindMemoryType(requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &memoryType))
        return false;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (!VulkanCheck(vkAllocateMemory(sVulkan.device, &allocation, NULL,
                                      &sVulkan.stagingMemory), "vkAllocateMemory")
        || !VulkanCheck(vkBindBufferMemory(sVulkan.device, sVulkan.stagingBuffer,
                                          sVulkan.stagingMemory, 0), "vkBindBufferMemory")
        || !VulkanCheck(vkMapMemory(sVulkan.device, sVulkan.stagingMemory, 0,
                                   VK_WHOLE_SIZE, 0, &sVulkan.stagingMapping), "vkMapMemory"))
        return false;

    return true;
}

static void DestroySwapchain(void)
{
    if (sVulkan.renderFinishedSemaphores != NULL)
    {
        for (uint32_t i = 0; i < sVulkan.swapchainImageCount; i++)
        {
            if (sVulkan.renderFinishedSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(sVulkan.device, sVulkan.renderFinishedSemaphores[i], NULL);
        }
    }
    free(sVulkan.renderFinishedSemaphores);
    sVulkan.renderFinishedSemaphores = NULL;
    if (sVulkan.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(sVulkan.device, sVulkan.swapchain, NULL);
    sVulkan.swapchain = VK_NULL_HANDLE;
    free(sVulkan.swapchainImages);
    sVulkan.swapchainImages = NULL;
    sVulkan.swapchainImageCount = 0;
}

static bool RecreateSwapchain(void)
{
    int drawableWidth, drawableHeight;
    SDL_Vulkan_GetDrawableSize(sVulkan.window, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0)
        return false;

    if (!VulkanCheck(vkDeviceWaitIdle(sVulkan.device), "vkDeviceWaitIdle"))
        return false;

    VkSurfaceCapabilitiesKHR capabilities;
    if (!VulkanCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(sVulkan.physicalDevice,
                                                               sVulkan.surface, &capabilities),
                     "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return false;

    uint32_t formatCount = 0;
    if (!VulkanCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(sVulkan.physicalDevice, sVulkan.surface,
                                                          &formatCount, NULL),
                     "vkGetPhysicalDeviceSurfaceFormatsKHR") || formatCount == 0)
        return false;
    VkSurfaceFormatKHR *formats = malloc(sizeof(*formats) * formatCount);
    if (formats == NULL)
        return false;
    if (!VulkanCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(sVulkan.physicalDevice, sVulkan.surface,
                                                          &formatCount, formats),
                     "vkGetPhysicalDeviceSurfaceFormatsKHR"))
    {
        free(formats);
        return false;
    }

    VkSurfaceFormatKHR selected = formats[0];
    bool selectedSupportsBlit = false;
    const VkFormat preferred[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
    };
    for (uint32_t p = 0; p < sizeof(preferred) / sizeof(preferred[0]); p++)
    {
        for (uint32_t f = 0; f < formatCount; f++)
        {
            if (formats[f].format == preferred[p]
                && SupportsBlitFormat(sVulkan.physicalDevice, formats[f].format,
                                      VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                selected = formats[f];
                selectedSupportsBlit = true;
                p = sizeof(preferred) / sizeof(preferred[0]);
                break;
            }
        }
    }
    if (!selectedSupportsBlit)
    {
        for (uint32_t f = 0; f < formatCount; f++)
        {
            if (formats[f].format != VK_FORMAT_UNDEFINED
                && SupportsBlitFormat(sVulkan.physicalDevice, formats[f].format,
                                      VK_FORMAT_FEATURE_BLIT_DST_BIT))
            {
                selected = formats[f];
                selectedSupportsBlit = true;
                break;
            }
        }
    }
    free(formats);
    if (selected.format == VK_FORMAT_UNDEFINED)
        selected.format = VK_FORMAT_B8G8R8A8_UNORM;
    if ((!selectedSupportsBlit
         && !SupportsBlitFormat(sVulkan.physicalDevice, selected.format,
                                VK_FORMAT_FEATURE_BLIT_DST_BIT))
        || (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
    {
        fprintf(stderr, "Vulkan: swapchain images cannot be used as blit destinations\n");
        return false;
    }

    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX)
        extent = capabilities.currentExtent;
    else
    {
        extent.width = (uint32_t)drawableWidth;
        extent.height = (uint32_t)drawableHeight;
        if (extent.width < capabilities.minImageExtent.width)
            extent.width = capabilities.minImageExtent.width;
        if (extent.width > capabilities.maxImageExtent.width)
            extent.width = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height)
            extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height)
            extent.height = capabilities.maxImageExtent.height;
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0 && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0)
    {
        const VkCompositeAlphaFlagBitsKHR choices[] = {
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (uint32_t i = 0; i < sizeof(choices) / sizeof(choices[0]); i++)
        {
            if ((capabilities.supportedCompositeAlpha & choices[i]) != 0)
            {
                compositeAlpha = choices[i];
                break;
            }
        }
    }

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = sVulkan.surface,
        .minImageCount = imageCount,
        .imageFormat = selected.format,
        .imageColorSpace = selected.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = sVulkan.swapchain,
    };
    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (!VulkanCheck(vkCreateSwapchainKHR(sVulkan.device, &createInfo, NULL, &newSwapchain),
                     "vkCreateSwapchainKHR"))
        return false;

    uint32_t newCount = 0;
    if (!VulkanCheck(vkGetSwapchainImagesKHR(sVulkan.device, newSwapchain, &newCount, NULL),
                     "vkGetSwapchainImagesKHR"))
    {
        vkDestroySwapchainKHR(sVulkan.device, newSwapchain, NULL);
        return false;
    }
    VkImage *newImages = malloc(sizeof(*newImages) * newCount);
    if (newImages == NULL
        || !VulkanCheck(vkGetSwapchainImagesKHR(sVulkan.device, newSwapchain, &newCount, newImages),
                        "vkGetSwapchainImagesKHR"))
    {
        free(newImages);
        vkDestroySwapchainKHR(sVulkan.device, newSwapchain, NULL);
        return false;
    }

    VkSemaphore *newRenderFinishedSemaphores = calloc(newCount,
                                                       sizeof(*newRenderFinishedSemaphores));
    if (newRenderFinishedSemaphores == NULL)
    {
        free(newImages);
        vkDestroySwapchainKHR(sVulkan.device, newSwapchain, NULL);
        return false;
    }
    VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    uint32_t createdSemaphoreCount = 0;
    for (; createdSemaphoreCount < newCount; createdSemaphoreCount++)
    {
        if (!VulkanCheck(vkCreateSemaphore(sVulkan.device, &semaphoreInfo, NULL,
                                           &newRenderFinishedSemaphores[createdSemaphoreCount]),
                         "vkCreateSemaphore"))
            break;
    }
    if (createdSemaphoreCount != newCount)
    {
        for (uint32_t i = 0; i < createdSemaphoreCount; i++)
            vkDestroySemaphore(sVulkan.device, newRenderFinishedSemaphores[i], NULL);
        free(newRenderFinishedSemaphores);
        free(newImages);
        vkDestroySwapchainKHR(sVulkan.device, newSwapchain, NULL);
        return false;
    }

    DestroySwapchain();
    sVulkan.swapchain = newSwapchain;
    sVulkan.swapchainImages = newImages;
    sVulkan.renderFinishedSemaphores = newRenderFinishedSemaphores;
    sVulkan.swapchainImageCount = newCount;
    sVulkan.swapchainFormat = selected.format;
    sVulkan.swapchainExtent = extent;
    sVulkan.recreateSwapchain = false;
    return true;
}

static bool CreateInstanceAndSurface(void)
{
    unsigned int extensionCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(sVulkan.window, &extensionCount, NULL))
    {
        fprintf(stderr, "SDL Vulkan extensions: %s\n", SDL_GetError());
        return false;
    }
    const char **extensions = malloc(sizeof(*extensions) * extensionCount);
    if (extensions == NULL)
        return false;
    if (!SDL_Vulkan_GetInstanceExtensions(sVulkan.window, &extensionCount, extensions))
    {
        fprintf(stderr, "SDL Vulkan extensions: %s\n", SDL_GetError());
        free(extensions);
        return false;
    }

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "pokeemerald",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "pokeemerald",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &applicationInfo,
        .enabledExtensionCount = extensionCount,
        .ppEnabledExtensionNames = extensions,
    };
    VkResult result = vkCreateInstance(&instanceInfo, NULL, &sVulkan.instance);
    free(extensions);
    if (!VulkanCheck(result, "vkCreateInstance"))
        return false;

    if (!SDL_Vulkan_CreateSurface(sVulkan.window, sVulkan.instance, &sVulkan.surface))
    {
        fprintf(stderr, "SDL Vulkan surface: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool CreateDevice(void)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = sVulkan.queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = extensions,
    };
    if (!VulkanCheck(vkCreateDevice(sVulkan.physicalDevice, &deviceInfo, NULL, &sVulkan.device),
                     "vkCreateDevice"))
        return false;
    vkGetDeviceQueue(sVulkan.device, sVulkan.queueFamily, 0, &sVulkan.queue);
    return true;
}

static bool CreateCommandResources(void)
{
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = sVulkan.queueFamily,
    };
    if (!VulkanCheck(vkCreateCommandPool(sVulkan.device, &poolInfo, NULL, &sVulkan.commandPool),
                     "vkCreateCommandPool"))
        return false;

    VkCommandBufferAllocateInfo commandInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = sVulkan.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (!VulkanCheck(vkAllocateCommandBuffers(sVulkan.device, &commandInfo,
                                              &sVulkan.commandBuffer), "vkAllocateCommandBuffers"))
        return false;

    VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (!VulkanCheck(vkCreateSemaphore(sVulkan.device, &semaphoreInfo, NULL,
                                       &sVulkan.imageAvailable), "vkCreateSemaphore"))
        return false;

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    return VulkanCheck(vkCreateFence(sVulkan.device, &fenceInfo, NULL, &sVulkan.frameFence),
                       "vkCreateFence");
}

bool PlatformVulkan_Init(SDL_Window *window)
{
    memset(&sVulkan, 0, sizeof(sVulkan));
    sVulkan.window = window;

    if (!CreateInstanceAndSurface()
        || !FindPhysicalDevice()
        || !CreateDevice()
        || !CreateFrameResources()
        || !CreateCommandResources()
        || !RecreateSwapchain())
    {
        PlatformVulkan_Shutdown();
        return false;
    }

    sVulkan.initialized = true;
    return true;
}

static void ConvertFrame(const uint16_t *source)
{
    uint8_t *destination = sVulkan.stagingMapping;
    for (size_t i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        uint16_t pixel = source[i];
        destination[i * 4 + 0] = ((pixel >> 0) & 0x1f) * 255 / 31;
        destination[i * 4 + 1] = ((pixel >> 5) & 0x1f) * 255 / 31;
        destination[i * 4 + 2] = ((pixel >> 10) & 0x1f) * 255 / 31;
        destination[i * 4 + 3] = 255;
    }
}

static void GetAspectFitRect(VkExtent2D extent, VkOffset3D *topLeft, VkOffset3D *bottomRight)
{
    uint32_t width = extent.width;
    uint32_t height = extent.height;
    if ((uint64_t)width * DISPLAY_HEIGHT > (uint64_t)height * DISPLAY_WIDTH)
        width = (uint32_t)((uint64_t)height * DISPLAY_WIDTH / DISPLAY_HEIGHT);
    else
        height = (uint32_t)((uint64_t)width * DISPLAY_HEIGHT / DISPLAY_WIDTH);

    uint32_t x = (extent.width - width) / 2;
    uint32_t y = (extent.height - height) / 2;
    *topLeft = (VkOffset3D){ (int32_t)x, (int32_t)y, 0 };
    *bottomRight = (VkOffset3D){ (int32_t)(x + width), (int32_t)(y + height), 1 };
}

bool PlatformVulkan_RenderFrame(const uint16_t *pixels)
{
    if (!sVulkan.initialized)
        return false;

    int drawableWidth, drawableHeight;
    SDL_Vulkan_GetDrawableSize(sVulkan.window, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0)
    {
        SDL_Delay(16);
        return true;
    }

    if (sVulkan.recreateSwapchain
        || sVulkan.swapchainExtent.width != (uint32_t)drawableWidth
        || sVulkan.swapchainExtent.height != (uint32_t)drawableHeight)
    {
        if (!RecreateSwapchain())
            return false;
    }

    if (!VulkanCheck(vkWaitForFences(sVulkan.device, 1, &sVulkan.frameFence, VK_TRUE, UINT64_MAX),
                     "vkWaitForFences"))
        return false;

    uint32_t imageIndex;
    VkResult acquire = vkAcquireNextImageKHR(sVulkan.device, sVulkan.swapchain, UINT64_MAX,
                                             sVulkan.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        sVulkan.recreateSwapchain = true;
        return true;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
        return VulkanCheck(acquire, "vkAcquireNextImageKHR");
    bool acquiredSuboptimal = acquire == VK_SUBOPTIMAL_KHR;

    ConvertFrame(pixels);
    if (!VulkanCheck(vkResetFences(sVulkan.device, 1, &sVulkan.frameFence), "vkResetFences")
        || !VulkanCheck(vkResetCommandBuffer(sVulkan.commandBuffer, 0), "vkResetCommandBuffer"))
        return false;

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!VulkanCheck(vkBeginCommandBuffer(sVulkan.commandBuffer, &beginInfo), "vkBeginCommandBuffer"))
        return false;

    VkImageMemoryBarrier frameToTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = sVulkan.frameImageInitialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = sVulkan.frameImageInitialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                  : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sVulkan.frameImage,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(sVulkan.commandBuffer,
                         sVulkan.frameImageInitialized ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                                       : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &frameToTransfer);

    VkBufferImageCopy copy = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { DISPLAY_WIDTH, DISPLAY_HEIGHT, 1 },
    };
    vkCmdCopyBufferToImage(sVulkan.commandBuffer, sVulkan.stagingBuffer, sVulkan.frameImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier frameToBlit = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sVulkan.frameImage,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(sVulkan.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &frameToBlit);

    VkImageMemoryBarrier swapToBlit = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sVulkan.swapchainImages[imageIndex],
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(sVulkan.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &swapToBlit);

    VkClearColorValue background = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
    VkImageSubresourceRange colorRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(sVulkan.commandBuffer, sVulkan.swapchainImages[imageIndex],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &background, 1, &colorRange);

    VkOffset3D sourceOffsets[2] = {
        { 0, 0, 0 },
        { DISPLAY_WIDTH, DISPLAY_HEIGHT, 1 },
    };
    VkOffset3D destinationOffsets[2];
    GetAspectFitRect(sVulkan.swapchainExtent, &destinationOffsets[0], &destinationOffsets[1]);
    VkImageBlit blit = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { sourceOffsets[0], sourceOffsets[1] },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { destinationOffsets[0], destinationOffsets[1] },
    };
    vkCmdBlitImage(sVulkan.commandBuffer, sVulkan.frameImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   sVulkan.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    VkImageMemoryBarrier swapToPresent = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sVulkan.swapchainImages[imageIndex],
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(sVulkan.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
                         &swapToPresent);
    if (!VulkanCheck(vkEndCommandBuffer(sVulkan.commandBuffer), "vkEndCommandBuffer"))
        return false;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sVulkan.imageAvailable,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &sVulkan.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sVulkan.renderFinishedSemaphores[imageIndex],
    };
    if (!VulkanCheck(vkQueueSubmit(sVulkan.queue, 1, &submit, sVulkan.frameFence), "vkQueueSubmit"))
        return false;
    sVulkan.frameImageInitialized = true;

    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sVulkan.renderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &sVulkan.swapchain,
        .pImageIndices = &imageIndex,
    };
    VkResult result = vkQueuePresentKHR(sVulkan.queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquiredSuboptimal)
        sVulkan.recreateSwapchain = true;
    else if (result != VK_SUCCESS)
        return VulkanCheck(result, "vkQueuePresentKHR");

    return true;
}

void PlatformVulkan_Shutdown(void)
{
    if (sVulkan.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(sVulkan.device);
    DestroySwapchain();
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.stagingMapping != NULL)
        vkUnmapMemory(sVulkan.device, sVulkan.stagingMemory);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.frameFence != VK_NULL_HANDLE)
        vkDestroyFence(sVulkan.device, sVulkan.frameFence, NULL);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.imageAvailable != VK_NULL_HANDLE)
        vkDestroySemaphore(sVulkan.device, sVulkan.imageAvailable, NULL);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(sVulkan.device, sVulkan.commandPool, NULL);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.stagingBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(sVulkan.device, sVulkan.stagingBuffer, NULL);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.stagingMemory != VK_NULL_HANDLE)
        vkFreeMemory(sVulkan.device, sVulkan.stagingMemory, NULL);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.frameImage != VK_NULL_HANDLE)
        vkDestroyImage(sVulkan.device, sVulkan.frameImage, NULL);
    if (sVulkan.device != VK_NULL_HANDLE && sVulkan.frameImageMemory != VK_NULL_HANDLE)
        vkFreeMemory(sVulkan.device, sVulkan.frameImageMemory, NULL);
    if (sVulkan.device != VK_NULL_HANDLE)
        vkDestroyDevice(sVulkan.device, NULL);
    if (sVulkan.instance != VK_NULL_HANDLE && sVulkan.surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(sVulkan.instance, sVulkan.surface, NULL);
    if (sVulkan.instance != VK_NULL_HANDLE)
        vkDestroyInstance(sVulkan.instance, NULL);
    memset(&sVulkan, 0, sizeof(sVulkan));
}

#endif // PLATFORM_VULKAN
