#!/bin/bash

# Start Bochs in run mode (auto-continue)
echo "Starting Bochs (run mode)..."
bochs -q -f scripts/bochs/bochs.txt -rc scripts/bochs/bochs_run_commands.txt
