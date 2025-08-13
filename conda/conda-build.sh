#!/bin/bash
set -e

# Get build number from command line argument, default to 1
BUILD_NUMBER=${1:-1}

# Export as environment variable for conda build
export CONDA_BUILD_NUMBER=$BUILD_NUMBER

conda build conda/ -c conda-forge
