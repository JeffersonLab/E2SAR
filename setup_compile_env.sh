#!/bin/bash

# Update and source this script to setup your environment so meson can find things.
# Prerequisites: activate the e2sar-dev conda environment before sourcing this script.
#   conda activate e2sar-dev

CONDA_ENV=/opt/anaconda3/envs/e2sar-dev

# All dependencies (gRPC, Boost, protobuf) come from the conda environment.
export BOOST_ROOT=${CONDA_ENV}
export PKG_CONFIG_PATH=${CONDA_ENV}/lib/pkgconfig

# Clear any stale CPPFLAGS/LDFLAGS that could inject conflicting system headers.
export CPPFLAGS=""
export LDFLAGS=""

# macOS runtime library path
export DYLD_LIBRARY_PATH=${CONDA_ENV}/lib
# Linux runtime library path
export LD_LIBRARY_PATH=${CONDA_ENV}/lib
