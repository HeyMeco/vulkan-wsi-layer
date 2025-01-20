/*
 * Copyright (c) 2019, 2021, 2023-2025 Arm Limited.
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

/**
 * @file
 * @brief Contains the factory methods for obtaining the specific surface and swapchain implementations.
 */

#pragma once

#include "swapchain_base.hpp"
#include "surface_properties.hpp"
#include "util/platform_set.hpp"

#include <unordered_map>

namespace wsi
{

/**
 * @brief Obtains the surface properties for the specific surface type.
 *
 * @param instance_data The instance specific data.
 * @param surface       The surface for which to get the properties.
 *
 * @return nullptr if surface type is unsupported.
 */
surface_properties *get_surface_properties(layer::instance_private_data &instance_data, VkSurfaceKHR surface);

/**
 * @brief Allocates a surface specific swapchain.
 *
 * @param surface    The surface for which a swapchain is allocated.
 * @param dev_data   The device specific data.
 * @param pAllocator The allocator from which to allocate any memory.
 *
 * @return nullptr on failure.
 */
util::unique_ptr<swapchain_base> allocate_surface_swapchain(VkSurfaceKHR surface, layer::device_private_data &dev_data,
                                                            const VkAllocationCallbacks *pAllocator);

/**
 * @brief Destroys a swapchain and frees memory. Used with @ref allocate_surface_swapchain.
 *
 * @param swapchain  Pointer to the swapchain to destroy.
 * @param dev_data   The device specific data.
 * @param pAllocator The allocator to use for freeing memory.
 */
void destroy_surface_swapchain(swapchain_base *swapchain, layer::device_private_data &dev_data,
                               const VkAllocationCallbacks *pAllocator);

/**
 * @brief Return which platforms the layer can handle for an instance constructed in the specified way.
 *
 * @details This function looks at the extensions specified in @p pCreateInfo and based on this returns a list of
 * platforms that the layer can support. For example, if the @c pCreateInfo.ppEnabledExtensionNames contains the string
 * "VK_EXT_headless_surface" then the returned platform set will contain @c VK_ICD_WSI_PLATFORM_HEADLESS.
 *
 * @param pCreateInfo Structure used when creating the instance in vkCreateInstance().
 *
 * @return A list of WS platforms supported by the layer.
 */
util::wsi_platform_set find_enabled_layer_platforms(const VkInstanceCreateInfo *pCreateInfo);

/**
 * @brief Add extra extensions that the layer requires to support the specified list of enabled platforms.
 *
 * @details Check whether @p phys_dev has support for the extensions required by the layer in order to support the
 * platforms it implements. The extensions that the layer requires to operate are added to @p extensions_to_enable.
 *
 * @param[in] phys_dev The physical device to check.
 * @param[in] enabled_platforms All the platforms that the layer must enable for @p phys_dev.
 * @param[in,out] extensions_to_enable All the extensions required by the layer are added to this list.
 *
 * @retval @c VK_SUCCESS if the operation was successful.
 */
VkResult add_device_extensions_required_by_layer(VkPhysicalDevice phys_dev,
                                                 const util::wsi_platform_set enabled_platforms,
                                                 util::extension_list &extensions_to_enable);

/**
 * @brief Add required instance extensions by the layer.
 *
 * @param[in] enabled_platforms All the enabled platforms for the current instance.
 * @param[in,out] extensions_to_enable All the extensions required by the layer are added to this list.
 *
 * @retval @c VK_SUCCESS if the operation was successful.
 */
VkResult add_instance_extensions_required_by_layer(const util::wsi_platform_set enabled_platforms,
                                                   util::extension_list &extensions_to_enable);

/**
 * @brief Return a function pointer for surface specific functions.
 *
 * @details This function iterates through the supported platforms and queries them for the
 * implementation of the @p name function.
 *
 * @param name The name of the target function
 * @param instance_data The instance specific data.
 *
 * @return A pointer to the implementation of the @p name function or null pointer in case this
 *         function isn't implemented for any platform.
 */
PFN_vkVoidFunction get_proc_addr(const char *name, const layer::instance_private_data &instance_data);

/**
 * @brief Set swapchain maintenance1 features state, true or false.
 *
 * Sets the state of the swapchain maintenance1 extensions
 * according to the supported enabled extensions.
 *
 * @param physical_device                   Vulkan physical_device.
 * @param swapchain_maintenance_features    address of Vulkan swapchain maintenance capabilities struct.
 *
 */
void set_swapchain_maintenance1_state(
   VkPhysicalDevice physicalDevice, VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *swapchain_maintenance1_features);

} // namespace wsi
