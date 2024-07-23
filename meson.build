project('E2SAR', 'cpp', 
        version: '0.1.0a1', default_options : ['cpp_std=c++17'])

add_project_arguments(f'-DE2SAR_VERSION="' + meson.project_version() + '"', language:['cpp'])

# -Wall
warning_level=1

# Dependencies that can be installed by package tools such as brew, apt, dnf etc.
glib_dep = dependency('glib-2.0')
protobuf_dep = dependency('protobuf')
thread_dep = dependency('threads')
# only needed for older compilers
cpp = meson.get_compiler('cpp')
stdcppfs_dep = cpp.find_library('stdc++fs', required: false)

# On many platforms, gRPC C++ has to be manually installed and found.
grpc_dep = dependency('grpc++', version: '>=1.54.1')

# boost also tends to lag so a source-based installation is preferred
# it also doesn't produce a .pc file - only cmake-based configuration is possible
# and meson can ONLY find it via BOOST_ROOT env variable
# (i.e. setting cmake_prefix_path doesn't work)
boost_dep = dependency('boost', modules: ['system', 'url', 'program_options', 'chrono', 'thread'], method: 'cmake', version: '>=1.83.0')

# we include 'src' because auto-generated headers for gRPC stubs go there 
inc = include_directories('include', 'src')

# Make sure find the one at ${GRPC_ROOT}/bin. /usr/bin/protoc may come with lower version.
# protobuf_include_path = protobuf_dep.get_variable(pkgconfig : 'includedir')
# protobuf_bin_path = join_paths(protobuf_include_path, '..', 'bin')
# protoc_cmd = find_program('protoc', required: true, dirs : [protobuf_bin_path], version : '>=3.21.12')
protoc_cmd = find_program('protoc', required: true, version : '>=3.21.12')

grpc_cpp_plugin_cmd = find_program('grpc_cpp_plugin', required: true)

# gRPC definitions
lb_proto_dir = '../udplbd/pkg/pb'
lb_proto = lb_proto_dir + '/loadbalancer.proto'

# subdirectories
subdir('include')
subdir('src')
subdir('test')
subdir('bin')
subdir('scripts')

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries : libe2sar,
                 version : '0.1',
                 name : 'libe2sar',
                 filebase : 'e2sar',
                 description : 'E2SAR: EJ-FAT Event Segmentation And Reassembly library')
