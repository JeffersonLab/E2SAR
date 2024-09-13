#!/usr/bin/env bash

GIT_SSH_KEY_MOUNT=/src/git_ssh_key
GIT_SSH_KEY=/src/ssh/git_ssh_key

if [[ "$1" == 'setup_src' ]]; then
        echo "***** Preparing SSH key"
        # copy to a new file the readonly key and adjust permissions
        mkdir -p /src/ssh
        if [[ ! -e ${GIT_SSH_KEY_MOUNT} ]]; then
                echo Please make sure your Github SSH key is mounted as /src/git_ssh_key
                echo Use -v '${HOME}/.ssh/my_github_key:/ssh/git_ssh_key:ro' option
                exit -1
        fi
        cp $GIT_SSH_KEY_MOUNT $GIT_SSH_KEY
        # ensure proper permissions
        chmod go-rwx $GIT_SSH_KEY
        chown root:root $GIT_SSH_KEY

        echo "***** Cloning E2SAR code"
        # clone appropriate branch
        if [[ ! -z ${E2SAR_BRANCH} ]]; then
                GIT_SSH_COMMAND="ssh -i $GIT_SSH_KEY -o IdentitiesOnly=yes -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no" git clone --recurse-submodules --depth 1 -b ${E2SAR_BRANCH} git@github.com:JeffersonLab/E2SAR.git
        else
                GIT_SSH_COMMAND="ssh -i $GIT_SSH_KEY -o IdentitiesOnly=yes -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no" git clone --recurse-submodules --depth 1  git@github.com:JeffersonLab/E2SAR.git
        fi

        cd E2SAR

        echo "***** Compiling E2SAR code"
        # create a build configuration
        BOOST_ROOT=/usr/local meson setup -Dpkg_config_path=/usr/local/lib/pkgconfig/ build

        # compile
        meson compile -C build
else
    exec "$@"
fi

# just wait here
echo "***** Waiting indefinitely. The code is located under /src/E2SAR."
tail -f /dev/null