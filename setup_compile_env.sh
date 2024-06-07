#!/bin/bash

# Update and source this script to setup your environment so meson can find things
# point wherever gRPC installation is 

# Pay particular attention to where gRPC (likely installed from source) and Boost (possibly
# installed from source) are located.

# point to gRPC
# On Homebrew it is just /opt/homebrew
GRPC_ROOT=/opt/homebrew
# to point to Boost 
# On Homebrew it is just /opt/homebrew
export BOOST_ROOT=/opt/homebrew

# point pkg_config and meson to .pc files
export PKG_CONFIG_PATH=${GRPC_ROOT}/lib/pkgconfig
# point to bin/ of grpc to find protoc (you may also need to add $HOME/.local/bin for python local installs of meson)
export PATH=${GRPC_ROOT}/bin:$PATH
# for MacOS point to lib where grpc and boost compiled artifacts can be
export DYLD_LIBRARY_PATH=${GRPC_ROOT}/lib:${BOOST_ROOT}/lib
# for Linux point to lib where grpc and boost compiled artifacts can be
export LD_LIBRARY_PATH=${GRPC_ROOT}/lib:${BOOST_ROOT}/lib
