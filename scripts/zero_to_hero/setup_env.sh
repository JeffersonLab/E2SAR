#!/bin/bash
# E2SAR Zero to Hero Environment Setup
#
# Usage:
#   source /path/to/zero_to_hero/setup_env.sh
#
# Or add to ~/.bashrc or ~/.zshrc:
#   source /path/to/zero_to_hero/setup_env.sh
#

# Determine the directory containing this script
if [[ -n "${BASH_SOURCE[0]}" ]]; then
    E2SAR_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
elif [[ -n "${(%):-%x}" ]]; then
    # zsh compatibility
    E2SAR_SCRIPTS_DIR="$(cd "$(dirname "${(%):-%x}")" && pwd)"
else
    echo "Warning: Could not determine E2SAR scripts directory"
    return 1 2>/dev/null || exit 1
fi

# Add scripts directory to PATH if not already present
if [[ ":$PATH:" != *":$E2SAR_SCRIPTS_DIR:"* ]]; then
    export PATH="$E2SAR_SCRIPTS_DIR:$PATH"
fi

# Export for reference
export E2SAR_SCRIPTS_DIR

echo "E2SAR Zero to Hero scripts available at: $E2SAR_SCRIPTS_DIR"
echo "Available commands: minimal_reserve.sh, minimal_sender.sh, minimal_receiver.sh, minimal_free.sh, perlmutter_slurm.sh, perlmutter_multi_slurm.sh, monitor_memory.sh"
