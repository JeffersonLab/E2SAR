#!/bin/bash

# install missing software
sudo apt-get -yq update
sudo apt-get -yq install python3-pip build-essential autoconf cmake libtool pkg-config libglib2.0-dev ninja-build openssl libssl-dev libsystemd-dev protobuf-compiler libre2-dev gdb

# install meson
pip3 install --user meson

# install git-lfs
curl -s https://packagecloud.io/install/repositories/github/git-lfs/script.deb.sh | sudo bash
sudo apt-get install git-lfs
git lfs install 

# enable apport so core dumps can be caught under /var/lib/apport/coredump/
sudo systemctl enable apport.service

