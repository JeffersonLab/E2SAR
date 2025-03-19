#!/bin/bash

# this is a simple testing script to use e2sar as a RECEIVER.
# all settings are very conservative, with the aim to establish
# basic functionality, not optimize the performance

show_help () {
	echo "e2sar-receiver.sh requires two arguments: "
	echo "-a <dataplane IP address of this host> -d <how long to run in seconds>"
}

# A POSIX variable
OPTIND=1       

# Initialize our own variables:
myDataPlaneIP=''
duration=''

while getopts ":h?a:d:" opt; do
  case "$opt" in
    h|\?)
      show_help
      exit 0
      ;;
    a)  myDataPlaneIP=$OPTARG
      ;;
	d)  durationInSeconds=$OPTARG
	  ;;
	:)
	  show_help
	  exit 0
	  ;;
  esac
done

# if no options were passed
if [ $OPTIND -eq 1 ]; then
	show_help
	exit 0
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

