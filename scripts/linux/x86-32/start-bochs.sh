#!/bin/bash

# Start Bochs in run mode (auto-continue)
echo "Starting Bochs (run mode)..."
bochs -q -f scripts/common/bochs/bochs.txt -rc scripts/common/bochs/bochs_run_commands.txt
