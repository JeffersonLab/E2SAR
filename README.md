# E2SAR
The internal dev repo of E2SAR.

## Documentation

Documentation is contained in the [wiki](JeffersonLab/E2SAR/wiki).

## Checking out the code

### Via cloning

All binary artifacts are stored using Git LFS (and you must [install git lfs](https://docs.github.com/en/repositories/working-with-files/managing-large-files/installing-git-large-file-storage?platform=linux) in order to properly check out their contents).

Clone the project as usual, then `cd` into the cloned directory and execute `git submodule init` to get the [UDPLBd](https://github.com/esnet/udplbd) repo contents (needed for the protobuf definitions located in udplbd/pkg/pb). 

If you want to update to the latest udplbd then also execute `git submodule update`. Note that you may need the correct branch of this project and as of this writing the is the `develop` branch and not `main`. You can do that by `cd udplbd && git fetch && get switch <branch>`.

## Building the code

Overview: the build process requires multiple dependencies for tools and code (gRPC+protobufs, boost and their dependencies). E2SAR uses Meson for a build tool. Also the protoc compiler is required to generate gRPC client stubs.

### Pre-requisistes

#### Installing dependencies

Build dependences

- MacOS: `brew install autoconf automake libtool shtool meson abseil c-ares re2 grpc pkg-config boost`
- Linux: `sudo apt-get -yq install python3-pip build-essential autoconf cmake libtool pkg-config libglib2.0-dev libboost-all-dev ninja-build openssl libssl-dev libsystemd-dev protobuf-compiler libre2-dev; pip3 install --user meson`

Install [`protoc` compiler](https://grpc.io/docs/protoc-installation/)
- MacOS: `brew install protoc`
- Linux: `sudo apt-get -yq install protobuf-compiler`

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
Then meson should be able to find everything. You can always test by doing e.g. `pkg-config --cflags grpc++`. 

##### Linux (Ubuntu 22)
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
Then meson should be able to find everything. You can always test by doing e.g. `pkg-config --cflags grpc++`. 

#### Install Boost from source

Use [this procedure](https://www.boost.io/doc/user-guide/getting-started.html) to build Boost from scratch (particularly on older Linux systems, like ubuntu 22).

When using it with meson be sure to set `BOOST_ROOT` to wherever it is installed (like e.g. `export BOOST_ROOT=/home/ubuntu/boost-install`) as per [these instructions](https://mesonbuild.com/Dependencies.html#boost).

### Building base libe2sar library

Make sure Meson is installed (with Ninja backend). The build should work for both LLVM/CLang and g++ compilers.

```
$ meson setup build
$ cd build
$ meson compile
$ meson test
```

### Building python bindings

TBD

# Related information

- [UDPLBd repo](https://github.com/esnet/udplbd) (aka Control Plane)
- [ejfat-rs repo](https://github.com/esnet/ejfat-rs) (command-line tool for testing)
- [Integrating with EJFAT](https://docs.google.com/document/d/1aUju_pWtHpS0Coesu8dC7HP6LbuKBJZqRYAMSSBtpWQ/edit#heading=h.zbhmzz3u1sna) document


