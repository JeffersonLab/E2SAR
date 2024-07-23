# E2SAR
The internal dev repo of E2SAR - a C++ library and associated Python binding that support segmentation/reassembly (SAR) and control plane functionality of EJ-FAT project.

## Documentation

Documentation is contained in the [wiki](https://github.com/JeffersonLab/E2SAR/wiki).

## Checking out the code

### Via cloning

All binary artifacts in this project are stored using Git LFS and you must [install git lfs](https://docs.github.com/en/repositories/working-with-files/managing-large-files/installing-git-large-file-storage?platform=linux) in order to properly check out their contents.

Clone the project as usual, then `cd` into the cloned directory and execute `git submodule init` to initialize the [UDPLBd](https://github.com/esnet/udplbd) repo contents (needed for the protobuf definitions located in udplbd/pkg/pb), then run `git submodule update` to get the code:
```bash
$ git clone git@github.com:JeffersonLab/E2SAR.git  # use -b if you want a specific branch
$ cd E2SAR
$ git submodule init
$ git submodule update
```

If you want to update to the latest udplbd then also execute `git submodule update`. Note that you may need the correct branch of this project and as of this writing the is the `develop` branch and not `main`. You can do that by:
```bash
$ cd udplbd 
$ git fetch && get switch develop
```

## Building the code

Overview: the build process requires multiple dependencies for tools and code (gRPC+protobufs, boost and their dependencies). E2SAR uses Meson for a build tool. Also the protoc compiler is required to generate gRPC client stubs.

### Pre-requisistes

#### Installing dependencies

Build dependences

- MacOS: `brew install autoconf automake libtool shtool meson abseil c-ares re2 grpc pkg-config boost protobuf`
- Linux: see [this script](scripts/notebooks//EJFAT/post-boot/sender.sh) for necessary dependencies.


For python dependencies minimum Python 3.9 is required. It is recommended you create a virtual environment for Python build dependencies, then activate the venv and install pybind11 and other dependencies:

```bash
$ python3 -m venv /path/to/e2sar/venv
$ . /path/to/e2sar/venv/bin/activate
$ pip install pybind11
```
Continue using the venv when compiling and testing e2sar. PSA: you can get out of the venv by running `deactivate` from inside the venv. 

#### Installing gRPC from source

gRPC versions available in binary are frequently too far behind what is used in the [UDPLBd code](https://github.com/esnet/udplbd/blob/main/go.mod). As a result it is likely necessary to build gRPC from source

##### MacOS
- Follow instructions in [this page](https://grpc.io/docs/languages/cpp/quickstart/) using appropriate version tag/branch that matches UDPLBd dependencies.
- Use the following command to build:
```bash
$ mkdir -p cmake/build && cd cmake/build
$ cmake ../.. -DgRPC_INSTALL=ON                \
              -DCMAKE_INSTALL_PREFIX=/wherever/grpc-install/ \
              -DgRPC_BUILD_TESTS=OFF           \
              -DgRPC_ZLIB_PROVIDER=package
$ make -j 16
$ make install
```
After you do `make -j <n>` and `make install` to the location you need to tell various parts of the build system where to find gRPC++:
```bash
$ export DYLD_LIBRARY_PATH=/wherever/grpc-install/lib/
$ export PATH=/wherever/grpc-install/bin/:$PATH   
$ export PKG_CONFIG_PATH=/wherever/grpc-install/lib/pkgconfig/
```
The `setup_compile_env.sh` script sets it up (below). Then meson should be able to find everything. You can always test by doing e.g. `pkg-config --cflags grpc++`. 

##### Linux (Ubuntu 22 or RHEL8)
- Follow instructions in [this page](https://grpc.io/docs/languages/cpp/quickstart/) using appropriate version tag/branch that matches UDPLBd dependencies.
- Use the following command to build:
```bash
$ cmake -DgRPC_INSTALL=ON  \
        -DgRPC_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX=/home/ubuntu/grpc-install \
        -DBUILD_SHARED_LIBS=ON ../..
```
After you do `make -j <n>` and `make install` to the location you need to tell various parts of the build system where to find gRPC++:
```bash
$ export LD_LIBRARY_PATH=/home/ubuntu/grpc-install/lib/
$ export PATH=~/grpc-install/bin/:$PATH   
$ export PKG_CONFIG_PATH=/home/ubuntu/grpc-install/lib/pkgconfig/
```
The `setup_compile_env.sh` script sets it up (below). Then meson should be able to find everything. You can always test by doing e.g. `pkg-config --cflags grpc++`. 

#### Install Boost from source

Use [this procedure](https://www.boost.io/doc/user-guide/getting-started.html) to build Boost from scratch (particularly on older Linux systems, like ubuntu 22). Use the b2 build tool.

When using it with meson be sure to set `BOOST_ROOT` to wherever it is installed (like e.g. `export BOOST_ROOT=/home/ubuntu/boost-install`) as per [these instructions](https://mesonbuild.com/Dependencies.html#boost).

### Building libe2sar library

Make sure Meson is installed (with Ninja backend). The build should work for both LLVM/CLang and g++ compilers.

Update `setup_compile_env.sh` file for your environment. Then run:

```
$ . ./setup_compile_env.sh
$ meson setup build
$ cd build
$ meson compile
$ EJFAT_URI='ejfats://udplbd@192.168.0.3:18347/lb/1?sync=192.168.2.1:19020&data=10.100.100.14' meson test -C build --suite unit --timeout 0
$ EJFAT_URI='ejfats://udplbd@192.168.0.3:18347/lb/12?sync=192.168.100.10:19020&data=192.168.101.10:18020' meson test -C build --suite unit --timeout 0
```

The `live` test suite requires a running UDPLBd and the setting of EJFAT_URI must reflect that. `Unit` tests do not require a running UDPBLBd and the IP addresses in URI can be random.

If you desire a custom installation directory you can add `--prefix=/absolute/path/to/install/root`. If you have a custom location for pkg-config scripts, you can also add `-Dpkg_config_path=/path/to/pkg-config/scripts` to the setup command. 

### Building on older systems (e.g. RHEL8)

Due to a much older g++ compiler on those systems meson produces incorrect ninja.build files. After the `setup build` step execute the following command to correct the build file: `sed -i 's/-std=c++11//g' build/build.ninja`. 

### Building a Docker version

There are several Docker files in the root of the source tree. They build various versions of the system for improved portability

#### Control Plane and other tools

To build this docker use the following command:
```
$ docker build -t cli -f Dockerfile.cli .
```
If you are building for the Docker Hub, then
```
$ docker login
$ docker build -t <username>/<repo>:<version> [-t <username>/<repo>:latest] -f Dockerfile.cli .
$ docker push <username>/<repo>:<version>
$ docker push <username>/<repo>:latest
```
To run a locally built image with e.g. `lbadm` in it you can do something like this:
```
$ docker run cli lbadm --version -u "ejfats://udplbd@192.168.0.3:18347/" -v
$ docker run --rm cli snifgen.py -g --sync --ip 192.168.100.1
```
When running a Docker Hub version, then:
```
$ docker run --rm <username>/<repo>:latest lbadm --version -u "ejfats://udplbd@192.168.0.3:18347/" -v
```
(Notice that default docker network plumbing probably isn't appropriate for listening or sending packets using snifgen.py).

You can also use pre-built Docker images from Docker hub:
```

## Installing and creating a distribution

You can install the code after compilation by running `meson install -C build` (you can add `--dry-run` option to see where things will get installed). To set the installation destination add `--prefix /path/to/install` option to `meson setup build` command above. 

To create a source distribution you can run `meson dist -C build --no-tests` (the `--no-tests` is needed because GRPC headers won't build properly when distribution is generated). 

# Testing

## C++

E2SAR code comes with a set of tests under [test/](test/) folder. It relies on Boost unit-testing framework as well as meson testing capabilities. The easiest way is to execute `meson test` or `meson test --suite unit` or `meson test --suite live`. The latter requires an instance of UDPLBd running and `EJFAT_URI` environment variable to be set to point to it (e.g. `export EJFAT_URI="ejfats://udplbd@192.168.0.3:18347/").

There is a  [Jupyter notebook](scripts/notebooks/EJFAT/LBCP-tester.ipynb) which runs all the tests on FABRIC testbed.

## Python

TBD

## Scapy

Scapy scripts are provided for sniffing/validating and generating various kinds of UDP packets. See this [folder](scripts/scapy/) for details. Make sure Scapy is installed (`pip install scapy`) and for most tasks the scripts must be run as root (bot for sending and sniffing). 

Typical uses may include (note the scripts have sane defaults for everything, but can be overridden with command line):

Generate a LB+RE header packet with a specific payload. Only show it without sending (doesn't need root privilege)
```bash
$ ./snifgen.py -g --lbre --pld "this is payload" --show -c 1 
```
Generate and send a LB+RE header packet to 192.168.100.10 with a specific payload. Use default MTU of 1500.
```bash
$ ./snifgen.py -g --lbre --pld "this is payload" --ip "192.168.100.10" -c 1 
```
Generate and send a RE header packet to 192.168.100.10 with a specific payload. Use MTU of 50.
```bash
$ ./snifgen.py -g --re --pld "this is payload" --ip "192.168.100.10" --mtu 50 -c 1 
```
Listen for 10 Sync packets sent to port 18347 then exit.
```bash
$ ./snifgen -l -p 18347 -c 10 --sync
```
Listen for 10 Sync packets sent to 192.168.100.10 port 18347 then exit.
```bash
$ ./snifgen -l -p 18347 -c 10 --ip "192.168.100.10" --sync
```
## Dealing with SSL certificate validation

UDPLBd implements gRPC over TLS to support channel privacy. Only server-side certificate for UDPLBd is required - the code does not rely on SSL client-side authentication. For testing You can generate UDPLBd certificate as follows:

```bash
$ openssl req -x509 -newkey rsa:4096 -keyout udplbd/etc/server_key.pem -out udplbd/etc/server_cert.pem -sha256 -days 365 -nodes -subj "/CN=cpnode/subjectAltName=IP:192.168.0.3" -nodes
```

Unit test code disables server certificate validation completely. When using `lbadm` you can either use `-v` option to disable the validation or copy the self-signed server certificate from UDPLBd host onto a file on the E2SAR host and point to it using `--root` or `-o` option. These two options (`-v` and `-o`) are mutually exclusive:
```bash
ubuntu@sender:~/E2SAR$ export EJFAT_URI="ejfats://udplbd@cpnode:18347/"
ubuntu@sender:~/E2SAR$ ./build/bin/lbadm --version -o ../server_cert.pem 
Getting load balancer version 
   Contacting: ejfats://udplbd@cpnode:18347/
Sucess.
Reported version: 1fb5d83f0186298f99a57f7d4df4177871e8524f
ubuntu@sender:~/E2SAR$ ./build/bin/lbadm --version -v
Skipping server certificate validation
Getting load balancer version 
   Contacting: ejfats://udplbd@cpnode:18347/
Sucess.
Reported version: 1fb5d83f0186298f99a57f7d4df4177871e8524f
```

If using a private CA, the `-o` option should be used to point to the CA certificate, assuming the server certificate contains the chain to the CA. Note also that you must specify EJFAT_URI using a hostname, not IP address if using certificate validation. When using `-v` option, IP addresses can be used.

See [lbadm](bin/lbadm.cpp) code on how the two options are implemented and used. Programmatically it is possible to supply client-side certs to E2SAR, however UDPLBd does not validate them. See [LBManager constructor](src/e2sarCP.cpp) for how that can be done.

# Related information

- [UDPLBd repo](https://github.com/esnet/udplbd) (aka Control Plane)
- [ejfat-rs repo](https://github.com/esnet/ejfat-rs) (command-line tool for testing)
- [Integrating with EJFAT](https://docs.google.com/document/d/1aUju_pWtHpS0Coesu8dC7HP6LbuKBJZqRYAMSSBtpWQ/edit#heading=h.zbhmzz3u1sna) document
