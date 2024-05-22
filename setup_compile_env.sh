#!/bin/bash

# Run this script to setup your environment so meson can find things
# point wherever gRPC installation is 

# point to .pc files
export PKG_CONFIG_PATH=/Users/baldin/workspaces/workspace-ejfat/grpc-install/lib/pkgconfig
# point to bin/
export PATH=/Users/baldin/workspaces/workspace-ejfat/grpc-install/bin:$PATH
# for MacOS point to lib
export DYLD_LIBRARY_PATH=/Users/baldin/workspaces/workspace-ejfat/grpc-install/lib
# for Linux point to lib
export LD_LIBRARY_PATH=/Users/baldin/workspaces/workspace-ejfat/grpc-install/lib
