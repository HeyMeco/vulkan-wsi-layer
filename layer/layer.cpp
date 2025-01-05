/*
 * Copyright (c) 2016-2024 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cassert>
#include <cstdio>
#include <cstring>
#include <array>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include "private_data.hpp"
#include "surface_api.hpp"
#include "swapchain_api.hpp"
#include "swapchain_maintenance_api.hpp"
#include "util/extension_list.hpp"
#include "util/custom_allocator.hpp"
#include "wsi/wsi_factory.hpp"
#include "util/log.hpp"
#include "util/macros.hpp"
#include "util/helpers.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
#include "wsi_layer_experimental.hpp"
#endif

#define VK_LAYER_API_VERSION VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION)

namespace layer
{

VKAPI_ATTR VkLayerInstanceCreateInfo *get_chain_info(const VkInstanceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   auto *chain_info = reinterpret_cast<const VkLayerInstanceCreateInfo *>(pCreateInfo->pNext);
   while (chain_info &&
          !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain_info->function == func))
   {
      chain_info = reinterpret_cast<const VkLayerInstanceCreateInfo *>(chain_info->pNext);
   }

   return const_cast<VkLayerInstanceCreateInfo *>(chain_info);
}

VKAPI_ATTR VkLayerDeviceCreateInfo *get_chain_info(const VkDeviceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   auto *chain_info = reinterpret_cast<const VkLayerDeviceCreateInfo *>(pCreateInfo->pNext);
   while (chain_info &&
          !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain_info->function == func))
   {
      chain_info = reinterpret_cast<const VkLayerDeviceCreateInfo *>(chain_info->pNext);
   }

   return const_cast<VkLayerDeviceCreateInfo *>(chain_info);
}

template <typename T>
static T get_instance_proc_addr(PFN_vkGetInstanceProcAddr fp_get_instance_proc_addr, const char *name,
                                VkInstance instance = VK_NULL_HANDLE)
{
   T func = reinterpret_cast<T>(fp_get_instance_proc_addr(instance, name));
   if (func == nullptr)
   {
      WSI_LOG_WARNING("Failed to get address of %s", name);
   }

   return func;
}

template <typename T>
static T get_device_proc_addr(PFN_vkGetDeviceProcAddr fp_get_device_proc_addr, const char *name,
                              VkDevice device = VK_NULL_HANDLE)
{
   T func = reinterpret_cast<T>(fp_get_device_proc_addr(device, name));
   if (func == nullptr)
   {
      WSI_LOG_WARNING("Failed to get address of %s", name);
   }

   return func;
}

/* This is where the layer is initialised and the instance dispatch table is constructed. */
VKAPI_ATTR VkResult create_instance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                    VkInstance *pInstance)
{
   VkLayerInstanceCreateInfo *layer_link_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   VkLayerInstanceCreateInfo *loader_data_callback = get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
   if (nullptr == layer_link_info || nullptr == layer_link_info->u.pLayerInfo || nullptr == loader_data_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer in layer initialization structures during vkCreateInstance");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = layer_link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkSetInstanceLoaderData loader_callback = loader_data_callback->u.pfnSetInstanceLoaderData;
   if (nullptr == fpGetInstanceProcAddr || nullptr == loader_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer for loader callback functions during vkCreateInstance");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto fpCreateInstance = get_instance_proc_addr<PFN_vkCreateInstance>(fpGetInstanceProcAddr, "vkCreateInstance");
   if (nullptr == fpCreateInstance)
   {
      WSI_LOG_ERROR("Unexpected NULL return value from pfnNextGetInstanceProcAddr");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* For instances handled by the layer, we need to enable extra extensions, therefore take a copy of pCreateInfo. */
   VkInstanceCreateInfo modified_info = *pCreateInfo;

   /* Create a util::vector in case we need to modify the modified_info.ppEnabledExtensionNames list.
    * This object and the extension_list object need to be in the global scope so they can be alive by the time
    * vkCreateInstance is called.
    */
   util::allocator allocator{ VK_SYSTEM_ALLOCATION_SCOPE_COMMAND, pAllocator };
   util::vector<const char *> modified_enabled_extensions{ allocator };
   util::extension_list extensions{ allocator };

   /* Find all the platforms that the layer can handle based on pCreateInfo->ppEnabledExtensionNames. */
   auto layer_platforms_to_enable = wsi::find_enabled_layer_platforms(pCreateInfo);
   if (!layer_platforms_to_enable.empty())
   {
      /* Create a list of extensions to enable, including the provided extensions and those required by the layer. */
      TRY_LOG_CALL(extensions.add(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount));

      if (!extensions.contains(VK_KHR_SURFACE_EXTENSION_NAME))
      {
         return VK_ERROR_EXTENSION_NOT_PRESENT;
      }

      TRY_LOG_CALL(wsi::add_instance_extensions_required_by_layer(layer_platforms_to_enable, extensions));
      TRY_LOG_CALL(extensions.get_extension_strings(modified_enabled_extensions));

      modified_info.ppEnabledExtensionNames = modified_enabled_extensions.data();
      modified_info.enabledExtensionCount = modified_enabled_extensions.size();
   }

   /* Advance the link info for the next element on the chain. */
   layer_link_info->u.pLayerInfo = layer_link_info->u.pLayerInfo->pNext;

   /* Now call create instance on the chain further down the list.
    * Note that we do not remove the extensions that the layer supports from modified_info.ppEnabledExtensionNames.
    * Layers have to abide the rule that vkCreateInstance must not generate an error for unrecognized extension names.
    * Also, the loader filters the extension list to ensure that ICDs do not see extensions that they do not support.
    */
   TRY_LOG(fpCreateInstance(&modified_info, pAllocator, pInstance), "Failed to create the instance");
   /* Note: If the call to vkCreateInstance succeeded, the loader will do the clean-up for us
    * after this function returns with an error code. We can't call vkDestroyInstance
    * ourselves as this will cause double-free from the loader attempting to clean up after us.
    * Any failing calls below this point should NOT call vkDestroyInstance and rather just
    * return the error code. */

   /* Following the spec: use the callbacks provided to vkCreateInstance() if not nullptr,
    * otherwise use the default callbacks.
    */
   util::allocator instance_allocator{ VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE, pAllocator };
   std::optional<instance_dispatch_table> table = instance_dispatch_table::create(instance_allocator);
   if (!table.has_value())
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY_LOG_CALL(table->populate(*pInstance, fpGetInstanceProcAddr));
   table->set_user_enabled_extensions(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);

   uint32_t api_version =
      pCreateInfo->pApplicationInfo != nullptr ? pCreateInfo->pApplicationInfo->apiVersion : VK_API_VERSION_1_3;

   TRY_LOG_CALL(instance_private_data::associate(*pInstance, std::move(*table), loader_callback,
                                                 layer_platforms_to_enable, api_version, instance_allocator));

   /*
    * Store the enabled instance extensions in order to return nullptr in
    * vkGetInstanceProcAddr for functions of disabled extensions.
    */
   VkResult result =
      instance_private_data::get(*pInstance)
         .set_instance_enabled_extensions(modified_enabled_extensions.data(), modified_enabled_extensions.size());
   if (result != VK_SUCCESS)
   {
      instance_private_data::disassociate(*pInstance);
      return result;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult create_device(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VkLayerDeviceCreateInfo *layer_link_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   VkLayerDeviceCreateInfo *loader_data_callback = get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
   if (nullptr == layer_link_info || nullptr == layer_link_info->u.pLayerInfo || nullptr == loader_data_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer in layer initialization structures during vkCreateDevice");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Retrieve the vkGetDeviceProcAddr and the vkCreateDevice function pointers for the next layer in the chain. */
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = layer_link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = layer_link_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
   PFN_vkSetDeviceLoaderData loader_callback = loader_data_callback->u.pfnSetDeviceLoaderData;
   if (nullptr == fpGetInstanceProcAddr || nullptr == fpGetDeviceProcAddr || nullptr == loader_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer for loader callback functions during vkCreateDevice");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto fpCreateDevice = get_instance_proc_addr<PFN_vkCreateDevice>(fpGetInstanceProcAddr, "vkCreateDevice");
   if (nullptr == fpCreateDevice)
   {
      WSI_LOG_ERROR("Unexpected NULL return value from pfnNextGetInstanceProcAddr");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Advance the link info for the next element on the chain. */
   layer_link_info->u.pLayerInfo = layer_link_info->u.pLayerInfo->pNext;

   /* Enable extra extensions if needed by the layer, similarly to what done in vkCreateInstance. */
   VkDeviceCreateInfo modified_info = *pCreateInfo;

   auto &inst_data = instance_private_data::get(physicalDevice);
   util::allocator allocator{ inst_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND, pAllocator };
   util::vector<const char *> modified_enabled_extensions{ allocator };
   util::extension_list enabled_extensions{ allocator };

   const util::wsi_platform_set &enabled_platforms = inst_data.get_enabled_platforms();
   if (!enabled_platforms.empty())
   {
      TRY_LOG_CALL(enabled_extensions.add(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount));
      TRY_LOG_CALL(wsi::add_device_extensions_required_by_layer(physicalDevice, enabled_platforms, enabled_extensions));
      TRY_LOG_CALL(enabled_extensions.get_extension_strings(modified_enabled_extensions));

      modified_info.ppEnabledExtensionNames = modified_enabled_extensions.data();
      modified_info.enabledExtensionCount = modified_enabled_extensions.size();
   }

   bool should_layer_handle_frame_boundary_events = false;
   VkPhysicalDeviceFrameBoundaryFeaturesEXT frame_boundary;

   if (ENABLE_INSTRUMENTATION)
   {
      if (enabled_extensions.contains(VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME))
      {
         if (inst_data.has_frame_boundary_support(physicalDevice))
         {
            const auto *application_frame_boundary_features =
               util::find_extension<VkPhysicalDeviceFrameBoundaryFeaturesEXT>(
                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT, pCreateInfo->pNext);

            if (application_frame_boundary_features)
            {
               if (application_frame_boundary_features->frameBoundary == VK_FALSE)
               {
                  /* The original features cannot be modified as they are marked as constant.
                   * Additionally, it is not possible to unlink this extension from the pNext
                   * chain as all other passed structures are also marked as const. We'll take
                   * the risk to modify the original structure as there is no trivial way to
                   * re-enable frame boundary feature or swap out the original structure. */
                  auto *frame_boundary_features_non_const =
                     const_cast<VkPhysicalDeviceFrameBoundaryFeaturesEXT *>(application_frame_boundary_features);
                  frame_boundary_features_non_const->frameBoundary = VK_TRUE;
               }
            }
            else
            {
               frame_boundary.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT;
               frame_boundary.pNext = const_cast<void *>(modified_info.pNext);
               frame_boundary.frameBoundary = VK_TRUE;

               modified_info.pNext = &frame_boundary;
            }

            should_layer_handle_frame_boundary_events = true;
         }
      }
   }

   VkPhysicalDeviceFeatures enabled_features;
   if (pCreateInfo->pEnabledFeatures) {
      enabled_features = *(pCreateInfo->pEnabledFeatures);
      enabled_features.textureCompressionBC = false;
      modified_info.pEnabledFeatures = &enabled_features;
   }

   /* Now call create device on the chain further down the list. */
   TRY_LOG(fpCreateDevice(physicalDevice, &modified_info, pAllocator, pDevice), "Failed to create the device");

   auto fn_destroy_device = get_device_proc_addr<PFN_vkDestroyDevice>(fpGetDeviceProcAddr, "vkDestroyDevice", *pDevice);
   /* This should never be nullptr */
   assert(fn_destroy_device != nullptr);

   /* Following the spec: use the callbacks provided to vkCreateDevice() if not nullptr, otherwise use the callbacks
    * provided to the instance (if no allocator callbacks was provided to the instance, it will use default ones).
    */
   util::allocator device_allocator{ inst_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, pAllocator };
   std::optional<device_dispatch_table> table = device_dispatch_table::create(device_allocator);
   if (!table.has_value())
   {
      fn_destroy_device(*pDevice, pAllocator);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   VkResult result = table->populate(*pDevice, fpGetDeviceProcAddr);
   if (result != VK_SUCCESS)
   {
      fn_destroy_device(*pDevice, pAllocator);
      return result;
   }

   table->set_user_enabled_extensions(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);

   result = device_private_data::associate(*pDevice, inst_data, physicalDevice, std::move(*table), loader_callback,
                                           device_allocator);
   if (result != VK_SUCCESS)
   {
      fn_destroy_device(*pDevice, pAllocator);
      return result;
   }

   /*
    * Store the enabled device extensions in order to return nullptr in
    * vkGetDeviceProcAddr for functions of disabled extensions.
    */
   auto &device_data = layer::device_private_data::get(*pDevice);
   device_data.set_layer_frame_boundary_handling_enabled(should_layer_handle_frame_boundary_events);

   result = device_data.set_device_enabled_extensions(modified_info.ppEnabledExtensionNames,
                                                      modified_info.enabledExtensionCount);
   if (result != VK_SUCCESS)
   {
      layer::device_private_data::disassociate(*pDevice);
      fn_destroy_device(*pDevice, pAllocator);
      return result;
   }

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   const auto *swapchain_compression_feature =
      util::find_extension<VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT, pCreateInfo->pNext);
   if (swapchain_compression_feature != nullptr)
   {
      device_data.set_swapchain_compression_control_enabled(
         swapchain_compression_feature->imageCompressionControlSwapchain);
   }
#endif

   const auto present_id_features = util::find_extension<VkPhysicalDevicePresentIdFeaturesKHR>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, pCreateInfo->pNext);
   if (present_id_features != nullptr)
   {
      device_data.set_present_id_feature_enabled(present_id_features->presentId);
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   auto *physical_device_swapchain_maintenance1_features =
      util::find_extension<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, pCreateInfo->pNext);
   if (physical_device_swapchain_maintenance1_features != nullptr)
   {
      device_data.set_swapchain_maintenance1_enabled(
         physical_device_swapchain_maintenance1_features->swapchainMaintenance1);
   }
#endif

   return VK_SUCCESS;
}

} /* namespace layer */

VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetDeviceProcAddr(VkDevice device, const char *funcName) VWL_API_POST;

VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetInstanceProcAddr(VkInstance instance, const char *funcName) VWL_API_POST;

/* Clean up the dispatch table for this instance. */
VWL_VKAPI_CALL(void)
wsi_layer_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   if (instance == VK_NULL_HANDLE)
   {
      return;
   }

   auto fn_destroy_instance =
      layer::instance_private_data::get(instance).disp.get_fn<PFN_vkDestroyInstance>("vkDestroyInstance");

   /* Call disassociate() before doing vkDestroyInstance as an instance may be created by a different thread
    * just after we call vkDestroyInstance() and it could get the same address if we are unlucky.
    */
   layer::instance_private_data::disassociate(instance);

   assert(fn_destroy_instance.has_value());
   (*fn_destroy_instance)(instance, pAllocator);
}

VWL_VKAPI_CALL(void)
wsi_layer_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   if (device == VK_NULL_HANDLE)
   {
      return;
   }

   auto fn_destroy_device = layer::device_private_data::get(device).disp.get_fn<PFN_vkDestroyDevice>("vkDestroyDevice");

   /* Call disassociate() before doing vkDestroyDevice as a device may be created by a different thread
    * just after we call vkDestroyDevice().
    */
   layer::device_private_data::disassociate(device);

   assert(fn_destroy_device.has_value());
   (*fn_destroy_device)(device, pAllocator);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                           VkInstance *pInstance) VWL_API_POST
{
   return layer::create_instance(pCreateInfo, pAllocator, pInstance);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) VWL_API_POST
{
   return layer::create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VWL_VKAPI_CALL(VkResult)
VWL_VKAPI_EXPORT wsi_layer_vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct)
   VWL_API_POST
{
   assert(pVersionStruct);
   assert(pVersionStruct->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

   /* 2 is the minimum interface version which would utilize this function. */
   assert(pVersionStruct->loaderLayerInterfaceVersion >= 2);

   /* Set our requested interface version. Set to 2 for now to separate us from newer versions. */
   pVersionStruct->loaderLayerInterfaceVersion = 2;

   /* Fill in struct values. */
   pVersionStruct->pfnGetInstanceProcAddr = &wsi_layer_vkGetInstanceProcAddr;
   pVersionStruct->pfnGetDeviceProcAddr = &wsi_layer_vkGetDeviceProcAddr;
   pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(void)
wsi_layer_vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice,
                                          VkPhysicalDeviceFeatures2 *pFeatures) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);

   instance.disp.GetPhysicalDeviceFeatures2KHR(physicalDevice, pFeatures);

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   auto *image_compression_control_swapchain_features =
      util::find_extension<VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT, pFeatures->pNext);
   if (image_compression_control_swapchain_features != nullptr)
   {
      image_compression_control_swapchain_features->imageCompressionControlSwapchain =
         instance.has_image_compression_support(physicalDevice);
   }
#endif

   auto *present_id_features = util::find_extension<VkPhysicalDevicePresentIdFeaturesKHR>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, pFeatures->pNext);
   if (present_id_features != nullptr)
   {
      present_id_features->presentId = true;
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   auto *physical_device_swapchain_maintenance1_features =
      util::find_extension<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, pFeatures->pNext);
   if (physical_device_swapchain_maintenance1_features != nullptr)
   {
      physical_device_swapchain_maintenance1_features->swapchainMaintenance1 = true;
   }

   auto *present_timing_features = util::find_extension<VkPhysicalDevicePresentTimingFeaturesEXT>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT, pFeatures->pNext);
   if (present_timing_features != nullptr)
   {
      present_timing_features->presentTiming = VK_TRUE;
      present_timing_features->presentAtAbsoluteTime = VK_TRUE;
      present_timing_features->presentAtRelativeTime = VK_TRUE;
   }
#endif
}

#define GET_PROC_ADDR(func)      \
   if (!strcmp(funcName, #func)) \
      return (PFN_vkVoidFunction)&wsi_layer_##func;

VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetDeviceProcAddr(VkDevice device, const char *funcName) VWL_API_POST
{
   if (layer::device_private_data::get(device).is_device_extension_enabled(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkCreateSwapchainKHR);
      GET_PROC_ADDR(vkDestroySwapchainKHR);
      GET_PROC_ADDR(vkGetSwapchainImagesKHR);
      GET_PROC_ADDR(vkAcquireNextImageKHR);
      GET_PROC_ADDR(vkQueuePresentKHR);
      GET_PROC_ADDR(vkAcquireNextImage2KHR);
      GET_PROC_ADDR(vkGetDeviceGroupPresentCapabilitiesKHR);
      GET_PROC_ADDR(vkGetDeviceGroupSurfacePresentModesKHR);
   }
   if (layer::device_private_data::get(device).is_device_extension_enabled(
          VK_KHR_SHARED_PRESENTABLE_IMAGE_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkGetSwapchainStatusKHR);
   }
#if VULKAN_WSI_LAYER_EXPERIMENTAL
   if (layer::device_private_data::get(device).is_device_extension_enabled(VK_EXT_PRESENT_TIMING_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkSetSwapchainPresentTimingQueueSizeEXT);
      GET_PROC_ADDR(vkGetSwapchainTimingPropertiesEXT);
      GET_PROC_ADDR(vkGetSwapchainTimeDomainPropertiesEXT);
      GET_PROC_ADDR(vkGetPastPresentationTimingEXT);
   }
#endif
   GET_PROC_ADDR(vkDestroyDevice);

   GET_PROC_ADDR(vkCreateImage);
   GET_PROC_ADDR(vkBindImageMemory2);

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   /* VK_EXT_swapchain_maintenance1 */
   if (layer::device_private_data::get(device).is_device_extension_enabled(
          VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkReleaseSwapchainImagesEXT);
   }
#endif

   return layer::device_private_data::get(device).disp.get_user_enabled_entrypoint(
      device, layer::device_private_data::get(device).instance_data.api_version, funcName);
}

VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetInstanceProcAddr(VkInstance instance, const char *funcName) VWL_API_POST
{
   GET_PROC_ADDR(vkGetDeviceProcAddr);
   GET_PROC_ADDR(vkGetInstanceProcAddr);
   GET_PROC_ADDR(vkCreateInstance);
   GET_PROC_ADDR(vkDestroyInstance);
   GET_PROC_ADDR(vkCreateDevice);
   GET_PROC_ADDR(vkGetPhysicalDevicePresentRectanglesKHR);

   if (!strcmp(funcName, "vkGetPhysicalDeviceFeatures2"))
   {
      return (PFN_vkVoidFunction)wsi_layer_vkGetPhysicalDeviceFeatures2KHR;
   }

   auto &instance_data = layer::instance_private_data::get(instance);

   if (instance_data.is_instance_extension_enabled(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkGetPhysicalDeviceFeatures2KHR);
   }

   if (instance_data.is_instance_extension_enabled(VK_KHR_SURFACE_EXTENSION_NAME))
   {
      PFN_vkVoidFunction wsi_func = wsi::get_proc_addr(funcName, instance_data);
      if (wsi_func)
      {
         return wsi_func;
      }

      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceSupportKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceFormatsKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfacePresentModesKHR);
      GET_PROC_ADDR(vkDestroySurfaceKHR);

      if (instance_data.is_instance_extension_enabled(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
      {
         GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
         GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceFormats2KHR);
      }
   }

   return instance_data.disp.get_user_enabled_entrypoint(instance, instance_data.api_version, funcName);
}
