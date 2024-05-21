# E2SAR
The internal dev repo of E2SAR.

## Documentation

Documentation is contained in the [wiki](JeffersonLab/E2SAR/wiki).

## Checking out the code

### Via cloning

Clone the project as usual, then `cd` into the cloned directory and execute `git submodule init` to get the [UDPLBd](https://github.com/esnet/udplbd) repo contents (needed for the protobuf definitions located in udplbd/pkg/pb). Note that the submodule is added via `https://` URL not `git://` URL and is not meant to be modified even if developing E2SAR.

If you want to update to the latest udplbd then also execute `git submodule update`. Note that you may need the correct branch of this project and as of this writing the is the `develop` branch and not `main`. You can do that by `cd udplbd && git fetch && get switch <branch>`.

## Building the code

Overview: the build process requires multiple dependencies for tools and code (gRPC+protobufs, boost and their dependencies). E2SAR uses Meson for a build tool. Also the protoc compiler is required to generate gRPC client stubs.

### Pre-requisistes

- Tool dependencies: 
    - MacOS: `brew install autoconf automake libtool shtool meson abseil c-ares re2 grpc pkg-config boost`
    - Linux: `sudo apt-get -yq install python3-pip build-essential autoconf cmake libtool pkg-config libglib2.0-dev libboost-all-dev ninja-build openssl libssl-dev libsystemd-dev protobuf-compiler libre2-dev`
- [Install `protoc` compiler](https://grpc.io/docs/protoc-installation/)
    - MacOS: `brew install protoc`
    - Linux: `sudo apt-get -yq install protobuf-compiler`
- Only if installing gRPC from source: [build and Install gRPC C++ library](https://grpc.io/blog/installation/)
    - Clone the [gRPC repo](https://github.com/grpc/grpc/tree/master) somewhere 
    - Build
        - MacOS:
```
$ mkdir -p cmake/build && cd cmake/build
$ cmake ../.. -DgRPC_INSTALL=ON                \
              -DCMAKE_INSTALL_PREFIX=/some/local/path/to/avoid/version/clashes/or/need/for/sudo/like/home/user/lib \
              -DCMAKE_BUILD_TYPE=Release       \
              -DgRPC_ABSL_PROVIDER=package     \
              -DgRPC_CARES_PROVIDER=package    \
              -DgRPC_PROTOBUF_PROVIDER=package \
              -DgRPC_RE2_PROVIDER=package      \
              -DgRPC_SSL_PROVIDER=package      \
              -DgRPC_ZLIB_PROVIDER=package
$ make
$ make install
```
        - Linux (Ubuntu 22)
            - Follow instructions in [this page](https://grpc.io/docs/languages/cpp/quickstart/) using appropriate version tag/branch that matches UDPLBd dependencies.
            - Use the following command to build:
```
$ cmake -DgRPC_INSTALL=ON  \
        -DgRPC_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX=/home/ubuntu/grpc-install \
        -DBUILD_SHARED_LIBS=ON ../..
```
            - After you do `make -j <n>` and `make install` to the location you need to tell various parts of the build system where to find gRPC++:
```
$ export LD_LIBRARY_PATH=/home/ubuntu/grpc-install/lib/
$ export PATH=~/grpc-install/bin/:$PATH   
$ export PKG_CONFIG_PATH=/home/ubuntu/grpc-install/lib/pkgconfig/
```
            - Then meson should be able to find everything. You can always test by doing e.g. `pkg-config --cflags grpc++`. 

### Building base libe2sar library

Make sure Meson is installed (with Ninja backend). The build should work for both LLVM/CLang and g++ compilers.

```
$ meson setup builddir
$ cd builddir
$ meson compile
```

### Building python bindings


# Related information

- [UDPLBd repo](https://github.com/esnet/udplbd) (aka Control Plane)
- [ejfat-rs repo](https://github.com/esnet/ejfat-rs) (command-line tool for testing)
- [Integrating with EJFAT](https://docs.google.com/document/d/1aUju_pWtHpS0Coesu8dC7HP6LbuKBJZqRYAMSSBtpWQ/edit#heading=h.zbhmzz3u1sna) document


