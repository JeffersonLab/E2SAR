project('E2SAR library', 'cpp', 
        version: '0.1', default_options : ['cpp_std=c++17'])

# -Wall
warning_level=1

# Dependencies that can be installed by package tools such as brew, apt, dnf etc.
glib_dep = dependency('glib-2.0')
protobuf_dep = dependency('protobuf')
thread_dep = dependency('threads')

# On many platforms, gRPC C++ has to be manually installed and found.
grpc_dep = dependency('grpc++', version: '>=1.54.1', required: false)

# boost also tends to lag so a source-based installation is preferred
# it also doesn't produce a .pc file - only cmake-based configuration is possible
# and meson can ONLY find it via BOOST_ROOT env variable
# (i.e. setting cmake_prefix_path doesn't work)
boost_dep = dependency('boost', modules: ['system', 'url', 'program_options'], method: 'cmake', version: '>=1.83.0')

# we include 'src' because auto-generated headers for gRPC stubs go there 
inc = include_directories('include', 'src')

protoc_cmd = find_program('protoc', required: true)
grpc_cpp_plugin_cmd = find_program('grpc_cpp_plugin', required: true)

# gRPC definitions
lb_proto_dir = '../udplbd/pkg/pb'
lb_proto = lb_proto_dir + '/loadbalancer.proto'

# subdirectories
subdir('include')
subdir('src')
subdir('test')
subdir('bin')

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries : libe2sar,
                 version : '0.1',
                 name : 'libe2sar',
                 filebase : 'e2sar',
                 description : 'E2SAR: EJ-FAT Event Segmentation And Reassembly library')
