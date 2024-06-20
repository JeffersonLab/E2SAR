# E2SAR
The internal dev repo of E2SAR.

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

- MacOS: `brew install autoconf automake libtool shtool meson abseil c-ares re2 grpc pkg-config boost`
- Linux: `sudo apt-get -yq install python3-pip build-essential autoconf cmake libtool pkg-config libglib2.0-dev libboost-all-dev ninja-build openssl libssl-dev libsystemd-dev protobuf-compiler libre2-dev; pip3 install --user meson`

Install [`protoc` compiler](https://grpc.io/docs/protoc-installation/)
- MacOS: `brew install protoc`
- Linux: `sudo apt-get -yq install protobuf-compiler`

For python dependencies minimum Python 3.11 is required. It is recommended you create a virtual environment for Python build dependencies, then activate the venv and install pybind11 and other dependencies:

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
The `setup_compile_env.sh` script sets it up (below). Then meson should be able to find everything. You can always test by doing e.g. `pkg-config --cflags grpc++`. 

#### Install Boost from source

Use [this procedure](https://www.boost.io/doc/user-guide/getting-started.html) to build Boost from scratch (particularly on older Linux systems, like ubuntu 22).

When using it with meson be sure to set `BOOST_ROOT` to wherever it is installed (like e.g. `export BOOST_ROOT=/home/ubuntu/boost-install`) as per [these instructions](https://mesonbuild.com/Dependencies.html#boost).

### Building base libe2sar library

Make sure Meson is installed (with Ninja backend). The build should work for both LLVM/CLang and g++ compilers.

Update `setup_compile_env.sh` file for your environment. Then run:

```
$ . ./setup_compile_env.sh
$ meson setup build
$ cd build
$ meson compile
$ meson test
```
If you desire a custom installation directory you can add `--prefix=/absolute/path/to/install/root`. If you have a custom location for pkg-config scripts, you can also add `-Dpkg_config_path=/path/to/pkg-config/scripts` to the setup command. 

### Building python bindings

TBD

# Testing

## C++

E2SAR code comes with a set of tests under [test/](test/) folder. It relies on Boost unit-testing framework as well as meson testing capabilities. The easiest way is to execute `meson test` or `meson test --suite unit` or `meson test --suite live`. The latter requires an instance of UDPLBd running and `EJFAT_URI` environment variable to be set to point to it (e.g. `export EJFAT_URI="ejfats://udplbd@192.168.0.3:18347/").

There is a  [Jupyter notebook](scripts/notebooks/EJFAT/LBCP-tester.ipynb) which runs all the tests on FABRIC testbed.

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
