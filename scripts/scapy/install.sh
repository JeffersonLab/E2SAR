#!/bin/bash

# install scripts
# DESTDIR
# MESON_INSTALL_PREFIX /home/ubuntu/e2sar-install
# MESON_SOURCE_ROOT /home/ubuntu/E2SAR
# MESON_BUILD_ROOT /home/ubuntu/E2SAR/build

# snifgen.py needs to be put into /usr/local/bin
# and permissions changed

BINDIR="${DESTDIR}/${MESON_INSTALL_PREFIX}/bin/"

echo "Installing scripts to ${BINDIR}"
mkdir -p ${BINDIR}
cp "${MESON_SOURCE_ROOT}/scripts/scapy/snifgen.py" ${BINDIR}
