# Using the scripts provided in this directory

## Overview

These are bash wrapper scripts to simplify the use of the E2SAR tools like `lbadm` and `e2sar-perf`. They do not exercise all possible options or the best performance, their options are chosen to be conservative to help make things work as easy as possible at first.

They presume E2SAR has been installed into the target system and would typically be found under `/usr/local/bin` if you didn't use a special installation destination prefix. 

## Workflow

The tools presume you want to quickly test that you can e.g. reach a loadbalancer from the host on which they are installed.

### Step 1: Create $HOME/e2sar.env file

This file follows a simple template shown below. Substitute your own values into it depending on the installation. If installing from RPM/DEB the below values should work. 

You should obtain the admin EJFAT URI and add it  into this file as well. 

```bash
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64
export PATH=/usr/local/bin:$PATH

# substitute the value of admin EJFAT URI here
export EJFAT_URI='ejfats://admintoken@loadbalancerhost:port/'
```

### Step 2: Test that you can talk to the load balancer

Run `overview-lb.sh` to list all registered load balancers
```bash
$ overview-lb.sh
Sourcing global configuration in /home/XXX/e2sar.env
Calling overview
E2SAR Version: 0.1.5
Skipping server certificate validation
Getting Overview
   Contacting: ejfats://xxx.xxx.xxx.xxx:yyyyy/ using address: ipv4:///xxx.xxx.xxx.xxx:yyyyy
LB somelbname ID: 2 FPGA LBID: 0
  Registered sender addresses: 
  Registered workers:

  LB details: expiresat=2025-12-31T23:59:59Z, currentepoch=22, predictedeventnum=114943810
```

### Step 3: Reserve a load balancer

Run `reserve-lb.sh` with two parameters - duration (`-d`) of reservation in hours and the name of the load balancer instance (`-n`), as follows:

```bash
$ ./reserve-lb.sh -n mynewlb -d 12
Sourcing global configuration in /home/XXXXX/e2sar.env
Reserving a new load balancer for $duration hours and generating $HOME/e2sar-instance.env
Skipping server certificate validation
New instance URI is
export EJFAT_URI='<instance EJFAT URI>'
```

Note that this script will save the instance EJFAT URI into a file called $HOME/e2sar-instance.env for future use.

### Step 4: Check the status of this instance

You can run `status-lb.sh` to check the status. It uses the instance URI saved in $HOME/e2sar-instance.env to do that:

```bash
$ ./status-lb.sh
Sourcing admin configuration in /home/XXXXX/e2sar.env
Sourcing instance configuration in /home/XXXXX/e2sar-instance.env
E2SAR Version: 0.1.5
Skipping server certificate validation
Getting LB Status
   Contacting: ejfats://<instance EJFAT URI without the token> using address: ipv4:///xxx.xxx.xxx.xxx.:yyyyy
   LB ID: 4
Registered sender addresses: zzz.zzz.zzz.zzz 
Registered workers:
LB details: expiresat=2025-03-19T13:19:11Z, currentepoch=0, predictedeventnum=18446744073709551615
```

### Step 5: Run the traffic check

At this point you can run sender and receiver in two separate shells to check that traffic passes through the data plane from this node. 

Shell 1 (sender). Successful execution should show something like this. Note the send rate is locked to 1Gbps, event size to 1MB and number of events sent is 10,000: 
```bash
$ ./e2sar-sender.sh -a xxx.xxx.xxx.xxx -m 1500
Sourcing global configuration in /home/XXXX/e2sar.env
Sourcing instance configuration in e2sar-instance.env
E2SAR Version: 0.1.5
Adding senders to LB: xxx.xxx.xxx.xxx
Control plane                ON
Event rate reporting in Sync ON
Using usecs as event numbers ON
*** Make sure the LB has been reserved and the URI reflects the reserved instance information.
Sending bit rate is 1 Gbps
Event size is 100,000 bytes or 800,000 bits
Event rate is 1,250 Hz
Inter-event sleep time is 800 microseconds
Sending 10,000 event buffers
Using MTU 1,500
Completed, 700,000 frames sent, 0 errors
Stopping threads
Removing senders: xxx.xxx.xxx.xxx
```

Shell 2 (receiver):
```bash
$ ./e2sar-receiver.sh -a xxx.xxx.xxx.xxx -d 30
Sourcing global configuration in /home/XXXX/e2sar.env
Sourcing instance configuration in /home/XXXX/e2sar-instance.env
E2SAR Version: 0.1.5
Control plane will be ON
Using Unassigned Threads
Will run for 30 sec
*** Make sure the LB has been reserved and the URI reflects the reserved instance information.
Receiving on ports 20000:20000
Stats:
	Events Received: 0
	Events Mangled: 0
	Events Lost: 0
	Data Errors: 0
	gRPC Errors: 0
	Events lost so far:
... OUTPUT TRUNCATED ...
Stats:
	Events Received: 10,000
	Events Mangled: 0
	Events Lost: 0
	Data Errors: 0
	gRPC Errors: 0
	Events lost so far:
Completed
Stopping threads
Deregistering worker
```

Normal output should show 10,000 event received with no errors. A small amount of loss (1-3 events is normal depending on the network noisiness). 

It is recommended to start the receiver FIRST and then start the SENDER.

### Step 6: Release the reserved instance

Run `free-lb.sh`. It will release the instance and delete $HOME/e2sar-instance.env:

```bash
$ ./free.sh
Sourcing global configuration in /home/XXXX/e2sar.env
Sourcing instance configuration in /home/XXXX/e2sar-instance.env
E2SAR Version: 0.1.5
Skipping server certificate validation
Freeing a load balancer
   Contacting: ejfats://<instance EJFAT URI without the token> using address: ipv4:///xxx.xxx.xxx.xxx.:yyyyy 
   LB ID: 5
Success.
Removing instance configuration in e2sar-instance.env
```