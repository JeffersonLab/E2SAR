#!/bin/bash

# get the distro (ubuntu or rocky)
distro=`awk 'BEGIN { FS = "=" } ; /^ID=/ {gsub(/"/, ""); print $2}' /etc/*-release`

if [[ ${distro} == 'ubuntu' ]]; then 
    echo Installing for Ubuntu
    # install missing software
    sudo apt-get -yq update
    sudo apt-get -yq install python3-pip build-essential autoconf cmake libtool pkg-config libglib2.0-dev ninja-build openssl libssl-dev libsystemd-dev protobuf-compiler libre2-dev gdb docker.io
    
    # install meson
    pip3 install --user meson pybind11
    # install scapy system-wide as root needs to run it
    pip3 install scapy
    
    sudo usermod -a -G docker ubuntu

    # install git-lfs
    curl -s https://packagecloud.io/install/repositories/github/git-lfs/script.deb.sh | sudo bash
    sudo apt-get install git-lfs
    git lfs install 
    
    # enable apport so core dumps can be caught under /var/lib/apport/coredump/
    sudo systemctl enable apport.service
fi

if [[ ${distro} == 'rocky' ]]; then
    echo Installing for Rocky

    sudo dnf -yq install epel-release
    sudo dnf -yq --enablerepo=devel update
    sudo dnf -yq --enablerepo=devel install gcc gcc-c++ kernel-devel make
    # libssl-dev libsystemd-dev
    sudo dnf -yq --enablerepo=devel install python3-pip autoconf cmake libtool pkg-config ninja-build openssl protobuf-compiler gdb git glib2-devel re2-devel libquadmath-devel python39-devel python39

    sudo update-alternatives --set python /usr/bin/python3.9
    sudo update-alternatives --set python3 /usr/bin/python3.9
    #install meson 
    pip3 install --user meson pybind11
    # install scapy system-wide as root needs to run it
    pip3 install scapy

    # install git-lfs
    curl -s https://packagecloud.io/install/repositories/github/git-lfs/script.rpm.sh | sudo bash
    sudo dnf -yq install git-lfs
    
fi

# install docker compose and buildx bits
export DOCKER_CONFIG=$HOME/.docker
mkdir -p ${DOCKER_CONFIG}/cli-plugins
curl -SL https://github.com/docker/compose/releases/download/v2.27.0/docker-compose-linux-x86_64   -o $DOCKER_CONFIG/cli-plugins/docker-compose 
chmod +x .docker/cli-plugins/docker-compose

curl -SL https://github.com/docker/buildx/releases/download/v0.14.0/buildx-v0.14.0.linux-amd64  -o $DOCKER_CONFIG/cli-plugins/docker-buildx
chmod +x .docker/cli-plugins/docker-buildx

# put cpnode in etc hosts to enable testing certificate validation
echo "192.168.0.3 cpnode" | sudo tee -a /etc/hosts
