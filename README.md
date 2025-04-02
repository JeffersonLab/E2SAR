# E2SAR
A C++ library and associated Python binding that supports segmentation/reassembly (SAR) and control plane functionality of EJ-FAT project.

## Documentation Scope

This documentation is primarily targeted at E2SAR developers and contributors. 

Documentation for E2SAR adopters is contained in the [wiki](https://github.com/JeffersonLab/E2SAR/wiki) and the [Doxygen site](https://jeffersonlab.github.io/E2SAR-doc/annotated.html).

## Checking out the code

### Via cloning

Binary artifacts in this project are stored using Git LFS and you must [install git lfs](https://docs.github.com/en/repositories/working-with-files/managing-large-files/installing-git-large-file-storage?platform=linux) in order to properly check out their contents.

Clone the project as shown below, to include the [UDPLBd](https://github.com/esnet/udplbd) repo contents (needed for the protobuf definitions located in udplbd/pkg/pb) as well as wiki/ and docs/ (which are separate repos maintaining doxygen documentation and the wiki):
```bash
$ git clone --recurse-submodules --depth 1  # use -b if you want a specific branch
```

If you want to update to the latest udplbd then also execute `git submodule update`. Note that you may need the correct branch of this project and as of this writing the is the `develop` branch and not `main`. You can do that by:
```bash
$ cd udplbd 
$ git fetch && get switch develop
```

Also a [section below](#Development-Docker) discusses the development Docker image which can be used for modifying, compiling and testing the code inside a running container.

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
$ pip install pybind11 pytest

# To bridge the C++ google::protobuf datatypes, install the Python protobuf package
$ pip install protobuf
```
Continue using the venv when compiling and testing e2sar. PSA: you can get out of the venv by running `deactivate` from inside the venv. 

The other two large dependencies are the C++ Boost library and gRPC library. Specific versions are defined in the top-level [meson.build](./meson.build) file.

#### Installing gRPC and Boost as packages

Major dependencies are available from the [release page](https://github.com/JeffersonLab/E2SAR/releases) as `e2sar-deps` Debian package for Ubuntu 20, 22 and 24. The package includes gRPC and Boost libraries of appropriate versions for each E2SAR release installed under `/usr/local/`. Using them requires adjusting `PATH` and `LD_LIBRARY_PATH` to point to `/usr/local/bin` and `/usr/local/lib`, respectively. 

They can also be built from scratch, as described below.

#### Installing gRPC from source

gRPC versions available in binary are frequently too far behind what is used in the [UDPLBd code](https://github.com/esnet/udplbd/blob/main/go.mod). As a result it is likely necessary to build gRPC from source. 

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
$ meson compile -C build
$ EJFAT_URI='ejfats://udplbd@192.168.0.3:18347/lb/1?sync=192.168.2.1:19020&data=10.100.100.14' meson test -C build --suite unit --timeout 0
$ EJFAT_URI='ejfats://udplbd@192.168.0.3:18347/lb/12?sync=192.168.100.10:19020&data=192.168.101.10:18020' meson test -C build --suite unit --timeout 0
```

The `live` test suite requires a running UDPLBd and the setting of EJFAT_URI must reflect that. `Unit` tests do not require a running UDPBLBd and the IP addresses in URI can be random.

If you desire a custom installation directory you can add `--prefix=/absolute/path/to/install/root`. If you have a custom location for pkg-config scripts, you can also add `-Dpkg_config_path=/path/to/pkg-config/scripts` to the setup command. 

#### Building on older systems (e.g. RHEL8)

Due to a much older g++ compiler on those systems meson produces incorrect ninja.build files. After the `setup build` step execute the following command to correct the build file: `sed -i 's/-std=c++11//g' build/build.ninja`. 

## Building Dockers

There are several Docker files in the root of the source tree. They build various versions of the system for improved portability

### Control Plane and other tools

To build [this docker](Dockerfile.cli) use the following command:
```
$ docker build --platform linux/amd64 -t cli -f Dockerfile.cli .
```
If you are building for the Docker Hub, then
```
$ docker login
$ docker build --platform linux/amd64 -t <username>/<repo>:<version> [-t <username>/<repo>:latest] -f Dockerfile.cli .
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
$ docker run --rm ibaldin/e2sar:latest lbadm --version -u "ejfats://udplbd@192.168.0.3:18347/" -v
```
To run `e2sar_perf` you must use `host` network driver like so (see help for the myriad of command line options):
```bash
$ docker run --rm --network host ibaldin/e2sar:latest e2sar_perf -h
```
Using `snifgen.py` also requires using `host` network driver if receiving live traffic.

### Development Docker

We provide another Docker image which allows development inside the running container. It includes all necessary dependencies for a given version of E2SAR. VSCode can be attached to it to ease the development. 

To build this image (Dockerfile.dev) use the following command:
```
$ docker build -t <username>/<repo>:<version> -t <username>/<repo>:latest -f Dockerfile.dev .
```

This docker image expects that user's GitHub SSH key is mounted read-only when it is started in order to be able to checkout the code. It also expects an optional E2SAR branch indicator as shown:
```
$ docker run --rm -v "${HOME}/.ssh/github_ecdsa:/src/git_ssh_key:ro" -e E2SAR_BRANCH=docker-dev ibaldin/e2sar-dev:latest
```
Note that the SSH key in the container must always be named `/src/git_ssh_key`. You can add `-d` option to background the process. If you want to preserve the container state, omit the `--rm` option above. Then after the container is stopped, it can be restarted (with all the changes) as follows:
```
$ docker start <container name>
```

Once the container is started it checks out the appropriate branch and compiles it and then continues to run indefinitely. You can connect to it and find the code in `/src/E2SAR`:
```
$ docker exec -ti <container id> bash
# cd /src/E2SAR
```
To connect VSCode be sure to install the `Dev Containers`  VSCode extension. To connect click `<F1>` then in the command prompt search for `Dev Containers: Attach to Running Container`. Select that, then select the running container. A new window will open connected to this container. You can open the `/src/E2SAR` folder in this workspace to find the source code. Compiling the code requires opening a terminal from inside VSCode. Locate `/src/E2SAR` directory then issue `meson compile -C build` command like explained above to build inside the container.

## Installing and creating a distribution or a release

### Locally

You can install the code after compilation by running `meson install -C build` (you can add `--dry-run` option to see where things will get installed). To set the installation destination add `--prefix /path/to/install` option to `meson setup build` command above. 

To create a source distribution you can run `meson dist -C build --no-tests` (the `--no-tests` is needed because GRPC headers won't build properly when distribution is generated). 

### In GitHub

You can use the GitHub actions to create a release in GitHub. Four workflows are defined for this repo, each applicable to several standard Linux distributions:
- Step 1: Create dependency artifacts: compiles and builds desired versions of BOOST and gRPC
- Step 2a: Create DEBs and RPMs of the dependencies: creates DEBs and RPMs from artifacts built in Step 1, that contain just the BOOST and gRPC dependency installed into /usr/local. It is useful to install these if you are doing E2SAR development
- Step 2b: Create E2SAR artifact, DEBs and RPMs: compiles E2SAR using dependency artifacts built in Step 1 and builds DEBs and RPMs that install into /usr/local
- Step 3: publish a release: using DEBs RPMs of dependencies and E2SAR itself built in steps 2a and 2b publish a release

Steps 2a, 2b and 3 depend on the tag in the form of `vX.Y.Z` (e.g. `v0.1.5`) to be present on main marking the release. Tagging is done with `git tag -s vX.Y.Z -m "Message" && git push origin vX.Y.Z`

All workflows are manually triggered and take input parameters including the gRPC and BOOST versions and the version of E2SAR that needs to be built. Note that all artifacts in all workflows are versioned according to the operating system, version of gRPC, BOOST and E2SAR. To build for a new version of E2SAR you need to at least start with step 2a, then proceed to 2b and Step 3. If changing the version of gRPC and BOOST from default, start from Step 1, then on to 2a, 2b and Step 3. Step 1 is only specific to the versions of gRPC and BOOST and is not specific to the version of E2SAR.

## Testing

### C++

E2SAR code comes with a set of tests under [test/](test/) folder. It relies on Boost unit-testing framework as well as meson testing capabilities. The easiest way is to execute `meson test` or `meson test --suite unit` or `meson test --suite live`. The latter requires an instance of UDPLBd running and `EJFAT_URI` environment variable to be set to point to it (e.g. `export EJFAT_URI="ejfats://udplbd@192.168.0.3:18347/").

There is a  [Jupyter notebook](scripts/notebooks/EJFAT/LBCP-tester.ipynb) which runs all the tests on FABRIC testbed.

### Python

The C++ unit tests `e2sar_uri_test` and `e2sar_reas_test` have been reproduced in Python Jupyter notebooks, which can be found at [scripts/notebooks/pybind11_examples/](scripts/notebooks/pybind11_examples/). The Python `e2sar_lbcp_live_test` is currently under development.

### Scapy

Scapy scripts are provided for sniffing/validating and generating various kinds of UDP packets. See this [folder](scripts/scapy/) for details. Make sure Scapy is installed (`pip install scapy`) and for most tasks the scripts must be run as root (both for sending and sniffing). 

More details on the use can be found in the [wiki](https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries).

## Dealing with SSL certificate validation

UDPLBd implements gRPC over TLS to support channel privacy and server authentication. Only server-side certificate for UDPLBd is required - the code does not rely on SSL client-side authentication. For testing you can generate UDPLBd certificate as follows:

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

## Generating Documentation

### Doxygen

A [Doxygen configuration file](Doxyfile) is provided with the distribution. It will update the documentation under docs/ which is a submodule - a separate public repo just for the documentation. Once updated, to make the documentation go live on [GitHub pages site](https://jeffersonlab.github.io/E2SAR-doc/annotated.html) you can do as follows from E2SAR root (this assumes you have installed Doxygen 1.12.0 or later):
```bash
$ doxygen Doxyfile 
$ cd docs
$ git add .
$ git commit -S -m "Update to the documentation"
$ git push origin main
$ cd ..
$ git add docs
$ git commit -S -m "Link docs/ commit to this commit of E2SAR"
$ git push origin <appropriate E2SAR branch>
```

### Wiki

The [Wiki](https://github.com/JeffersonLab/E2SAR/wiki) is also a submodule of the project (but is a separate repository) under wiki/. You can update it by doing the following:
```bash
$ cd wiki/
$ vi <one or more of the .md pages>
$ git add .
$ git commit -S -m "Wiki update"
$ git push origin master
$ cd ..
$ git add wiki
$ git commit -S -m "Link wiki commit to this commit of E2SAR"
$ git push origin <appropriate E2SAR branch>
```
Note that wiki uses `master` as its main branch, while docs/ and main E2SAR repo use `main`. 

## Related information

- [UDPLBd repo](https://github.com/esnet/udplbd) (aka Control Plane)
- [ejfat-rs repo](https://github.com/esnet/ejfat-rs) (command-line tool for testing)
- [Integrating with EJFAT](https://docs.google.com/document/d/1aUju_pWtHpS0Coesu8dC7HP6LbuKBJZqRYAMSSBtpWQ/edit#heading=h.zbhmzz3u1sna) document
