#!/bin/bash

# install generated GRPC headers
# DESTDIR
# MESON_INSTALL_PREFIX /home/ubuntu/e2sar-install
# MESON_SOURCE_ROOT /home/ubuntu/E2SAR
# MESON_BUILD_ROOT /home/ubuntu/E2SAR/build

# loadbalancer.pb.h and loadbalancer.grpc.pb.h
# need to be put into include/

GRPC_INCLUDE="${DESTDIR}/${MESON_INSTALL_PREFIX}/include/grpc/"

echo "Installing gRPC stub header files loadbalancer.pb.h and loadbalancer.grpc.pb.h to ${GRPC_INCLUDE}"
mkdir -p ${GRPC_INCLUDE}
cp "${MESON_BUILD_ROOT}/src/grpc/loadbalancer.pb.h" ${GRPC_INCLUDE}
cp "${MESON_BUILD_ROOT}/src/grpc/loadbalancer.grpc.pb.h" ${GRPC_INCLUDE}

