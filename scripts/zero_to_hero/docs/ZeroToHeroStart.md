# EJFAT - From Zero to Hero

## Introduction

The following set of learning activities can be pursued in parallel:

1. "Hello world". Using e2sar_perf to send and receive packets in back to back mode on a single node or laptop. This does not need a load balancer and assumes 1 sender and 1 receiver.

2. Extending "Hello world" by running it on Perlmutter with a real load balancer and more than one sender and / or receiver

3. Replacing e2sar_perf with a real application. This is done with the E2SAR development library, and can be done in Python (recommended) or C++ (steeper learning curve).

4. Extending the transmission model to send traffic from SLAC to Perlmutter. This will depend on how quickly we can debug the network path. It does not limit progress on 1, 2 and 3 above.

## 1. Hello World

Study the documentation here to get oriented with EJFAT:

[https://github.com/JeffersonLab/E2SAR/wiki/Integration](https://github.com/JeffersonLab/E2SAR/wiki/Integration)

To get started we need the e2sar_perf binaries. There are many ways to get these, including conda install, pre built docker versions, and building from source. For the very first pass we recommend using docker:

[https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries](https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries)

```bash
docker run --rm --network host ibaldin/e2sar:latest lbadm --help -v
```

Example output:

```
(base)yak@yk-macbook:ejfat_tests(main)$ docker run --rm --network host ibaldin/e2sar:latest e2sar_perf --help -v

Unable to find image 'ibaldin/e2sar:latest' locally
latest: Pulling from ibaldin/e2sar
a3be5d4ce401: Downloading [==========================> ] 15.87MB/29.54MB
8820264101df: Downloading [=> ] 9.174MB/240.2MB
60235472d435: Download complete
cfe54ce66ff6: Download complete
570caf699672: Waiting
92ad791d8720: Waiting
c50bbcbc011a: Waiting
a3d4a61339e0: Waiting
6d3c5af0f2be: Waiting

E2SAR Version: 0.2.2
E2SAR Available Optimizations: none,sendmmsg
Command-line options:
  -h [ --help ]                 show this help message
  -s [ --send ]                 send traffic
  -r [ --recv ]                 receive traffic
  -l [ --length ] arg (=1048576) event buffer length (defaults to 1024^2) [s]
  -u [ --uri ] arg              specify EJFAT_URI on the command-line instead
                                of the environment variable
  -n [ --num ] arg (=10)        number of event buffers to send (defaults to
                                10) [s]
  -e [ --enum ] arg (=0)        starting event number (defaults to 0) [s]
  -m [ --mtu ] arg (=1500)      MTU (default 1500) [s]
  --src arg (=1234)             Event source (default 1234) [s]
  --dataid arg (=4321)          Data id (default 4321) [s]
  --threads arg (=1)            number of receive threads (defaults to 1) [r]
  --sockets arg (=4)            number of send sockets (defaults to 4) [r]
  --rate arg (=1)               send rate in Gbps (defaults to 1.0, negative
                                value means no limit)
  -p [ --period ] arg (=1000)   receive side reporting thread sleep period in
                                ms (defaults to 1000) [r]
  -b [ --bufsize ] arg (=3145728) send or receive socket buffer size (default
                                to 3MB)
  -d [ --duration ] arg (=0)    duration for receiver to run for (defaults to
                                0 - until Ctrl-C is pressed)[s]
  -c [ --withcp ]               enable control plane interactions
  -i [ --ini ] arg              INI file to initialize SegmenterFlags [s] or
                                ReassemblerFlags [r]. Values found in the
                                file override --withcp, --mtu, --sockets,
                                --novalidate, --ip[46] and --bufsize
  --ip arg                      IP address (IPv4 or IPv6) from which sender
                                sends from or on which receiver listens
                                (conflicts with --autoip) [s,r]
  --port arg (=10000)           Starting UDP port number on which receiver
                                listens. Defaults to 10000. [r]
  -6 [ --ipv6 ]                 force using IPv6 control plane address if URI
                                specifies hostname (disables cert validation)
                                [s,r]
  -4 [ --ipv4 ]                 force using IPv4 control plane address if URI
                                specifies hostname (disables cert validation)
                                [s,r]
  -v [ --novalidate ]           don't validate server certificate [s,r]
  --autoip                      auto-detect dataplane outgoing ip address
                                (conflicts with --ip; doesn't work for
                                reassembler in back-to-back testing) [s,r]
  --deq arg (=1)                number of event dequeue threads in receiver
                                (defaults to 1) [r]
  --cores arg                   optional list of cores to bind sender or
                                receiver threads to; number of receiver
                                threads is equal to the number of cores [s,r]
  -o [ --optimize ] arg         a list of optimizations to turn on [s]
  --numa arg (=-1)              bind all memory allocation to this NUMA node
                                (if >= 0) [s,r]
  --multiport                   use consecutive destination ports instead of
                                one port [s]
  --smooth                      use smooth shaping in the sender (only works
                                without optimizations and at low sub 3-5Gbps
                                rates!) [s]
  --timeout arg (=500)          event timeout on reassembly in MS [r]
```

A trivial loopback invocation sending 10 1MB events at 1Gbps looks like this (start the receiver first, stop it with Ctrl-C when done):

**Receiver:**
```bash
e2sar_perf --ip '127.0.0.1' -r -u 'ejfat://token@127.0.0.1:18020/lb/36?data=127.0.0.1:10000'
```

**Sender:**
```bash
e2sar_perf --ip '127.0.0.1' -s -u 'ejfat://token@127.0.0.1:18020/lb/36?data=127.0.0.1:10000' --rate 1
```

At the very bottom of the help page is an easter egg for new users. The exact commands you need to run a back to back test.

Start a receiver and then a transmitter and see if they connect to each other.

On Mac OS you **might** see some event loss with this first docker based test. Ignore those for now, on Mac OS we will switch to using conda based native binaries for e2sar_perf in the next step. On Linux based systems, we will continue with the docker based workflow.

## 1.2 Moving to conda based installs

Now that we have first success with e2sar_perf. Follow the instructions on this page to install E2SAR into a conda env. This will improve performance on Mac OS and set the stage for further development when we want to integrate E2SAR into a custom application.

[https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries](https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries)

Once e2sar_perf is installed with conda on your laptop, it is possible to invoke e2sar_perf directly. It also installs python python packages that we can import into our own custom senders and receivers at a later step.

## 2. Extending hello world and running it on Perlmutter

### 2.1 Setting up Load Balancer Access on Perlmutter

Log into Perlmutter and repeat the docker based exercise to run e2sar_perf on the Perlmutter login node. NERSC has comprehensive documentation on how to run docker containers in their environment. They use podman-hpc rather than docker, for our purposes it is functionally the same.

[https://docs.nersc.gov/development/containers/podman-hpc/overview/](https://docs.nersc.gov/development/containers/podman-hpc/overview/)

NERSC provides shifter and podman-hpc as two different alternatives for running containers. We have been working with podman-hpc.

Make sure that you have set your environment variable EJFAT_URI based on the token provided to you by ESnet. It should look something like the following. The port and load balancer hostname might differ.

```bash
export EJFAT_URI='ejfats://nuYr-------JlZ1@ejfat-lb.es.net:18008/lb/'
```

Now test to see if you can contact the ESnet load balancer:

```bash
podman-hpc run -e EJFAT_URI=$EJFAT_URI --rm --network host ibaldin/e2sar:latest lbadm --overview
```

If you are successful go ahead and reserve a load balancer:

```bash
podman-hpc run -e EJFAT_URI=$EJFAT_URI --rm --network host ibaldin/e2sar:latest lbadm --reserve --lbname "yk_test" --export
```

Example output:
```bash
export EJFAT_URI='ejfats://cc----5c21@ejfat-lb.es.net:18008/lb/272?sync=192.188.29.6:19010&data=192.188.29.10&data=[2001:400:a300::10]'
```

The reserve command will return a new URI, conveniently formatted as an export statement. Cut and paste the export command so that your EJFAT_URI is now pointing to this more specific instance URI. You can see that it has additional information about your load balancer instance, such as the dataplane address that you will be sending to.

You can run overview again to see the status of this freshly minted load balancer:

```bash
podman-hpc run -e EJFAT_URI=$EJFAT_URI --rm --network host ibaldin/e2sar:latest lbadm --overview
```

And you can free this instance using lbadm --free:

```bash
podman-hpc run -e EJFAT_URI=$EJFAT_URI --rm --network host ibaldin/e2sar:latest lbadm --free
```

Congratulations... sit back and bask in the joy that you are now ready to start streaming to and from the live load balancer hosted inside ESnet.

### 2.2 Running Sender and Receiver Jobs on Perlmutter

Now that you have successfully connected to the ESnet load balancer, you're ready to run actual performance tests on Perlmutter.

To make testing easier, we provide a set of minimal wrapper scripts that handle:
- Load balancer reservation management
- Automatic IP address detection for multi-homed systems
- Containerized sender and receiver execution
- Memory usage monitoring
- SLURM batch job orchestration for distributed testing

**For complete instructions on running sender and receiver jobs on Perlmutter, please see:**

- **[QuickStartMinimalScripts.md](QuickStartMinimalScripts.md)** - Quick start guide and script overview
- **[RunningMinimalScripts.md](RunningMinimalScripts.md)** - Comprehensive tutorial covering:
  - Basic sender/receiver workflows
  - Performance tuning parameters
  - SLURM batch job submission
  - Memory monitoring and optimization
  - Troubleshooting common issues

These scripts provide a production-ready workflow for network performance testing on Perlmutter, handling all the containerization and networking complexity automatically.

---

## Appendix - Useful Stuff to Know

### On Mac OS: Running Docker with Colima

To run Docker with Colima, you need to install both the docker CLI and colima, start the Colima virtual machine (VM), and then use standard docker commands. Colima provides a lightweight Linux VM on your macOS (or Linux) machine where the Docker daemon runs.

#### Prerequisites

You will need Homebrew installed to manage packages on macOS.

#### Setup and Usage

Follow these steps to set up and run Docker with Colima:

1. **Install Colima and Docker CLI:** Open your terminal and run the following commands to install Colima and the Docker client, Docker Compose, and Buildx via Homebrew:

   ```bash
   brew install colima docker docker-compose docker-buildx
   ```

2. **Start Colima:** Initiate the Colima VM. By default, Colima uses the Docker runtime:

   ```bash
   colima start
   ```

   You can customize the VM's resources (CPU, memory, disk) on startup if needed:

   ```bash
   colima start --cpu 4 --memory 8 --disk 60
   ```

   If you have Apple Silicon (M1/M2/M3), you can also specify the architecture to ensure compatibility with certain images:

   ```bash
   colima start --arch x86_64
   ```

3. **Verify Status:** Check that the Colima instance is running and using the Docker runtime:

   ```bash
   colima status
   ```

4. **Use Docker Commands:** Once Colima is running, you can use standard Docker commands as usual. The Docker client on your host machine will automatically connect to the Docker daemon running inside the Colima VM:

   ```bash
   docker run hello-world
   docker pull ubuntu
   docker images
   docker ps
   ```
