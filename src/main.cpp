#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h> //if we want to print out enum names
#define GLFW_INCLUDE_NONE //"no, do not include OpenGL"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>

#include "../include/libs/stb/stb_image.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

//macro function to check the return value of Vulkan API calls. Print an error message and aborts the program if it fails
#define VK_CHECK(call)                                                  \
    do {                                                                \
        VkResult vk_check_result = (call);                              \
        if (vk_check_result != VK_SUCCESS) {                            \
            std::fprintf(stderr, "%s:%d: %s failed with VkResult %d\n", \
                        __FILE__, __LINE__, #call, vk_check_result);    \
            std::abort();                                               \
        }                                                               \
    } while (0)

// static const uint32_t WINDOW_WIDTH = 800;
// static const uint32_t WINDOW_HEIGHT = 800;
static const uint32_t WINDOW_WIDTH = 1920;
static const uint32_t WINDOW_HEIGHT = 1080;
static const uint32_t FRAME_WIDTH = WINDOW_WIDTH;
static const uint32_t FRAME_HEIGHT = WINDOW_HEIGHT;
static const uint32_t FRAMES_IN_FLIGHT = 3;
// static const char* COMPUTE_SHADER_PATH = SHADER_DIR "/myshader.comp.spv";
static const char* COMPUTE_SHADER_PATH = SHADER_DIR "/blackhole.comp.spv";

//validation layers pass debug messages to this function
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                           VkDebugUtilsMessageTypeFlagsEXT message_types,
                                           const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                           void* user_data)
{
    (void)message_severity;
    (void)message_types;
    (void)user_data;

    std::fprintf(stderr, "[validation] %s\n", callback_data->pMessage);

    static std::ofstream log_file("validation.txt", std::ios::out | std::ios::trunc);

    if (log_file.is_open()) {
        log_file << "[validation] " << callback_data->pMessage << "\n";
        log_file << std::flush;
    }

    return VK_FALSE;
}

//set up our validation.txt file
static std::ofstream& ValidationLog()
{
    static std::ofstream log_file("validation.txt", std::ios::out | std::ios::trunc);
    return log_file;
}

//check if our physical device supports a certain device extension
static bool DeviceSupportsExtension(VkPhysicalDevice physical_device, const char* extension_name)
{
    uint32_t extension_count = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr));

    std::vector<VkExtensionProperties> extensions(extension_count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, extensions.data()));

    for (uint32_t i = 0; i < extension_count; ++i) {
        if (std::strcmp(extensions[i].extensionName, extension_name) == 0) {
            return true;
        }
    }

    return false;
}

//check if our Vulkan instance supports a certain instance extension
static bool InstanceSupportsExtension(const char* extension_name)
{
    uint32_t extension_count = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr));

    std::vector<VkExtensionProperties> extensions(extension_count);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()));

    for (uint32_t i = 0; i < extension_count; ++i) {
        if (std::strcmp(extensions[i].extensionName, extension_name) == 0) {
            return true;
        }
    }

    return false;
}

//check if a certain bit index is set in a mask
static bool BitIsSet(uint32_t mask, uint32_t bit_index)
{
    return (mask & (1u << bit_index)) != 0;
}

//helper function to interpret shader code as a uint32 vec
static std::vector<uint32_t> ReadSpirv(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        std::fprintf(stderr, "failed to open shader file: %s\n", std::filesystem::absolute(path).string().c_str());
        std::abort();
    }

    size_t size_in_bytes = (size_t)file.tellg();
    std::vector<uint32_t> code(size_in_bytes / sizeof(uint32_t));

    file.seekg(0);
    file.read((char*)code.data(), size_in_bytes);

    return code;
}

int main()
{
    //--------------------------------------------------------------------------------------//
    // WINDOW CREATION
    //--------------------------------------------------------------------------------------//

    //initialize glfw
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE; //EXIT_FAILURE resolves to 1
    }

    //disables OpenGL context creation
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    const char* window_title = "P3DG-PROJECT1";

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, window_title, nullptr, nullptr);

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        return EXIT_FAILURE;
    }

    ValidationLog() << "=== VALIDATION LOG: " << window_title << " ===\n" << std::flush;
    std::fprintf(stderr, "[info] validation log location: %s\n", std::filesystem::absolute("validation.txt").string().c_str());

    //--------------------------------------------------------------------------------------//
    // INSTANCE CREATION
    //--------------------------------------------------------------------------------------//

    //glfw needs certain vulkan instance extensions to be enabled to perform core functions like creating a window surface. we query for them here
    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!glfw_extensions) {
        std::fprintf(stderr, "glfwGetRequiredInstanceExtensions failed (Vulkan not supported by GLFW on this platform)\n");
        return EXIT_FAILURE;
    }

    std::vector<const char*> instance_extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

    //let's add the debug extension so that our debug messanger works
    instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    //enable validation layers that create debug messages
    const char* enabled_layers[] = { "VK_LAYER_KHRONOS_validation" };

    //describe our debug messanger
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = nullptr,
    };

    //let's enable synchronization validation to make sure our stage mask targets are correct
    instance_extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);

    VkBool32 validate_sync_enabled = VK_TRUE;

    VkLayerSettingEXT layer_settings[] = {
        {
            .pLayerName = "VK_LAYER_KHRONOS_validation",
            .pSettingName = "validate_sync",
            .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
            .valueCount = 1,
            .pValues = &validate_sync_enabled,
        },
    };

    VkLayerSettingsCreateInfoEXT layer_settings_create_info = {
        .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext = &debug_messenger_create_info,
        .settingCount = sizeof(layer_settings) / sizeof(layer_settings[0]),
        .pSettings = layer_settings,
    };

    VkInstanceCreateFlags instance_create_flags = 0;
    //mac support
    if (InstanceSupportsExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    //describe our application
    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = window_title,
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "none",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),

        .apiVersion = VK_API_VERSION_1_3,
    };

    //describe our Vulkan instance
    VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &layer_settings_create_info,
        .flags = instance_create_flags,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = sizeof(enabled_layers) / sizeof(enabled_layers[0]),
        .ppEnabledLayerNames = enabled_layers,
        .enabledExtensionCount = (uint32_t)instance_extensions.size(),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    //create our Vulkan instance
    VkInstance instance = VK_NULL_HANDLE;
    VkResult instance_creation_result = vkCreateInstance(&instance_create_info, nullptr, &instance);
    VK_CHECK(instance_creation_result);

    //since extension functions are not loaded in by default (after all, they are extensions), we have to query the function as a pointer in order to use it
    PFN_vkCreateDebugUtilsMessengerEXT pfn_create_debug_utils_messengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");

    //establish the debug messanger callback
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VK_CHECK(pfn_create_debug_utils_messengerEXT(instance, &debug_messenger_create_info, nullptr, &debug_messenger));

    //--------------------------------------------------------------------------------------//
    // SURFACE CREATION
    //--------------------------------------------------------------------------------------//
    
    //create the surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    //--------------------------------------------------------------------------------------//
    // PHYSICAL DEVICE SELECTION
    //--------------------------------------------------------------------------------------//

    //simply check the number of available physical devices (GPUs), don't get their data yet
    uint32_t physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
    if (physical_device_count == 0) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices failed (no Vulkan-capable devices found)\n");
        return EXIT_FAILURE;
    }
    
    //properly get the physical device data, look through them and find the best* one
    //"best" meaning preferably a dedicated GPU (as opposed to an integrated one on the CPU), and minimally must support version 1.3 of Vulkan 
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t queue_family_index = UINT32_MAX;

    for (uint32_t i = 0; i < physical_device_count; ++i) {
        VkPhysicalDeviceProperties physical_device_properties = {};
        vkGetPhysicalDeviceProperties(physical_devices[i], &physical_device_properties);

        if (physical_device_properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        //devices have one or more queue families that support different pipelines
        //to keep it simple, let's grab a family that supports both the graphics pipeline and glfw surfaces 
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, queue_families.data());

        for (uint32_t j = 0; j < queue_family_count; ++j) {
            if (!(queue_families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }

            VkBool32 present_surface_support = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present_surface_support));

            if (present_surface_support == VK_TRUE) {
                queue_family_index = j;
                break;
            }
        }

        if (queue_family_index == UINT32_MAX) {
            continue;
        }

        if (physical_device != VK_NULL_HANDLE && physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            continue;
        }

        physical_device = physical_devices[i];

        if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            break;
        }
    }

    if (physical_device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "No device supporting Vulkan 1.3 + present\n");
        return EXIT_FAILURE;
    }

    //--------------------------------------------------------------------------------------//
    // LOGICAL DEVICE & QUEUE CREATION
    //--------------------------------------------------------------------------------------//

    //queue priority is useful for > 1 queue but still necessary for == 1, ranges from 0.0-1.0
    //(it's used for prioritizing queues when scheduling)
    float queue_priority = 1.f;

    //describe our queue
    VkDeviceQueueCreateInfo device_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    //note that simply being on vulkan 1.3 doesn't automatically turn 1.3 features on.
    //they default to VK_FALSE, so we enable them here and pass it to the logical device creation info
    VkPhysicalDeviceVulkan13Features vulkan_1_3_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = NULL,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    //presenting a surface (and by extension, using a swapchain) is not core Vulkan but rather a device extension
    std::vector<const char*> device_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    //mac support
    if (DeviceSupportsExtension(physical_device, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    //describe our logical device that serves as the interface between our Vulkan instance and our physical device (GPU)
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan_1_3_features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_create_info,
        .enabledExtensionCount = (uint32_t)device_extensions.size(),
        .ppEnabledExtensionNames = device_extensions.data(),
    };

    //create our logical device and its queues 
    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));

    //retrieve the handle of the queue generated with the logical device
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_index = 0;
    vkGetDeviceQueue(device, queue_family_index, queue_index, &queue);

    //--------------------------------------------------------------------------------------//
    // SWAPCHAIN CREATION
    //--------------------------------------------------------------------------------------//

    //let's check if our physical device supports the transfer op we're going to use later
    //(don't worry, it practically always is)
    VkSurfaceCapabilitiesKHR surface_capabilities = {};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities));

    //VK_IMAGE_USAGE_TRANSFER_DST_BIT is the flag that signifies the image can be used as the destination for a transfer op
    if (!(surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        std::fprintf(stderr, "Surface does not support VK_IMAGE_USAGE_TRANSFER_DST_BIT\n");
        return EXIT_FAILURE;
    }

    //query the number of supported surface (image) formats first before fetching them
    uint32_t surface_format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, nullptr));

    //...then fetch them
    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, surface_formats.data()));

    //for our purposes, this is the format and color space we want. in case it isn't there, we grab the first one
    //at least one format is guaranteed to exist
    VkSurfaceFormatKHR surface_format = surface_formats[0];
    for (uint32_t i = 0; i < surface_format_count; ++i) {
        if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = surface_formats[i];
            break;
        }
    }

    //device will support up to a specific surface size, or any size (UINT32_MAX). 
    //since we presumably get to choose, let's just use the framebuffer dimensions
    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;
    if (swapchain_extent.width == UINT32_MAX) {
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        swapchain_extent.width = (uint32_t)framebuffer_width;
        swapchain_extent.height = (uint32_t)framebuffer_height;
    }

    //minImageCount can be as low as 1...
    //let's make sure there are at least two images to use in our swapchain to "swap" betweeen
    uint32_t swapchain_min_image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0 && swapchain_min_image_count > surface_capabilities.maxImageCount) {
        swapchain_min_image_count = surface_capabilities.maxImageCount;
    }

    //describe our swapchain
    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = swapchain_min_image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = swapchain_extent,
        //more than 1 only used for stereoscopic rendering
        .imageArrayLayers = 1,
        //transfer dst is here bc we clear via a transfer command
        //color attachment won't be used until we have a fully-fledged pipeline
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        //FIFO is guaranteed to be supported
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, // VK_PRESENT_MODE_IMMEDIATE_KHR for fast frames
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    //create our swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain));

    //query number of images in the swapchain we just made
    uint32_t swapchain_image_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr));
    
    //grab the images the swapchain made - we do not generate them ourselves
    std::vector<VkImage> swapchain_images(swapchain_image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data()));

    //--------------------------------------------------------------------------------------//
    // COMMAND POOL CREATION & COMMAND BUFFER ALLOCATION 
    //--------------------------------------------------------------------------------------//

    //describe our command pool
    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        //this flag enables vkResetCommandBuffer which we need because we are re-recording the command buffers on each frame
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        //a command pool is bound to (& effectively owned by) one queue family.
        //its buffers can only be submitted to queues in that family
        .queueFamilyIndex = queue_family_index,
    };

    //create our command pool
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool));

    //describe the buffer(s) we are going to create
    VkCommandBufferAllocateInfo command_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FRAMES_IN_FLIGHT,
    };

    //allocate (create) command buffers for the pool we've made
    std::vector<VkCommandBuffer> command_buffers(FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_allocate_info, command_buffers.data()));

    //--------------------------------------------------------------------------------------//
    // IMAGE ALLOCATION
    //--------------------------------------------------------------------------------------//

    // REMEMBER VULKAN ORDER IS POS X NEG X POS Y NEG Y POS Z NEG Z
    stbi_set_flip_vertically_on_load(true);

    char* texture_data[6];
    int cubemap_width, cubemap_height, channels;

    // TODO: fix weird paths
    texture_data[0] = (char*)stbi_load("../../skybox/px.png", &cubemap_width, &cubemap_height, &channels, STBI_rgb_alpha);
    texture_data[1] = (char*)stbi_load("../../skybox/nx.png", &cubemap_width, &cubemap_height, &channels, STBI_rgb_alpha);
    texture_data[2] = (char*)stbi_load("../../skybox/ny.png", &cubemap_width, &cubemap_height, &channels, STBI_rgb_alpha); // In vulkan +Y is flipped
    texture_data[3] = (char*)stbi_load("../../skybox/py.png", &cubemap_width, &cubemap_height, &channels, STBI_rgb_alpha);
    texture_data[4] = (char*)stbi_load("../../skybox/pz.png", &cubemap_width, &cubemap_height, &channels, STBI_rgb_alpha);
    texture_data[5] = (char*)stbi_load("../../skybox/nz.png", &cubemap_width, &cubemap_height, &channels, STBI_rgb_alpha);

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = FRAME_WIDTH,
            .height = FRAME_HEIGHT,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,//SRC -> blit, STORAGE -> img view
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImageCreateInfo cubemap_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = (uint32_t)cubemap_width,
            .height = (uint32_t)cubemap_height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 6,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    std::vector<VkImage> frame(FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateImage(device, &image_info, nullptr, &frame[i])); //creates the VkImage handle (equivalent to VkBuffer)
    }

    VkImage cubemap;
    VK_CHECK(vkCreateImage(device, &cubemap_info, nullptr, &cubemap));

    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    // Frame
    VkMemoryRequirements memory_requirements = {};
    vkGetImageMemoryRequirements(device, frame[0], &memory_requirements);
    VkMemoryPropertyFlags property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    uint32_t memory_type_index = UINT32_MAX;
    uint32_t allowed_memory_type_bits = memory_requirements.memoryTypeBits;

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        bool type_allowed = BitIsSet(allowed_memory_type_bits, i);
        bool has_properties = (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags;

        if (type_allowed && has_properties) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        std::fprintf(stderr, "No host-visible coherent memory type available\n");
        return EXIT_FAILURE;
    }

    VkDeviceSize stride = ((memory_requirements.size + memory_requirements.alignment - 1) / memory_requirements.alignment) * memory_requirements.alignment;

    VkDeviceSize frame_size = stride * FRAMES_IN_FLIGHT;

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = frame_size,
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory frame_memory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &frame_memory));

    std::vector<VkImageView> frame_view(FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VkDeviceSize bind_offset = i * stride;
        VK_CHECK(vkBindImageMemory(device, frame[i], frame_memory, bind_offset)); //binds VkImage handle to VkMemory block

        //to create the image view...
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = frame[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &frame_view[i])); //creates the VkImageView handle
    }
    
    // Cubemap
    vkGetImageMemoryRequirements(device, cubemap, &memory_requirements);
    property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    memory_type_index = UINT32_MAX;
    allowed_memory_type_bits = memory_requirements.memoryTypeBits;

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        bool type_allowed = BitIsSet(allowed_memory_type_bits, i);
        bool has_properties = (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags;

        if (type_allowed && has_properties) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        std::fprintf(stderr, "No host-visible coherent memory type available\n");
        return EXIT_FAILURE;
    }

    stride = ((memory_requirements.size + memory_requirements.alignment - 1) / memory_requirements.alignment) * memory_requirements.alignment;

    VkDeviceSize cubemap_size = memory_requirements.size;

    VkMemoryAllocateInfo alloc_info2 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = cubemap_size,
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory cubemap_memory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device, &alloc_info2, nullptr, &cubemap_memory));

    VkImageView cubemap_view;

    VK_CHECK(vkBindImageMemory(device, cubemap, cubemap_memory, 0)); //binds VkImage handle to VkMemory block

    //to create the image view...
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = cubemap,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6,
        },
    };

    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &cubemap_view)); //creates the VkImageView handle

    // Load Cubemap
    VkDeviceSize face_size = cubemap_width * cubemap_height * 4;
    VkDeviceSize staging_buffer_size = face_size * 6;

    VkBufferCreateInfo staging_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .size = staging_buffer_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkBuffer staging_buffer;
    VK_CHECK(vkCreateBuffer(device, &staging_buffer_info, nullptr, &staging_buffer));

    VkMemoryRequirements staging_buffer_mem_reqs;
    vkGetBufferMemoryRequirements(device, staging_buffer, &staging_buffer_mem_reqs);

    property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    memory_type_index = UINT32_MAX;
    allowed_memory_type_bits = staging_buffer_mem_reqs.memoryTypeBits;

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        bool type_allowed = BitIsSet(allowed_memory_type_bits, i);
        bool has_properties = (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags;

        if (type_allowed && has_properties) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        std::fprintf(stderr, "No host-visible coherent memory type available\n");
        return EXIT_FAILURE;
    }

    VkMemoryAllocateInfo staging_buffer_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = staging_buffer_mem_reqs.size,
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory staging_buffer_memory;
    VK_CHECK(vkAllocateMemory(device, &staging_buffer_alloc_info, nullptr, &staging_buffer_memory));
    VK_CHECK(vkBindBufferMemory(device, staging_buffer, staging_buffer_memory, 0));

    void* cubemap_data;
    VK_CHECK(vkMapMemory(device, staging_buffer_memory, 0, staging_buffer_size, 0, &cubemap_data));

    for(int i = 0; i < 6; ++i) {
        std::memcpy(static_cast<char*>(cubemap_data) + face_size * i, texture_data[i], face_size);
    }

    vkUnmapMemory(device, staging_buffer_memory);

    // Resources
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };

    VkSampler cubemap_sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &cubemap_sampler));

    std::vector<VkDescriptorPoolSize> descriptor_pool_sizes(2); 

    descriptor_pool_sizes[0] = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = FRAMES_IN_FLIGHT,
    };

    descriptor_pool_sizes[1] = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = FRAMES_IN_FLIGHT,
    };

    VkDescriptorPoolCreateInfo descriptor_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .maxSets = FRAMES_IN_FLIGHT,
        .poolSizeCount = 2,
        .pPoolSizes = descriptor_pool_sizes.data(),
    };

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptor_pool_info, NULL, &descriptor_pool));

    std::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings(2);

    descriptor_set_layout_bindings[0] = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    descriptor_set_layout_bindings[1] = {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .bindingCount = 2,
        .pBindings = descriptor_set_layout_bindings.data(),
    };

    std::vector<VkDescriptorSetLayout> descriptor_set_layout(FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {  
        VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_set_layout_info, NULL, &descriptor_set_layout[i]));
    }

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = FRAMES_IN_FLIGHT,
        .pSetLayouts = descriptor_set_layout.data(),
    };

    std::vector<VkDescriptorSet> descriptor_set(FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, descriptor_set.data()));

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorImageInfo descriptor_image_info = {
            .sampler = VK_NULL_HANDLE,
            .imageView = frame_view[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet write_descriptor_set = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = NULL,
            .dstSet = descriptor_set[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descriptor_image_info,
        };

        VkDescriptorImageInfo cubemap_descriptor_info = {
            .sampler = cubemap_sampler,
            .imageView = cubemap_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet cubemap_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = NULL,
            .dstSet = descriptor_set[i],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &cubemap_descriptor_info,
        };

        vkUpdateDescriptorSets(device, 1, &write_descriptor_set, 0, nullptr);
        vkUpdateDescriptorSets(device, 1, &cubemap_write, 0, nullptr);
    }

    // Camera Parameters
    float FOV = 600.0;
    glm::vec4 right  = {1.0, 0.0, 0.0, 0.0};
    glm::vec4 up     = {0.0, 1.0, 0.0, 0.0};
    glm::vec4 lookAt = {0.0, 0.0, -1.0, 0.0};
    glm::vec4 eye    = {0.0, 0.0, 50.0, 1.0};

    glm::mat4 camera = {right, up, lookAt, eye};

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = (sizeof(int) * 2 + sizeof(float) * 2 + sizeof(glm::mat4)),
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = FRAMES_IN_FLIGHT,
        .pSetLayouts = descriptor_set_layout.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout));

    std::vector<uint32_t> compute_code = ReadSpirv(COMPUTE_SHADER_PATH);

    VkShaderModuleCreateInfo shader_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .codeSize = compute_code.size() * sizeof(uint32_t),
        .pCode = compute_code.data(),
    };

    VkShaderModule compute_shader_module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &shader_module_info, nullptr, &compute_shader_module));

    VkPipelineShaderStageCreateInfo pipeline_shader_stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = compute_shader_module,
        .pName = "main",
    };

    VkComputePipelineCreateInfo compute_pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = pipeline_shader_stage_info,
        .layout = pipeline_layout,
    };

    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute_pipeline_info, NULL, &compute_pipeline));

    //--------------------------------------------------------------------------------------//
    // SYNCHRONIZATION OBJECT CREATION
    //--------------------------------------------------------------------------------------//

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_SEMAPHORE_TYPE_BINARY,
    };

    VkFenceCreateInfo command_buffer_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    std::vector<VkSemaphore> image_available_semaphore(FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> render_finished_semaphore(swapchain_image_count);
    std::vector<VkFence> in_flight_fence(FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &image_available_semaphore[i]));
        VK_CHECK(vkCreateFence(device, &command_buffer_fence_info, nullptr, &in_flight_fence[i]));
    }

    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &render_finished_semaphore[i]));
    }

    //--------------------------------------------------------------------------------------//
    // FRAME LOOP
    //--------------------------------------------------------------------------------------//

    // Stuff I gotta do before render loop

    VkCommandBufferBeginInfo staging_command_begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkCommandBuffer staging_command_buffer = command_buffers[0];

    VK_CHECK(vkResetCommandBuffer(staging_command_buffer, 0));

    VK_CHECK(vkBeginCommandBuffer(staging_command_buffer, &staging_command_begin_info));

    VkImageSubresourceRange cubemap_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 6,
    };

    VkImageMemoryBarrier2 barrier_to_transfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,

        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = cubemap,
        .subresourceRange = cubemap_range,
    };

    VkDependencyInfo dependency_to_transfer = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_to_transfer,
    };

    vkCmdPipelineBarrier2(staging_command_buffer, &dependency_to_transfer);

    VkImageSubresourceRange subresource_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    std::vector<VkImageMemoryBarrier2> barrier_storage_to_transfer_dsts(FRAMES_IN_FLIGHT);
    for(uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        barrier_storage_to_transfer_dsts[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,

            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = frame[i],
            .subresourceRange = subresource_range,
        };
    }

    VkDependencyInfo dependency_to_transfer2 = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = FRAMES_IN_FLIGHT,
        .pImageMemoryBarriers = barrier_storage_to_transfer_dsts.data(),
    };

    vkCmdPipelineBarrier2(staging_command_buffer, &dependency_to_transfer2);

    std::vector<VkBufferImageCopy> staging_buffer_copies(6);
    for(uint32_t i = 0; i < 6; ++i) {
        staging_buffer_copies[i] = {
            .bufferOffset = face_size * i,
            .bufferRowLength = 0, //tightly packed
            .bufferImageHeight = 0, //tightly packed
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = i,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {
                .width = (uint32_t)cubemap_width,
                .height = (uint32_t)cubemap_height,
                .depth = 1,
            },
        };
    }

    vkCmdCopyBufferToImage(staging_command_buffer, staging_buffer, cubemap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, staging_buffer_copies.data());

    VkImageMemoryBarrier2 barrier_to_shader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,

        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = cubemap,
        .subresourceRange = cubemap_range,
    };

    VkDependencyInfo dependency_to_shader = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_to_shader,
    };

    vkCmdPipelineBarrier2(staging_command_buffer, &dependency_to_shader);

    VK_CHECK(vkEndCommandBuffer(staging_command_buffer));

    VkCommandBufferSubmitInfo command_buffer_submit_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = NULL,
        .commandBuffer = staging_command_buffer,
        .deviceMask = 0,
    };

    VkSubmitInfo2 staging_submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = NULL,
        .flags = 0,
        .waitSemaphoreInfoCount = 0,
        .pWaitSemaphoreInfos = nullptr,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &command_buffer_submit_info,
        .signalSemaphoreInfoCount = 0,
        .pSignalSemaphoreInfos = nullptr,
    };

    VK_CHECK(vkQueueSubmit2(queue, 1, &staging_submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(device));

    vkDestroyBuffer(device, staging_buffer, nullptr);
    vkFreeMemory(device, staging_buffer_memory, nullptr);

    // END

    uint32_t current_frame = 0;
    uint64_t total_frames = 0;
    std::chrono::milliseconds start_ms = duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::chrono::milliseconds curr_ms, end_ms;
    std::chrono::milliseconds last_frame_ms = start_ms;
    std::chrono::milliseconds delta_ms = start_ms;

    // INPUT SETTING CONSTANTS
    const float SHIFT = 30.0;
    const float THETA = glm::radians(90.0f);

    // float pitch = 0.0f, yaw = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        vkWaitForFences(device, 1, &in_flight_fence[current_frame], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &in_flight_fence[current_frame]);

        uint32_t image_index = 0;
        VkResult acquire_result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore[current_frame], VK_NULL_HANDLE, &image_index);

        if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "vkAcquireNextImageKHR failed with VkResult %d\n", acquire_result);
            return EXIT_FAILURE;
        }

        float delta = delta_ms.count() / (float)1000;
        
        // Handle Inputs
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            camera = glm::translate(camera, glm::vec3(0.0, 0.0, SHIFT) * delta);
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            camera = glm::translate(camera, glm::vec3(0.0, 0.0, -SHIFT) * delta);
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            camera = glm::translate(camera, glm::vec3(-SHIFT, 0.0, 0.0) * delta);
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            camera = glm::translate(camera, glm::vec3(SHIFT, 0.0, 0.0) * delta);
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            camera = glm::translate(camera, glm::vec3(0, -SHIFT, 0.0) * delta);
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            camera = glm::translate(camera, glm::vec3(0, SHIFT, 0.0) * delta);
        }
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            camera = glm::rotate(camera, THETA * delta, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
            camera = glm::rotate(camera, -THETA * delta, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            camera = glm::rotate(camera, THETA * delta, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            camera = glm::rotate(camera, -THETA * delta, glm::vec3(0.0f, 1.0f, 0.0f));
        }

        VK_CHECK(vkResetCommandBuffer(command_buffers[current_frame], 0));

        VkCommandBufferBeginInfo command_buffer_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        VK_CHECK(vkBeginCommandBuffer(command_buffers[current_frame], &command_buffer_begin_info));

        struct PushConstants {
            int camera_width;
            int camera_height;
            float FOV;
            float time;
            glm::mat4 camera;
        };

        PushConstants pc = {(int)FRAME_WIDTH, (int)FRAME_HEIGHT, FOV, (curr_ms - start_ms).count() / 1.0e3f, camera};

        vkCmdBindPipeline(command_buffers[current_frame], VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
        vkCmdBindDescriptorSets(command_buffers[current_frame], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set[current_frame], 0, nullptr);
        vkCmdPushConstants(command_buffers[current_frame], pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDispatch(command_buffers[current_frame], (FRAME_WIDTH + 15) / 16, (FRAME_HEIGHT + 15) / 16, 1);

        VkImageMemoryBarrier2 barrier_storage_to_transfer_src = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            
            .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = frame[current_frame],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barrier_to_swapchain_transfer_dst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,

            .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            
            .image = swapchain_images[image_index],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barriers_before_blit[2] = {barrier_storage_to_transfer_src, barrier_to_swapchain_transfer_dst};

        VkDependencyInfo dependency_info_before_blit = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = barriers_before_blit,
        };

        vkCmdPipelineBarrier2(command_buffers[current_frame], &dependency_info_before_blit);

        VkImageBlit image_blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcOffsets = { {0, 0, 0}, {FRAME_WIDTH, FRAME_HEIGHT, 1} },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .dstOffsets = { {0, 0, 0}, {(int32_t)swapchain_extent.width, (int32_t)swapchain_extent.height, 1} },
        };

        vkCmdBlitImage(command_buffers[current_frame], frame[current_frame], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_blit, VK_FILTER_LINEAR);

        VkImageMemoryBarrier2 barrier_to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,

            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = swapchain_images[image_index],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barrier_to_general = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
 
            .image = frame[current_frame],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barriers_after_blit[2] = {barrier_to_present, barrier_to_general};

        VkDependencyInfo dependency_info_to_present = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = barriers_after_blit,
        };

        vkCmdPipelineBarrier2(command_buffers[current_frame], &dependency_info_to_present);

        VK_CHECK(vkEndCommandBuffer(command_buffers[current_frame]));

        VkSemaphoreSubmitInfo wait_semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = NULL,
            .semaphore = image_available_semaphore[current_frame],
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .deviceIndex = 0,
        };

        VkCommandBufferSubmitInfo command_buffer_submit_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = NULL,
            .commandBuffer = command_buffers[current_frame],
            .deviceMask = 0,
        };

        VkSemaphoreSubmitInfo signal_semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = NULL,
            .semaphore = render_finished_semaphore[image_index],
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        };

        VkSubmitInfo2 submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = NULL,
            .flags = 0,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &wait_semaphore_info,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command_buffer_submit_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signal_semaphore_info,
        };

        VK_CHECK(vkQueueSubmit2(queue, 1, &submit_info, in_flight_fence[current_frame]));

        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished_semaphore[image_index],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };

        VkResult present_result = vkQueuePresentKHR(queue, &present_info);
        if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "vkQueuePresentKHR failed with VkResult %d\n", present_result);
            return EXIT_FAILURE;
        }

        curr_ms = duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        delta_ms = curr_ms - last_frame_ms;
        last_frame_ms = curr_ms;
        current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
        total_frames++;
    }

    //let's wait for everything to finish on the host side before cleaning up or finalizing timers
    VK_CHECK(vkDeviceWaitIdle(device));

    end_ms = duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    delta_ms = end_ms - start_ms;

    double fps = (double)total_frames / (delta_ms.count() / 1000.0);
    std::printf("Avg FPS: %i\n", int(fps));

    //destruction generally goes in reverse order of creation

    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        vkDestroySemaphore(device, render_finished_semaphore[i], nullptr);
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device, image_available_semaphore[i], nullptr);
        vkDestroyFence(device, in_flight_fence[i], nullptr);
    }

    vkDestroyPipeline(device, compute_pipeline, nullptr);
    
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);

    vkDestroyShaderModule(device, compute_shader_module, nullptr);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout[i], nullptr);
    }

    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

    vkDestroySampler(device, cubemap_sampler, nullptr);

    vkDestroyCommandPool(device, command_pool, nullptr);

    vkDestroyImageView(device, cubemap_view, nullptr);
    vkDestroyImage(device, cubemap, nullptr);

    vkFreeMemory(device, cubemap_memory, nullptr);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(device, frame_view[i], nullptr);
        vkDestroyImage(device, frame[i], nullptr);
    }

    vkFreeMemory(device, frame_memory, nullptr);

    vkDestroySwapchainKHR(device, swapchain, nullptr);

    vkDestroyDevice(device, nullptr);

    vkDestroySurfaceKHR(instance, surface, nullptr);

    PFN_vkDestroyDebugUtilsMessengerEXT pfn_destroy_debug_utils_messengerEXT =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    pfn_destroy_debug_utils_messengerEXT(instance, debug_messenger, nullptr);

    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}