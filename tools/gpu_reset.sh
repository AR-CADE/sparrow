#!/bin/bash

echo "
======================================================
  AMDGPU GPU RECOVERY FOR SPARROW-SLP (FORCED RECOVERY)
======================================================

This script attempts to **force** a GPU reset on AMD hardware
using the sysfs recovery interface.

======================================================
WARNINGS:
- This is a HARD reset of the GPU driver/firmware
- It will likely crash or disrupt ALL running applications
- Use ONLY for debugging GPU driver crashes
- You will need to restart the compositor after this
- You will not work in nested mode

======================================================
Press [ENTER] to continue with GPU reset
Press [Ctrl+C] to cancel
======================================================

"
read -p ""

sudo cat /sys/kernel/debug/dri/1/amdgpu_gpu_recover

GPU=$(lspci -D | grep -Ei 'VGA|Display|3D' | head -n1 | awk '{print $1}') || exit 1
[ -n "$GPU" ] || { echo "No GPU address found." >&2; exit 1; }

echo 1 | sudo tee "/sys/bus/pci/devices/$GPU/reset"
echo "exit status: ${PIPESTATUS[0]}"
