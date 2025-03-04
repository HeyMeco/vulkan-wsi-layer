#!/bin/bash

# Function to find the latest matching kernel headers
find_kernel_headers() {
  ls -d /usr/src/linux-headers-6.1.* 2>/dev/null | sort -V | tail -n 1
}

# Try to find the kernel headers
KERNEL_HEADER_DIR=$(find_kernel_headers)

# If not found, install the missing package and try again
if [[ -z "$KERNEL_HEADER_DIR" ]]; then
  echo "Linux kernel headers not found. Installing linux-headers-vendor-rk35xx..."
  sudo apt update
  sudo apt install -y linux-headers-vendor-rk35xx

  # Try to find the headers again after installation
  KERNEL_HEADER_DIR=$(find_kernel_headers)
  
  # If still not found, exit with an error
  if [[ -z "$KERNEL_HEADER_DIR" ]]; then
    echo "Error: Kernel headers were installed but could not be found in /usr/src/"
    exit 1
  fi
fi

echo "Using kernel headers from: $KERNEL_HEADER_DIR"

# Install required dependencies
echo "Installing required packages..."
sudo apt install -y cmake libvulkan-dev libdrm-dev libwayland-dev wayland-protocols libx11-xcb-dev

# Clone the repository
echo "Cloning vulkan-wsi-layer repository..."
git clone https://github.com/ginkage/vulkan-wsi-layer.git
cd vulkan-wsi-layer || { echo "Failed to enter repository directory"; exit 1; }

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
