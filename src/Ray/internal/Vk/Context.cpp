#include "Context.h"

#include <regex>

#include "../../Log.h"
#include "../../Types.h"
#include "../SmallVector.h"
#include "DescriptorPool.h"
#include "MemoryAllocator.h"

#include "../../third-party/renderdoc/renderdoc_app.h"

namespace Ray {
namespace Vk {
bool ignore_optick_errors = false;

VKAPI_ATTR VkBool32 VKAPI_CALL DebugReportCallback(const VkDebugReportFlagsEXT flags,
                                                   const VkDebugReportObjectTypeEXT objectType, const uint64_t object,
                                                   const size_t location, const int32_t messageCode,
                                                   const char *pLayerPrefix, const char *pMessage, void *pUserData) {
    auto *ctx = reinterpret_cast<const Context *>(pUserData);

    bool ignore = ignore_optick_errors && (location == 0x45e90123 || location == 0xffffffff9cacd67a);
    ignore |= (location == 0x0000000079de34d4); // dynamic rendering support is incomplete
    ignore |= (location == 0x000000004dae5635); // layout warning when blitting within the same image
    if (!ignore) {
        ctx->log()->Error("%s: %s\n", pLayerPrefix, pMessage);
    }
    return VK_FALSE;
}

const std::pair<uint32_t, const char *> KnownVendors[] = {
    {0x1002, "AMD"}, {0x10DE, "NVIDIA"}, {0x8086, "INTEL"}, {0x13B5, "ARM"}};

RENDERDOC_DevicePointer rdoc_device = {};

#ifndef NDEBUG
const char *g_enabled_layers[] = {"VK_LAYER_KHRONOS_validation", "VK_LAYER_KHRONOS_synchronization2"};
const int g_enabled_layers_count = COUNT_OF(g_enabled_layers);
#else
const char **g_enabled_layers = nullptr;
const int g_enabled_layers_count = 0;
#endif
} // namespace Vk
} // namespace Ray

Ray::Vk::Context::~Context() { Destroy(); }

void Ray::Vk::Context::Destroy() {
    if (device_) {
        vkDeviceWaitIdle(device_);

        for (int i = 0; i < MaxFramesInFlight; ++i) {
            backend_frame = i; // default_descr_alloc_'s destructors rely on this
            default_descr_alloc_[i].reset();
            DestroyDeferredResources(i);

            vkDestroyFence(device_, in_flight_fences_[i], nullptr);
            vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
            vkDestroySemaphore(device_, image_avail_semaphores_[i], nullptr);

            vkDestroyQueryPool(device_, query_pools_[i], nullptr);
        }

        default_memory_allocs_.reset();

        vkFreeCommandBuffers(device_, command_pool_, 1, &setup_cmd_buf_);
        vkFreeCommandBuffers(device_, command_pool_, MaxFramesInFlight, draw_cmd_bufs_);

        // for (int i = 0; i < StageBufferCount; ++i) {
        //     default_stage_bufs_.fences[i].ClientWaitSync();
        //     default_stage_bufs_.fences[i] = {};
        //     default_stage_bufs_.bufs[i] = {};
        // }

        vkDestroyCommandPool(device_, command_pool_, nullptr);
        vkDestroyCommandPool(device_, temp_command_pool_, nullptr);

        // for (size_t i = 0; i < api_ctx_->present_image_views.size(); ++i) {
        //     vkDestroyImageView(api_ctx_->device, api_ctx_->present_image_views[i], nullptr);
        // }

        // vkDestroySwapchainKHR(device_, api_ctx_->swapchain, nullptr);

        vkDestroyDevice(device_, nullptr);
        // vkDestroySurfaceKHR(instance_, surface_, nullptr);
#ifndef NDEBUG
        vkDestroyDebugReportCallbackEXT(instance_, debug_callback_, nullptr);
#endif

        vkDestroyInstance(instance_, nullptr);
    }
}

bool Ray::Vk::Context::Init(ILog *log, const char *preferred_device) {
    log_ = log;

    if (!LoadVulkan(log)) {
        return false;
    }

    if (!InitVkInstance(instance_, g_enabled_layers, g_enabled_layers_count, log)) {
        return false;
    }

#ifndef NDEBUG
    { // Sebug debug report callback
        VkDebugReportCallbackCreateInfoEXT callback_create_info = {VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT};
        callback_create_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                     VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        callback_create_info.pfnCallback = DebugReportCallback;
        callback_create_info.pUserData = this;

        const VkResult res =
            vkCreateDebugReportCallbackEXT(instance_, &callback_create_info, nullptr, &debug_callback_);
        if (res != VK_SUCCESS) {
            log->Error("Failed to create debug report callback");
            return false;
        }
    }
#endif

    if (!ChooseVkPhysicalDevice(physical_device_, device_properties_, mem_properties_, graphics_family_index_,
                                raytracing_supported_, ray_query_supported_, dynamic_rendering_supported_,
                                preferred_device, instance_, log)) {
        return false;
    }

    // Disable as it is not needed for now
    dynamic_rendering_supported_ = false;

    if (!InitVkDevice(device_, physical_device_, graphics_family_index_, raytracing_supported_, ray_query_supported_,
                      dynamic_rendering_supported_, g_enabled_layers, g_enabled_layers_count, log)) {
        return false;
    }

    if (!InitCommandBuffers(command_pool_, temp_command_pool_, setup_cmd_buf_, draw_cmd_bufs_, image_avail_semaphores_,
                            render_finished_semaphores_, in_flight_fences_, query_pools_, graphics_queue_, device_,
                            graphics_family_index_, log)) {
        return false;
    }

    log_->Info("===========================================");
    log_->Info("Device info:");

    log_->Info("\tVulkan version\t: %i.%i", VK_API_VERSION_MAJOR(device_properties_.apiVersion),
               VK_API_VERSION_MINOR(device_properties_.apiVersion));

    auto it = find_if(std::begin(KnownVendors), std::end(KnownVendors),
                      [this](std::pair<uint32_t, const char *> v) { return device_properties_.vendorID == v.first; });
    if (it != std::end(KnownVendors)) {
        log_->Info("\tVendor\t\t: %s", it->second);
    }
    log_->Info("\tName\t\t: %s", device_properties_.deviceName);

    log_->Info("===========================================");

    VkPhysicalDeviceProperties device_properties = {};
    vkGetPhysicalDeviceProperties(physical_device_, &device_properties);

    phys_device_limits_ = device_properties.limits;
    max_combined_image_samplers_ = std::min(std::min(device_properties.limits.maxPerStageDescriptorSampledImages,
                                                     device_properties.limits.maxPerStageDescriptorSamplers) -
                                                10,
                                            16384u);

    { // check if 3-component images are supported
        VkImageFormatProperties props;
        const VkResult res = vkGetPhysicalDeviceImageFormatProperties(
            physical_device_, VK_FORMAT_R8G8B8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT), 0,
            &props);

        VkFormatProperties format_properties;
        vkGetPhysicalDeviceFormatProperties(physical_device_, VK_FORMAT_R8G8B8_UNORM, &format_properties);

        rgb8_unorm_is_supported_ =
            (res == VK_SUCCESS) && (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
    }

    default_memory_allocs_.reset(new MemoryAllocators("Default Allocs", this, 32 * 1024 * 1024 /* initial_block_size */,
                                                      1.5f /* growth_factor */));

    for (int i = 0; i < MaxFramesInFlight; ++i) {
        const int PoolStep = 4;
        const int MaxImgSamplerCount = 32;
        const int MaxStoreImgCount = 6;
        const int MaxUBufCount = 8;
        const int MaxSBufCount = 20;
        const int MaxTBufCount = 16;
        const int MaxAccCount = 1;
        const int InitialSetsCount = 16;

        default_descr_alloc_[i].reset(new DescrMultiPoolAlloc(this, PoolStep, MaxImgSamplerCount, MaxStoreImgCount,
                                                              MaxUBufCount, MaxSBufCount, MaxTBufCount, MaxAccCount,
                                                              InitialSetsCount));
    }

    rdoc_device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance_);

    return true;
}

bool Ray::Vk::Context::InitVkInstance(VkInstance &instance, const char *enabled_layers[],
                                      const int enabled_layers_count, ILog *log) {
    { // Find validation layer
        uint32_t layers_count = 0;
        vkEnumerateInstanceLayerProperties(&layers_count, nullptr);

        if (!layers_count) {
            log->Error("Failed to find any layer in your system");
            return false;
        }

        SmallVector<VkLayerProperties, 16> layers_available(layers_count);
        vkEnumerateInstanceLayerProperties(&layers_count, &layers_available[0]);

#ifndef NDEBUG
        bool found_validation = false;
        for (uint32_t i = 0; i < layers_count; i++) {
            if (strcmp(layers_available[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                found_validation = true;
            }
        }

        if (!found_validation) {
            log->Error("Could not find validation layer");
            return false;
        }
#endif
    }

    const char *desired_extensions[] = {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#ifndef NDEBUG
                                        VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME
#endif
    };
    const uint32_t number_required_extensions = 0;
    const uint32_t number_optional_extensions = COUNT_OF(desired_extensions) - number_required_extensions;

    { // Find required extensions
        uint32_t ext_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);

        SmallVector<VkExtensionProperties, 16> extensions_available(ext_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, &extensions_available[0]);

        uint32_t found_required_extensions = 0;
        for (uint32_t i = 0; i < ext_count; i++) {
            for (uint32_t j = 0; j < number_required_extensions; j++) {
                if (strcmp(extensions_available[i].extensionName, desired_extensions[j]) == 0) {
                    found_required_extensions++;
                }
            }
        }
        if (found_required_extensions != number_required_extensions) {
            log->Error("Not all required extensions were found!");
            log->Error("\tRequested:");
            for (int i = 0; i < number_required_extensions; ++i) {
                log->Error("\t\t%s", desired_extensions[i]);
            }
            log->Error("\tFound:");
            for (uint32_t i = 0; i < ext_count; i++) {
                for (uint32_t j = 0; j < number_required_extensions; j++) {
                    if (strcmp(extensions_available[i].extensionName, desired_extensions[j]) == 0) {
                        log->Error("\t\t%s", desired_extensions[i]);
                    }
                }
            }
            return false;
        }
    }

    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "Dummy";
    app_info.engineVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledLayerCount = enabled_layers_count;
    instance_info.ppEnabledLayerNames = enabled_layers;
    instance_info.enabledExtensionCount = number_required_extensions + number_optional_extensions;
    instance_info.ppEnabledExtensionNames = desired_extensions;

#ifndef NDEBUG
    const VkValidationFeatureEnableEXT enabled_validation_features[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT, VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
        // VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
        //  VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
    };

    VkValidationFeaturesEXT validation_features = {VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validation_features.enabledValidationFeatureCount = COUNT_OF(enabled_validation_features);
    validation_features.pEnabledValidationFeatures = enabled_validation_features;

    instance_info.pNext = &validation_features;
#endif

    const VkResult res = vkCreateInstance(&instance_info, nullptr, &instance);
    if (res != VK_SUCCESS) {
        log->Error("Failed to create vulkan instance");
        return false;
    }

    LoadVulkanExtensions(instance, log);

    return true;
}

bool Ray::Vk::Context::ChooseVkPhysicalDevice(VkPhysicalDevice &physical_device,
                                              VkPhysicalDeviceProperties &out_device_properties,
                                              VkPhysicalDeviceMemoryProperties &out_mem_properties,
                                              uint32_t &out_graphics_family_index, bool &out_raytracing_supported,
                                              bool &out_ray_query_supported, bool &out_dynamic_rendering_supported,
                                              const char *preferred_device, VkInstance instance, ILog *log) {
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);

    SmallVector<VkPhysicalDevice, 4> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count, &physical_devices[0]);

    int best_score = 0;

    for (uint32_t i = 0; i < physical_device_count; i++) {
        VkPhysicalDeviceProperties device_properties = {};
        vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);

        bool acc_struct_supported = false, raytracing_supported = false, ray_query_supported = false,
             dynamic_rendering_supported = false;

        { // check for features support
            uint32_t extension_count;
            vkEnumerateDeviceExtensionProperties(physical_devices[i], nullptr, &extension_count, nullptr);

            SmallVector<VkExtensionProperties, 16> available_extensions(extension_count);
            vkEnumerateDeviceExtensionProperties(physical_devices[i], nullptr, &extension_count,
                                                 &available_extensions[0]);

            for (uint32_t j = 0; j < extension_count; j++) {
                const VkExtensionProperties &ext = available_extensions[j];

                if (strcmp(ext.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0) {
                    acc_struct_supported = true;
                } else if (strcmp(ext.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0) {
                    raytracing_supported = true;
                } else if (strcmp(ext.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0) {
                    ray_query_supported = true;
                } else if (strcmp(ext.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0) {
                    dynamic_rendering_supported = true;
                }
            }
        }

        uint32_t queue_family_count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, nullptr);

        SmallVector<VkQueueFamilyProperties, 8> queue_family_properties(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, &queue_family_properties[0]);

        uint32_t graphics_family_index = 0xffffffff;

        for (uint32_t j = 0; j < queue_family_count; j++) {
            if (queue_family_properties[j].queueCount > 0 &&
                queue_family_properties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphics_family_index = j;
                break;
            } else if (queue_family_properties[j].queueCount > 0 &&
                       (queue_family_properties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       graphics_family_index == 0xffffffff) {
                graphics_family_index = j;
            }
        }

        if (graphics_family_index != 0xffffffff) {
            int score = 0;

            if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score += 1000;
            }

            score += int(device_properties.limits.maxImageDimension2D);

            if (acc_struct_supported && raytracing_supported) {
                score += 500;
            }

            if (dynamic_rendering_supported) {
                score += 100;
            }

            if (preferred_device) {
                std::regex match_name(preferred_device);
                if (strcmp(device_properties.deviceName, preferred_device) == 0 ||
                    std::regex_search(device_properties.deviceName, match_name)) {
                    // preferred device found
                    score += 100000;
                }
            }

            if (score > best_score) {
                best_score = score;

                physical_device = physical_devices[i];
                out_device_properties = device_properties;
                out_graphics_family_index = graphics_family_index;
                out_raytracing_supported = (acc_struct_supported && raytracing_supported);
                out_ray_query_supported = ray_query_supported;
                out_dynamic_rendering_supported = dynamic_rendering_supported;
            }
        }
    }

    if (!physical_device) {
        log->Error("No physical device detected that can render and present!");
        return false;
    }

    vkGetPhysicalDeviceMemoryProperties(physical_device, &out_mem_properties);

    return true;
}

bool Ray::Vk::Context::InitVkDevice(VkDevice &device, VkPhysicalDevice physical_device, uint32_t graphics_family_index,
                                    bool enable_raytracing, bool enable_ray_query, bool enable_dynamic_rendering,
                                    const char *enabled_layers[], int enabled_layers_count, ILog *log) {
    VkDeviceQueueCreateInfo queue_create_infos[2] = {{}, {}};
    const float queue_priorities[] = {1.0f};

    { // graphics queue
        queue_create_infos[0] = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_create_infos[0].queueFamilyIndex = graphics_family_index;
        queue_create_infos[0].queueCount = 1;

        queue_create_infos[0].pQueuePriorities = queue_priorities;
    }
    int infos_count = 1;

    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = infos_count;
    device_info.pQueueCreateInfos = queue_create_infos;
    device_info.enabledLayerCount = enabled_layers_count;
    device_info.ppEnabledLayerNames = enabled_layers;

    SmallVector<const char *, 16> device_extensions;

    device_extensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
    // device_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    device_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

#if defined(VK_USE_PLATFORM_MACOS_MVK)
    device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    if (enable_raytracing) {
        device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        if (enable_ray_query) {
            device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        }
    }

    if (enable_dynamic_rendering) {
        device_extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME); // required for dynamic rendering
        device_extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);   // required for depth stencil resolve
    }

    device_info.enabledExtensionCount = uint32_t(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.cdata();

    VkPhysicalDeviceFeatures features = {};
    features.shaderClipDistance = VK_TRUE;
    features.samplerAnisotropy = VK_TRUE;
    features.imageCubeArray = VK_TRUE;
    features.fillModeNonSolid = VK_TRUE;
    device_info.pEnabledFeatures = &features;
    void **pp_next = const_cast<void **>(&device_info.pNext);

    VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexing_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT};
    indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    indexing_features.descriptorBindingPartiallyBound = VK_TRUE;
    indexing_features.runtimeDescriptorArray = VK_TRUE;
    (*pp_next) = &indexing_features;
    pp_next = &indexing_features.pNext;

    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR device_address_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR};
    device_address_features.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    rt_pipeline_features.rayTracingPipeline = VK_TRUE;
    rt_pipeline_features.rayTracingPipelineTraceRaysIndirect = VK_TRUE;

    VkPhysicalDeviceRayQueryFeaturesKHR rt_query_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rt_query_features.rayQuery = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR acc_struct_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    acc_struct_features.accelerationStructure = VK_TRUE;

    if (enable_raytracing) {
        (*pp_next) = &device_address_features;
        pp_next = &device_address_features.pNext;

        (*pp_next) = &rt_pipeline_features;
        pp_next = &rt_pipeline_features.pNext;

        (*pp_next) = &acc_struct_features;
        pp_next = &acc_struct_features.pNext;

        if (enable_ray_query) {
            (*pp_next) = &rt_query_features;
            pp_next = &rt_query_features.pNext;
        }
    }

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dynamic_rendering_features.dynamicRendering = VK_TRUE;

    if (enable_dynamic_rendering) {
        (*pp_next) = &dynamic_rendering_features;
        pp_next = &dynamic_rendering_features.pNext;
    }

#if defined(VK_USE_PLATFORM_MACOS_MVK)
    VkPhysicalDevicePortabilitySubsetFeaturesKHR subset_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
    subset_features.mutableComparisonSamplers = VK_TRUE;
    (*pp_next) = &subset_features;
    pp_next = &subset_features.pNext;
#endif

    const VkResult res = vkCreateDevice(physical_device, &device_info, nullptr, &device);
    if (res != VK_SUCCESS) {
        log->Error("Failed to create logical device!");
        return false;
    }

    return true;
}

bool Ray::Vk::Context::InitCommandBuffers(VkCommandPool &command_pool, VkCommandPool &temp_command_pool,
                                          VkCommandBuffer &setup_cmd_buf,
                                          VkCommandBuffer draw_cmd_bufs[MaxFramesInFlight],
                                          VkSemaphore image_avail_semaphores[MaxFramesInFlight],
                                          VkSemaphore render_finished_semaphores[MaxFramesInFlight],
                                          VkFence in_flight_fences[MaxFramesInFlight],
                                          VkQueryPool query_pools[MaxFramesInFlight], VkQueue &graphics_queue,
                                          VkDevice device, uint32_t graphics_family_index, ILog *log) {
    vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

    VkCommandPoolCreateInfo cmd_pool_create_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmd_pool_create_info.queueFamilyIndex = graphics_family_index;

    VkResult res = vkCreateCommandPool(device, &cmd_pool_create_info, nullptr, &command_pool);
    if (res != VK_SUCCESS) {
        log->Error("Failed to create command pool!");
        return false;
    }

    { // create pool for temporary commands
        VkCommandPoolCreateInfo tmp_cmd_pool_create_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        tmp_cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        tmp_cmd_pool_create_info.queueFamilyIndex = graphics_family_index;

        res = vkCreateCommandPool(device, &tmp_cmd_pool_create_info, nullptr, &temp_command_pool);
        if (res != VK_SUCCESS) {
            log->Error("Failed to create command pool!");
            return false;
        }
    }

    VkCommandBufferAllocateInfo cmd_buf_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_buf_alloc_info.commandPool = command_pool;
    cmd_buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buf_alloc_info.commandBufferCount = 1;

    res = vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, &setup_cmd_buf);
    if (res != VK_SUCCESS) {
        log->Error("Failed to create command buffer!");
        return false;
    }

    cmd_buf_alloc_info.commandBufferCount = MaxFramesInFlight;
    res = vkAllocateCommandBuffers(device, &cmd_buf_alloc_info, draw_cmd_bufs);
    if (res != VK_SUCCESS) {
        log->Error("Failed to create command buffer!");
        return false;
    }

    { // create fences
        VkSemaphoreCreateInfo sem_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MaxFramesInFlight; i++) {
            res = vkCreateSemaphore(device, &sem_info, nullptr, &image_avail_semaphores[i]);
            if (res != VK_SUCCESS) {
                log->Error("Failed to create semaphore!");
                return false;
            }
            res = vkCreateSemaphore(device, &sem_info, nullptr, &render_finished_semaphores[i]);
            if (res != VK_SUCCESS) {
                log->Error("Failed to create semaphore!");
                return false;
            }
            res = vkCreateFence(device, &fence_info, nullptr, &in_flight_fences[i]);
            if (res != VK_SUCCESS) {
                log->Error("Failed to create fence!");
                return false;
            }
        }
    }

    { // create query pools
        VkQueryPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        pool_info.queryCount = MaxTimestampQueries;
        pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;

        for (int i = 0; i < MaxFramesInFlight; ++i) {
            res = vkCreateQueryPool(device, &pool_info, nullptr, &query_pools[i]);
            if (res != VK_SUCCESS) {
                log->Error("Failed to create query pool!");
                return false;
            }
        }
    }

    return true;
}

VkCommandBuffer Ray::Vk::BegSingleTimeCommands(VkDevice device, VkCommandPool temp_command_pool) {
    VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = temp_command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buf = {};
    vkAllocateCommandBuffers(device, &alloc_info, &command_buf);

    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(command_buf, &begin_info);
    return command_buf;
}

void Ray::Vk::EndSingleTimeCommands(VkDevice device, VkQueue cmd_queue, VkCommandBuffer command_buf,
                                    VkCommandPool temp_command_pool) {
    vkEndCommandBuffer(command_buf);

    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buf;

    vkQueueSubmit(cmd_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(cmd_queue);

    vkFreeCommandBuffers(device, temp_command_pool, 1, &command_buf);
}

int Ray::Vk::Context::WriteTimestamp(const bool start) {
    VkCommandBuffer cmd_buf = draw_cmd_bufs_[backend_frame];

    vkCmdWriteTimestamp(cmd_buf, start ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        query_pools_[backend_frame], query_counts_[backend_frame]);

    const uint32_t query_index = query_counts_[backend_frame]++;
    assert(query_counts_[backend_frame] < MaxTimestampQueries);
    return int(query_index);
}

uint64_t Ray::Vk::Context::GetTimestampIntervalDurationUs(const int query_beg, const int query_end) const {
    return (query_results_[backend_frame][query_end] - query_results_[backend_frame][query_beg]) / 1000;
}

bool Ray::Vk::Context::ReadbackTimestampQueries(const int i) {
    VkQueryPool query_pool = query_pools_[i];
    const uint32_t query_count = uint32_t(query_counts_[i]);
    if (!query_count) {
        // nothing to readback
        return true;
    }

    const VkResult res =
        vkGetQueryPoolResults(device_, query_pool, 0, query_count, query_count * sizeof(uint64_t), query_results_[i],
                              sizeof(uint64_t), VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_64_BIT);
    query_counts_[i] = 0;

    return (res == VK_SUCCESS);
}

void Ray::Vk::Context::DestroyDeferredResources(const int i) {
    for (VkImageView view : image_views_to_destroy[i]) {
        vkDestroyImageView(device_, view, nullptr);
    }
    image_views_to_destroy[i].clear();
    for (VkImage img : images_to_destroy[i]) {
        vkDestroyImage(device_, img, nullptr);
    }
    images_to_destroy[i].clear();
    for (VkSampler sampler : samplers_to_destroy[i]) {
        vkDestroySampler(device_, sampler, nullptr);
    }
    samplers_to_destroy[i].clear();

    allocs_to_free[i].clear();

    for (VkBufferView view : buf_views_to_destroy[i]) {
        vkDestroyBufferView(device_, view, nullptr);
    }
    buf_views_to_destroy[i].clear();
    for (VkBuffer buf : bufs_to_destroy[i]) {
        vkDestroyBuffer(device_, buf, nullptr);
    }
    bufs_to_destroy[i].clear();

    for (VkDeviceMemory mem : mem_to_free[i]) {
        vkFreeMemory(device_, mem, nullptr);
    }
    mem_to_free[i].clear();

    for (VkRenderPass rp : render_passes_to_destroy[i]) {
        vkDestroyRenderPass(device_, rp, nullptr);
    }
    render_passes_to_destroy[i].clear();

    for (VkFramebuffer fb : framebuffers_to_destroy[i]) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_to_destroy[i].clear();

    for (VkDescriptorPool pool : descriptor_pools_to_destroy[i]) {
        vkDestroyDescriptorPool(device_, pool, nullptr);
    }
    descriptor_pools_to_destroy[i].clear();

    for (VkPipelineLayout pipe_layout : pipeline_layouts_to_destroy[i]) {
        vkDestroyPipelineLayout(device_, pipe_layout, nullptr);
    }
    pipeline_layouts_to_destroy[i].clear();

    for (VkPipeline pipe : pipelines_to_destroy[i]) {
        vkDestroyPipeline(device_, pipe, nullptr);
    }
    pipelines_to_destroy[i].clear();

    for (VkAccelerationStructureKHR acc_struct : acc_structs_to_destroy[i]) {
        vkDestroyAccelerationStructureKHR(device_, acc_struct, nullptr);
    }
    acc_structs_to_destroy[i].clear();
}

int Ray::Vk::Context::QueryAvailableDevices(ILog *log, gpu_device_t out_devices[], const int capacity) {
    if (!LoadVulkan(log)) {
        log->Error("Failed to initialize vulkan!");
        return 0;
    }

    VkInstance instance;
    if (!InitVkInstance(instance, g_enabled_layers, g_enabled_layers_count, log)) {
        log->Error("Failed to initialize VkInstance!");
        return 0;
    }

    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);

    SmallVector<VkPhysicalDevice, 4> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count, &physical_devices[0]);

    if (out_devices) {
        if (int(physical_device_count) > capacity) {
            log->Warning("Insufficiend devices copacity");
            physical_device_count = capacity;
        }

        for (int i = 0; i < int(physical_device_count); ++i) {
            VkPhysicalDeviceProperties device_properties = {};
            vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);

            strncpy(out_devices[i].name, device_properties.deviceName, sizeof(out_devices[i].name));
        }
    }
    vkDestroyInstance(instance, nullptr);

    return int(physical_device_count);
}