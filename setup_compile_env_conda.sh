#!/bin/bash

# Conda-based alternative to setup_compile_env.sh.
# Use this when all dependencies (gRPC, Boost, protobuf) come from the
# e2sar-dev conda environment instead of Homebrew or from-source builds.
#
# Prerequisites: activate the conda environment before sourcing this script.
#   conda activate e2sar-dev
#
# Then source this file and run meson:
#   source setup_compile_env_conda.sh
#   meson setup --native-file native-file-macos.ini build   # macOS
#   meson setup --native-file native-file-linux.ini build   # Linux
#
# Alternatively, skip the native file and rely solely on the exported vars:
#   source setup_compile_env_conda.sh
#   meson setup build

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
