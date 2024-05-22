#!/bin/bash

# install missing software
sudo apt-get -yq update 
sudo apt-get -yq install unzip

# make sure we are in the 'docker' group
sudo usermod -a -G docker ubuntu

# install docker compose and buildx bits
export DOCKER_CONFIG=$HOME/.docker
mkdir -p ${DOCKER_CONFIG}/cli-plugins
curl -SL https://github.com/docker/compose/releases/download/v2.27.0/docker-compose-linux-x86_64   -o $DOCKER_CONFIG/cli-plugins/docker-compose 
chmod +x .docker/cli-plugins/docker-compose

curl -SL https://github.com/docker/buildx/releases/download/v0.14.0/buildx-v0.14.0.linux-amd64  -o $DOCKER_CONFIG/cli-plugins/docker-buildx
chmod +x .docker/cli-plugins/docker-buildx

# so its available on login
echo "export DOCKER_CONFIG=$HOME/.docker" >> ~/.profile