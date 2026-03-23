#!/bin/bash
# QEMU Output Capture Script for HimuOS
# Usage: ./qemu_capture.sh [timeout_seconds] [output_file]

TIMEOUT=${1:-30}
OUTPUT_FILE=${2:-/tmp/qemu_output.log}
SUDO_PASS="liuhuan123"

echo "Starting QEMU with ${TIMEOUT}s timeout..."
echo "Output will be saved to: ${OUTPUT_FILE}"

# Run make run with sudo in background, redirect output
echo "$SUDO_PASS" | sudo -S make run 2>&1 | tee "$OUTPUT_FILE" &
QEMU_PID=$!

# Wait for specified timeout
sleep "$TIMEOUT"

# Kill QEMU process
pkill -f qemu-system || true
kill $QEMU_PID 2>/dev/null || true

echo ""
echo "=== QEMU Output Captured ==="
cat "$OUTPUT_FILE"
