#!/bin/bash

# get the distro (ubuntu or rocky)
distro=`awk 'BEGIN { FS = "=" } ; /^ID=/ {gsub(/"/, ""); print $2}' /etc/*-release`

if [[ ${distro} == 'ubuntu' ]]; then 
    # install missing software
    sudo apt-get -yq update 
    sudo apt-get -yq install unzip docker.io

    # make sure we are in the 'docker' group
    sudo usermod -a -G docker ubuntu
    echo "export DOCKER_CONFIG=$HOME/.docker" >> ~/.profile
fi

if [[ ${distro} == 'rocky' ]]; then
    sudo dnf -yq update
    sudo dnf -yq install unzip
    sudo systemctl start docker

    # make sure we are in the 'docker' group
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

