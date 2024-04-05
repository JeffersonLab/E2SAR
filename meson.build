project('E2SAR library', 'cpp', 
        version: '0.1', default_options : ['cpp_std=c++17'])

# -Wall
warning_level=1

# some dependencies
glib_dep = dependency('glib-2.0')
grpc_dep = dependency('grpc++', version: '>=1.60')
protobuf_dep = dependency('protobuf')
thread_dep = dependency('threads')
boost_dep = dependency('boost')

inc = include_directories('include')
protoc_cmd = find_program('protoc', required: true)
grpc_cpp_plugin_cmd = find_program('grpc_cpp_plugin', required: true)

# gRPC definitions
lb_proto_dir = '../udplbd/pkg/pb'
lb_proto = lb_proto_dir + '/loadbalancer.proto'

# subdirectories
subdir('include')
subdir('src')
subdir('test')

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries : libe2sar,
                 version : '0.1',
                 name : 'libe2sar',
                 filebase : 'e2sar',
                 description : 'E2SAR: EJ-FAT Event Segmentation And Reassembly library')
