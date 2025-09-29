#!/bin/bash

# Create new tmux session with custom layout
tmux new-session -d -s exos-dev

# Split window vertically (left and right)
tmux split-window -h

# Split left pane horizontally (top and bottom)
tmux select-pane -t 0
tmux split-window -v

# Start htop in top-left pane
tmux select-pane -t 0
tmux send-keys 'htop --filter=qemu' C-m

# Move to bottom-left pane (ready for commands)
tmux select-pane -t 1

# Attach to the session
tmux attach-session -t exos-dev
