#!/bin/bash


# interface
INTF=enp7s0np0
# replace or add - most likely replace since the kernel will put its own root qdisc
REPLADD=add
# burst size in MB - something worth playing with - too large and you won't see any smoothing
# too small and there may be clipping losses
BURSTSZ=5mb
# desired rate - some fraction of the link rate
RATE=900mbit
# LB UDP address
LBADDR=192.168.0.2


# print out the current setup
echo PRINTING CURRENT SETUP
sudo tc qdisc show dev ${INTF}
sudo tc class show dev ${INTF}
sudo tc filter show dev ${INTF}
echo SETTING UP NEW ROOT QDISC
# set up the root qdisc as HTB
if [[ ${REPLADD} == "add" ]]; then
	echo DELETING OLD ROOT QDISC
	sudo tc qdisc del dev ${INTF} root
fi
sudo tc qdisc ${REPLADD} dev ${INTF} root handle 1: htb default 20
# set up our traffic class
echo ADDING NEW TRAFFIC CLASS
#sudo tc class del dev ${INTF} classid 1:10
sudo tc class add dev ${INTF} parent 1: classid 1:10 htb rate ${RATE} ceil ${RATE} burst ${BURSTSZ} cburst ${BURSTSZ}
# add SFQ qdisc below to make sure UDP traffic is mixing
echo ADDING SFQ LEAF
sudo tc qdisc add dev ${INTF} parent 1:10 handle 100: sfq perturb 10
# remove old filters add filter based on IP destination
echo SETTING NEW FILTER
sudo tc filter del dev ${INTF} parent 1:
sudo tc filter add dev ${INTF} protocol ip parent 1: prio 1 u32 match ip dst ${LBADDR} match ip protocol 17 0xff flowid  1:10

echo PRINTING NEW SETUP
sudo tc qdisc show dev ${INTF}
sudo tc class show dev ${INTF}
sudo tc filter show dev ${INTF}
# to delete this setup you can reboot or run the following:
# sudo tc qdisc del dev ${INTF} root
echo TO DELETE THIS SETUP DO "sudo tc qdisc del ${INTF} root"

