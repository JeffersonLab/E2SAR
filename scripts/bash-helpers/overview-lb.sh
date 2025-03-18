#!/bin/bash

echo Sourcing global configuration in $HOME/e2sar.env
if [ ! -e ${HOME}/e2sar.env ]; then
	echo 'Unable to find admin configuration $HOME/e2sar.env'
	echo 'Please create it following the this template:'
    echo ''
	echo 'export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:<custom e2sar install path>'
	echo 'export PATH=<e2sar installation path>:$PATH'
	echo "export EJFAT_URI='<admin URI>'"
	exit -1
fi
source $HOME/e2sar.env
echo 'Calling overview'
lbadm -v --overview
