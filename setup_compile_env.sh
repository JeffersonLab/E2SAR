#!/bin/bash

# Update and source this script to setup your environment so meson can find things
# point wherever gRPC installation is 

# Pay particular attention to where gRPC (likely installed from source) and Boost (possibly
# installed from source) are located.

# point to .pc files
export PKG_CONFIG_PATH=${HOME}/workspaces/workspace-ejfat/grpc-install/lib/pkgconfig
# point to bin/ (you may also need to add $HOME/.local/bin for python local installs)
export PATH=${HOME}/workspaces/workspace-ejfat/grpc-install/bin:$PATH
# for MacOS point to lib where grpc and boost compiled artifacts can be
export DYLD_LIBRARY_PATH=${HOME}/workspaces/workspace-ejfat/grpc-install/lib:${HOME}/workspaces/workspace-ejfat/boost-install/lib
# for Linux point to lib where grpc and boost compiled artifacts can be
export LD_LIBRARY_PATH=${HOME}/workspaces/workspace-ejfat/grpc-install/lib:${HOME}/workspaces/workspace-ejfat/boost-install/lib
# to point to Boost 
export BOOST_ROOT=/opt/homebrew
