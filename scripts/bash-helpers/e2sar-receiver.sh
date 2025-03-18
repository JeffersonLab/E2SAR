#!/bin/bash

# this is a simple testing script to use e2sar as a RECEIVER.
# all settings are very conservative, with the aim to establish
# basic functionality, not optimize the performance

# Please modify the parameters below as needed

myDataPlaneIP='<ip address of the interface for the dataplane>'
durationInSeconds=30

if [ "${myDataPlaneIP:0:1}" == "<" ]; then
	echo "Please change myDataPlaneIP in this script to a real value"
	exit -1
fi

# these rarely need to be changed
startingUDPPort=20000
numReceiveThreads=1

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
echo Sourcing instance configuration in $HOME/e2sar-instance.env
if [ ! -e ${HOME}/e2sar-instance.env ]; then
	echo Unable to find instance configuration e2sar-instance.env, have you run reserve.sh or copied e2sar-instance.env from elsewhere?
	exit -1
fi
source $HOME/e2sar-instance.env

e2sar_perf -r -d ${durationInSeconds} --ip ${myDataPlaneIP} --port ${startingUDPPort} --threads ${numReceiveThreads} --withcp -v

