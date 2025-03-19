#!/bin/bash

show_help () {
	echo "reserve-lb.sh requires two arguments: "
	echo "-n <lbname> -d <reservation length in hours>"
}

# A POSIX variable
OPTIND=1       

# Initialize our own variables:
duration='2'
lbname='mylb'

while getopts ":h?n:d:" opt; do
  case "$opt" in
    h|\?)
      show_help
      exit 0
      ;;
    n)  lbname=$OPTARG
      ;;
    d)  duration=$OPTARG
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

# this address won't be needed in version 0.2.0
fake_sender_address='8.8.8.8'

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
echo 'Reserving a new load balancer for $duration hours and generating $HOME/e2sar-instance.env'
lbadm -v --reserve --lbname ${lbname} -a ${fake_sender_address} -d ${duration} -e > ${HOME}/e2sar-instance.env
echo New instance URI is
cat $HOME/e2sar-instance.env
