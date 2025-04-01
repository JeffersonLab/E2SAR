#!/bin/bash

# this is a simple testing script to use e2sar as a SENDER.
# all settings are very conservative, with the aim to establish
# basic functionality, not optimize the performance

show_help () {
	echo "e2sar-sender.sh requires two arguments: "
	echo "-a <dataplane IP address of this host> -m <MTU of the dataplane interface>"
}

# A POSIX variable
OPTIND=1       

# Initialize our own variables:
myDataPlaneIP=''
dataPlaneMTU='1500'

while getopts ":h?a:m:" opt; do
  case "$opt" in
    h|\?)
      show_help
      exit 0
      ;;
    a)  myDataPlaneIP=$OPTARG
      ;;
	m)	dataPlaneMTU=$OPTARG
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
dataRateInGbps=1
eventLengthInBytes=100000
numEventsToSend=10000

if [ "${myDataPlaneIP:0:1}" == "<" ]; then
	echo "Please change myDataPlaneIP in this script to a real value"
	exit -1
fi

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
echo Sourcing instance configuration in e2sar-instance.env
if [ ! -e ${HOME}/e2sar-instance.env ]; then
	echo Unable to find instance configuration e2sar-instance.env, have you run reserve.sh or copied e2sar-instance.env from elsewhere?
	exit -1
fi
source $HOME/e2sar-instance.env

if [ ${E2SAR_IN_DOCKER} == "yes" ]; then
	COMMAND_PREFIX="docker run --rm --network host ibaldin/e2sar"
else
	COMMAND_PREFIX=""
fi
${COMMAND_PREFIX} e2sar_perf -s --mtu ${dataPlaneMTU} --rate ${dataRateInGbps} --length ${eventLengthInBytes} -n ${numEventsToSend} --ip ${myDataPlaneIP} --withcp -v
