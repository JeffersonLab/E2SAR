project('E2SAR library', 'cpp', 
        version: '0.1', default_options : ['cpp_std=c++17'])

# -Wall
warning_level=1

# Dependencies that can be installed by package tools such as brew, apt, dnf etc.
glib_dep = dependency('glib-2.0')
protobuf_dep = dependency('protobuf')
thread_dep = dependency('threads')
boost_dep = dependency('boost')  # For RHEL, via xxx install boost-devel

# On RHEL platforms, gRPC C++ has to be manually installed and found.
# Meson dependency guide: https://mesonbuild.com/Dependencies.html
grpc_dep = dependency('grpc++', version: '>=1.60', required: false)
if not grpc_dep.found()
  # gRPC C++ building from source guide: https://github.com/grpc/grpc/blob/v1.62.0/BUILDING.md
  # TODO: link the path to some ENV vars or grpc++.pc
  grpc_dep = declare_dependency(
    include_directories: ['/usr/local/include/grpc++'],
    link_args: ['-L/usr/local/lib' , 'lgrpc++']
  )
  message('Declare grpc++ dependency manually')
endif

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
