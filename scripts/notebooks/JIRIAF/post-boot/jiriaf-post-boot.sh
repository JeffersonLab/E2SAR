#!/bin/bash

# get the distro (ubuntu or rocky)
distro=`awk 'BEGIN { FS = "=" } ; /^ID=/ {gsub(/"/, ""); print $2}' /etc/*-release`

if [[ ${distro} == 'ubuntu' ]]; then 
    echo Installing for Ubuntu
    
    # install missing software. we remove ufw and install firewalld so we can use
    # the same firewall commands on rocky and ubuntu
    sudo dpkg -r ufw
    sudo apt-get -yq update
    sudo apt-get -yq install python3-pip openssl docker.io firewalld git
    sudo systemctl enable --now firewalld.service

    # add user to docker
    sudo usermod -a -G docker ubuntu

fi

if [[ ${distro} == 'rocky' ]]; then
    echo Installing for Rocky

    # install missing software. rocky should come with firewalld pre-installed
    sudo dnf -yq install epel-release
    sudo dnf -yq --enablerepo=devel update
    sudo dnf -yq --enablerepo=devel install python3-pip openssl git python39

    # add user to docker
    sudo usermod -a -G docker rocky

fi

# install docker compose and buildx bits
export DOCKER_CONFIG=$HOME/.docker
mkdir -p ${DOCKER_CONFIG}/cli-plugins
curl -SL https://github.com/docker/compose/releases/download/v2.27.0/docker-compose-linux-x86_64   -o $DOCKER_CONFIG/cli-plugins/docker-compose 
chmod +x .docker/cli-plugins/docker-compose

curl -SL https://github.com/docker/buildx/releases/download/v0.14.0/buildx-v0.14.0.linux-amd64  -o $DOCKER_CONFIG/cli-plugins/docker-buildx
chmod +x .docker/cli-plugins/docker-buildx
