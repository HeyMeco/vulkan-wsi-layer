# How to compile for use with libmali-rockchip

1. Run `apt update` and then use `armbian-config` to install Kernel Headers
   <br>Alternative to armbian-config is to just install the headers via `apt install linux-headers-vendor-rk35xx`
2. Install cmake dependency to compile
```
apt install cmake -y
```
4. Clone this repo and run the following inside the vulkan-wsi-layer folder:
```
cmake . -DCMAKE_BUILD_TYPE=Release -DBUILD_WSI_HEADLESS=0 -DBUILD_WSI_WAYLAND=1 -DBUILD_WSI_X11=0 -DSELECT_EXTERNAL_ALLOCATOR=dma_buf_heaps -DKERNEL_HEADER_DIR="/usr/src/linux-headers-6.1.84-vendor-rk35xx" -DWSIALLOC_MEMORY_HEAP_NAME=cma
```

5. Follow the [Installation](https://github.com/HeyMeco/vulkan-wsi-layer/tree/main?tab=readme-ov-file#installation) section from below

---
# Vulkan® Window System Integration Layer

## Introduction

This project is a Vulkan® layer which implements some of the Vulkan® window system
integration extensions such as `VK_KHR_swapchain`. The layer is designed to be
GPU vendor agnostic when used as part of the Vulkan® ICD/loader architecture.

Our vision for the project is to become the de facto implementation for Vulkan®
window system integration extensions so that they need not be implemented in the
ICD; instead, the implementation of these extensions are shared across vendors
for mutual benefit.

The project currently implements support for `VK_EXT_headless_surface` and
its dependencies. Experimental support for `VK_KHR_wayland_surface` can be
enabled via a build option [as explained below](#building-with-wayland-support).

### Implemented Vulkan® extensions

The Vulkan® WSI Layer in addition to the window system integration extensions
implements the following extensions:
* Instance extensions
  * VK_KHR_get_surface_capabilities2
* Device extensions
  * VK_KHR_shared_presentable_image
  * VK_EXT_image_compression_control_swapchain
  * VK_KHR_present_id

## Building

### Dependencies

* [CMake](https://cmake.org) version 3.4.3 or above.
* C++17 compiler.
* Vulkan® loader and associated headers with support for the
  `VK_EXT_headless_surface` extension and for the Vulkan 1.1, or later API.

The Vulkan WSI Layer uses Vulkan extensions to communicate with the Vulkan ICDs.
The ICDs installed in the system are required to support the following extensions:
* Instance extensions:
  * VK_KHR_get_physical_device_properties_2
  * VK_KHR_external_fence_capabilities
  * VK_KHR_external_semaphore_capabilities
  * VK_KHR_external_memory_capabilities
* Device extensions (only required when Wayland support is enabled):
  * VK_EXT_image_drm_format_modifier
  * VK_KHR_image_format_list
  * VK_EXT_external_memory_dma_buf
  * VK_KHR_external_memory_fd
  * VK_KHR_external_fence_fd
* Any dependencies of the above extensions

### Vulkan Header Version

The Vulkan WSI Layer has been validated against Vulkan header version 1.4.299.

If you are using a Vulkan header version newer than this, a warning will appear during compilation.

### Building the Vulkan® loader

This step is not necessary if your system already has a loader and associated
headers with support for the `VK_EXT_headless_surface` extension. We include
these instructions for completeness.

```
git clone https://github.com/KhronosGroup/Vulkan-Loader.git
mkdir Vulkan-Loader/build
cd Vulkan-Loader/build
../scripts/update_deps.py
cmake -C helper.cmake ..
make
make install
```

### Building with headless support

The layer requires a version of the loader and headers that includes support for
the `VK_EXT_headless_surface` extension. By default, the build system will use
the system Vulkan® headers as reported by `pkg-config`. This may be overriden by
specifying `VULKAN_CXX_INCLUDE` in the CMake configuration, for example:

```
cmake . -DVULKAN_CXX_INCLUDE="path/to/vulkan-headers"
```

If the loader and associated headers already meet the requirements of the layer
then the build steps are straightforward:

```
cmake . -Bbuild
make -C build
```

### Building with Wayland support

In order to build with Wayland support the `BUILD_WSI_WAYLAND` build option
must be used, the `SELECT_EXTERNAL_ALLOCATOR` option has to be set to
a graphics memory allocator (currently only ion and dma_buf_heaps are supported) and
the `KERNEL_HEADER_DIR` option must be defined as the directory that includes the kernel headers.
source.

```
cmake . -DVULKAN_CXX_INCLUDE="path/to/vulkan-header" \
        -DBUILD_WSI_HEADLESS=0 \
        -DBUILD_WSI_WAYLAND=1 \
        -DSELECT_EXTERNAL_ALLOCATOR=ion \
        -DKERNEL_HEADER_DIR="path/to/linux-kernel-headers"
```

In the command line above, `-DBUILD_WSI_HEADLESS=0` is used to disable support
for `VK_EXT_headless_surface`, which is otherwise enabled by default.

Note that a custom graphics memory allocator implementation can be provided
using the `EXTERNAL_WSIALLOC_LIBRARY` option. For example,

```
cmake . -DVULKAN_CXX_INCLUDE="path/to/vulkan-header" \
        -DBUILD_WSI_WAYLAND=1 \
        -DEXTERNAL_WSIALLOC_LIBRARY="path/to/custom/libwsialloc" \
        -DKERNEL_HEADER_DIR="path/to/linux-kernel-headers"
```

The `EXTERNAL_WSIALLOC_LIBRARY` option allows to specify the path to a library
containing the implementation of the graphics memory allocator API, as
described in [the wsialloc.h header file](util/wsialloc/wsialloc.h).
The allocator is not only responsible for allocating graphics buffers, but is
also responsible for selecting a suitable format that can be
efficiently shared between the different devices in the system, e.g. GPU,
display. It is therefore an important point of integration. It is expected
that each system will need a tailored implementation, although the layer
provides a generic ion and dma_buf_heaps implementations that may work in
systems that support linear formats. This is selected by
the `-DSELECT_EXTERNAL_ALLOCATOR=ion` option, as shown above.

### Wayland support with FIFO presentation mode

The WSI Layer has 2 FIFO implementations for the Wayland backend. One that
blocks in vkQueuePresent and one that uses a presentation thread. This is due
to the fact that the FIFO implementation that utilises the presentation thread
in the Wayland backend is not strictly conformant to the Vulkan specification,
however it has a much better performance due to not needing to block in vkQueuePresent.

By default, the WSI Layer uses the queue present blocking FIFO implementation
when using Wayland swapchains. This can be switched to instead use the presentation
thread implementation by including the build option `ENABLE_WAYLAND_FIFO_PRESENTATION_THREAD`,
along with the other build options mentioned in "Building with Wayland support"
section.

### Building with frame instrumentation support

The layer can be built to pass frame boundary information down to other
layers or ICD by making use of the [VK_EXT_frame_boundary extension](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_frame_boundary.html).

By enabling this feature, if application is not making use of the
VK_EXT_frame_boundary extension, the layer will generate and pass down
frame boundary events which enables the ability to instrument present submissions
for applications that do not make use of this extension.

In order to enable this feature `-DENABLE_INSTRUMENTATION=1` option can
be passed at build time.

## Installation

Copy the shared library `libVkLayer_window_system_integration.so` and JSON
configuration `VkLayer_window_system_integration.json` into a Vulkan®
[implicit layer directory](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#linux-layer-discovery).

## Contributing

We are open for contributions.

 * The software is provided under the MIT license. Contributions to this project
   are accepted under the same license.
 * Please also ensure that each commit in the series has at least one
   `Signed-off-by:` line, using your real name and email address. The names in
   the `Signed-off-by:` and `Author:` lines must match. If anyone else
   contributes to the commit, they must also add their own `Signed-off-by:`
   line. By adding this line the contributor certifies the contribution is made
   under the terms of the [Developer Certificate of Origin (DCO)](DCO.txt).
 * Questions, bug reports, et cetera are raised and discussed on the issues page.
 * Please make merge requests into the main branch.
 * Code should be formatted with clang-format using the project's .clang-format
   configuration.

We use [pre-commit](https://pre-commit.com/) for local git hooks to help ensure
code quality and standardization. To install the hooks run the following
commands in the root of the repository:

    $ pip install pre-commit
    $ pre-commit install

Contributors are expected to abide by the
[freedesktop.org code of conduct](https://www.freedesktop.org/wiki/CodeOfConduct/).

### Implement a new WSI backend

Instructions on how to implement a WSI backend can be found in the
[README](wsi/README.md) in the wsi folder.

## Trace

When using other layers to trace content with the WSI Layer, special attention
should be paid to the order of the layers by the Vulkan® loader. The Vulkan WSI
Layer should be placed after the trace layer as it implements entrypoints that
may not be implemented by the ICD.

One way to avoid these kinds of issues is by using an implicit
[meta-layer](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#meta-layers)
which will define the order of the layers and the WSI Layer should be placed at
the bottom of the list.

## Khronos® Conformance

This software is based on a published Khronos® Specification and is expected to
pass the relevant parts of the Khronos® Conformance Testing Process when used as
part of a conformant Vulkan® implementation.
