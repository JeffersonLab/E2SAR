#!/bin/bash

echo Sourcing global configuration in $HOME/e2sar.env
if [ ! -e ${HOME}/e2sar.env ]; then
	echo 'Unable to find admin configuration $HOME/e2sar.env'
	echo 'Please create it following the this template:'
    echo ''
	echo '# will you run from docker or installed locally'
	echo 'export E2SAR_IN_DOCKER=yes'
	echo '# if running in docker, you can omit setting PATH and LD_LIBRARY_PATH'
	echo 'export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:<custom e2sar install path>'
	echo 'export PATH=<e2sar installation path>:$PATH'
	echo "export EJFAT_URI='<admin URI>'"
	exit -1
fi
source $HOME/e2sar.env
echo 'Calling overview'
if [ ${E2SAR_IN_DOCKER} == "yes" ]; then
	COMMAND_PREFIX="docker run --rm --network host ibaldin/e2sar"
else
	COMMAND_PREFIX=""
fi
${COMMAND_PREFIX} lbadm -v --overview
