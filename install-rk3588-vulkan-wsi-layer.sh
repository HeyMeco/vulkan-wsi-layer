#!/bin/bash

# Find the latest kernel headers matching the pattern /usr/src/linux-headers-6.1.*
KERNEL_HEADER_DIR=$(ls -d /usr/src/linux-headers-6.1.* 2>/dev/null | sort -V | tail -n 1)

# Check if a valid kernel header directory was found
if [[ -z "$KERNEL_HEADER_DIR" ]]; then
  echo "Error: No matching Linux kernel headers found in /usr/src/"
  exit 1
fi

echo "Using kernel headers from: $KERNEL_HEADER_DIR"

# Install required packages
echo "Installing required packages..."
sudo apt update
sudo apt install -y linux-headers-vendor-rk35xx cmake libvulkan-dev libdrm-dev libwayland-dev wayland-protocols libx11-xcb-dev

# Clone the repository
echo "Cloning vulkan-wsi-layer repository..."
git clone https://github.com/KhronosGroup/Vulkan-WSI-Layer.git
cd Vulkan-WSI-Layer || { echo "Failed to enter repository directory"; exit 1; }

# Run CMake with the required configuration
echo "Configuring build with CMake..."
cmake . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_WSI_HEADLESS=0 \
  -DBUILD_WSI_WAYLAND=1 \
  -DBUILD_WSI_X11=0 \
  -DSELECT_EXTERNAL_ALLOCATOR=dma_buf_heaps \
  -DKERNEL_HEADER_DIR="$KERNEL_HEADER_DIR" \
  -DENABLE_WAYLAND_FIFO_PRESENTATION_THREAD=1 \
  -DWSIALLOC_MEMORY_HEAP_NAME=system-uncached

# Build and install
echo "Building and installing..."
sudo make -i

# Reboot prompt
echo "Installation complete. Please reboot your system."
read -p "Would you like to reboot now? (y/N): " REBOOT
if [[ "$REBOOT" =~ ^[Yy]$ ]]; then
  sudo reboot
else
  echo "Reboot manually when ready."
fi
