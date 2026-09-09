#!/bin/bash

# get the distro (ubuntu or rocky)
distro=`awk 'BEGIN { FS = "=" } ; /^ID=/ {gsub(/"/, ""); print $2}' /etc/*-release`

if [[ ${distro} == 'ubuntu' ]]; then
    echo Installing for Ubuntu

    # need to install python3.10 on ubuntu20.04
    version=`awk 'BEGIN { FS = "=" } ; /^DISTRIB_RELEASE=/ {gsub(/"/, ""); print $2}' /etc/*-release`

    if [[ ${version} == '20.04' ]]; then
        echo Installing python3.10 for Ubuntu 20.04
        sudo apt-get -yq update
        sudo apt-get install -yq software-properties-common
        sudo add-apt-repository -y ppa:deadsnakes/ppa
        sudo apt-get -yq update
        sudo apt-get -yq install python3.10
        sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 1
        sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.10 2
    fi

    # install missing software
    sudo apt-get -yq update
    sudo dpkg -r ufw
    sudo apt-get -yq install python3-pip build-essential autoconf cmake libtool pkg-config libglib2.0-dev ninja-build openssl libssl-dev libsystemd-dev protobuf-compiler libre2-dev gdb docker.io firewalld unzip liburing-dev liburing2

    # install meson
    if [[ ${version} == '24.04' ]]; then
        pip3 install --user --break-system-packages meson pybind11
    else
        pip3 install --user meson pybind11
    fi

    # install scapy system-wide as root needs to run it
    pip3 install scapy

    sudo usermod -a -G docker ubuntu
    echo "export DOCKER_CONFIG=$HOME/.docker" >> ~/.profile

    # enable apport so core dumps can be caught under /var/lib/apport/coredump/
    sudo systemctl enable apport.service

fi

if [[ ${distro} == 'rocky' ]]; then
    echo Installing for Rocky

    sudo dnf -yq install epel-release
    sudo dnf -yq --enablerepo=devel update
    sudo dnf -yq --enablerepo=devel install gcc gcc-c++ kernel-devel make
    # libssl-dev libsystemd-dev
    sudo dnf -yq --enablerepo=devel install python3-pip autoconf cmake libtool pkg-config ninja-build openssl protobuf-compiler gdb git glib2-devel re2-devel libquadmath-devel python39-devel python39 liburing-devel liburing

    sudo update-alternatives --set python /usr/bin/python3.9
    sudo update-alternatives --set python3 /usr/bin/python3.9
    #install meson
    pip3 install --user meson pybind11
    # install scapy system-wide as root needs to run it
    pip3 install scapy

    sudo systemctl start docker
    sudo usermod -a -G docker rocky
    echo "export DOCKER_CONFIG=$HOME/.docker" >> ~/.bashrc
    echo "export DOCKER_CONFIG=$HOME/.docker" >> ~/.profile

fi

# install docker compose and buildx bits
export DOCKER_CONFIG=$HOME/.docker
mkdir -p ${DOCKER_CONFIG}/cli-plugins
curl -SL https://github.com/docker/compose/releases/download/v2.27.0/docker-compose-linux-x86_64   -o $DOCKER_CONFIG/cli-plugins/docker-compose
chmod +x .docker/cli-plugins/docker-compose

curl -SL https://github.com/docker/buildx/releases/download/v0.14.0/buildx-v0.14.0.linux-amd64  -o $DOCKER_CONFIG/cli-plugins/docker-buildx
chmod +x .docker/cli-plugins/docker-buildx

# make docker socket world-accessible so persistent SSH sessions (e.g. paramiko)
# can use docker without picking up group membership changes made after connect
sudo chmod 666 /var/run/docker.sock

# put cpnode in etc hosts to enable testing certificate validation
echo "192.168.0.3 cpnode" | sudo tee -a /etc/hosts
