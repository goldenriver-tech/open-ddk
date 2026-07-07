#!/bin/bash

# 1. Check if ant, java, ninja, python2.7  exist in the environment
for tool in ant java ninja python2.7; do
    if ! command -v $tool >/dev/null 2>&1; then
        echo "Warning: $tool not found in PATH, adding /usr/bin to PATH"
        export PATH="/usr/bin:$PATH"
        # Check again after updating PATH
        if ! command -v $tool >/dev/null 2>&1; then
            echo "Error: $tool still not found after updating PATH!"
            exit 1
        fi
    fi
done

# 2. Check if the default python is version 2.7
PYTHON_VERSION=$(python -c 'import sys; print(sys.version_info[0])' 2>/dev/null)
if [ "$PYTHON_VERSION" != "2" ]; then
    # Create a local python27 symlink directory
    mkdir -p ./python27
    ln -sf "$(command -v python2.7)" ./python27/python
    export PATH="$(pwd)/python27:$PATH"
    echo "Switched default python to python2.7"
fi

# 3. Print current environment
echo "Current python: $(which python)"
echo "Current ant: $(which ant)"
echo "Current java: $(which java)"
echo "Current ninja: $(which ninja)"

# Your build logic follows...