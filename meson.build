project('E2SAR', 'cpp',
        version: files('VERSION.txt'),
        default_options : [
                'buildtype=debugoptimized',
                'cpp_std=c++17',
                'b_asneeded=false'])

compiler = meson.get_compiler('cpp')

# adding -fsanitize if needed:
# you can add -fsanitize=address for address sanitizer or 
# -fsanitize=thread to look for race conditions. Need to be
# added to both compile and link arguments
#add_project_arguments('-fsanitize=address', language : 'cpp')
#add_project_link_arguments('-fsanitize=address', language : 'cpp')

if compiler.has_header('linux/netlink.h')
        add_project_arguments('-DNETLINK_CAPABLE', language: ['cpp'])
endif

# check for both kernel capability and liburing-dev presence
linker_flags = []
if compiler.has_header('liburing.h') and compiler.has_header('linux/io_uring.h')
        add_project_arguments('-DLIBURING_AVAILABLE', language: ['cpp'])
	linker_flags += ['-luring']
endif

numacode = '''
#include <numa.h>

void f() {
  int res = numa_available();
}
'''

if compiler.compiles(numacode, name: 'NUMA library check')
        add_project_arguments('-DNUMA_AVAILABLE', language: ['cpp'])
        linker_flags += ['-lnuma']
endif

affcode = '''
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
void f() {
cpu_set_t set;
sched_setaffinity(getpid(), sizeof(set), &set); }
'''

if compiler.compiles(affcode, name: 'CPU Affinity check')
        add_project_arguments('-DAFFINITY_AVAILABLE', language: ['cpp'])
endif

thaffcode = '''
#define _GNU_SOURCE
#include <pthread.h>
void f() {
cpu_set_t set;
pthread_t thread = pthread_self();
pthread_setaffinity_np(thread, sizeof(set), &set); }
'''

if compiler.compiles(thaffcode, name: 'Thread Affinity check')
        add_project_arguments('-DTHRD_AFFINITY_AVAILABLE', language: ['cpp'])
endif

mmsgcode = '''
#define _GNU_SOURCE
#include <sys/socket.h>
#include <stddef.h>
void f() {
  int ret = sendmmsg(0, NULL, 1, 0);
}
'''

if compiler.compiles(mmsgcode, name: 'sendmmsg check')
        add_project_arguments('-DSENDMMSG_AVAILABLE', language: ['cpp'])
endif

add_project_arguments(f'-DE2SAR_VERSION="' + meson.project_version() + '"', language:['cpp'])

# Ensure all shared libraries are linked - b_asneeded=false in project options
# disables meson's automatic --as-needed, and this explicit flag is a fallback.
# This fixes Debian linking issues with Abseil/Protobuf transitive dependencies
# where template instantiations in gRPC/Protobuf headers need symbols from
# abseil libraries that would otherwise be dropped by the linker.
if host_machine.system() == 'linux'
    add_project_link_arguments('-Wl,--no-as-needed', language: 'cpp')
endif

# -Wall
warning_level=1
#add_project_arguments('-Wall -Wno-deprecated-declarations', language: ['cpp'])

# Dependencies that can be installed by package tools such as brew, apt, dnf etc.
glib_dep = dependency('glib-2.0')
protobuf_dep = dependency('protobuf')
thread_dep = dependency('threads')
# only needed for older compilers
stdcppfs_dep = compiler.find_library('stdc++fs', required: false)

# On many platforms, gRPC C++ has to be manually installed and found.
grpc_dep = dependency('grpc++', version: '>=1.74.1')

# Explicitly declare Abseil dependencies to ensure the versions from the
# dependency package are used, not system packages. This is critical on Debian
# where system Abseil may have a different ABI version than the one bundled
# with grpc/protobuf in the dependency package.
absl_deps = []
foreach absl_mod : ['absl_base', 'absl_strings', 'absl_cord', 'absl_log',
                    'absl_status', 'absl_cord_internal', 'absl_log_internal_message']
    dep = dependency(absl_mod, required: false)
    if dep.found()
        absl_deps += dep
    endif
endforeach

# boost also tends to lag so a source-based installation is preferred
# it also doesn't produce a .pc file - only cmake-based configuration is possible
# and meson can ONLY find it via BOOST_ROOT env variable
# (i.e. setting cmake_prefix_path doesn't work)
boost_dep = dependency('boost', modules: ['url', 'program_options', 'chrono', 'thread', 'filesystem', 'log'], method: 'cmake', 
        version: ['>=1.89.0', '<=1.89.0'])

# we include 'src' because auto-generated headers for gRPC stubs go there 
inc = include_directories('include', 'src')

# Make sure find the one at ${GRPC_ROOT}/bin. /usr/bin/protoc may come with lower version.
# protobuf_include_path = protobuf_dep.get_variable(pkgconfig : 'includedir')
# protobuf_bin_path = join_paths(protobuf_include_path, '..', 'bin')
# protoc_cmd = find_program('protoc', required: true, dirs : [protobuf_bin_path], version : '>=3.21.12')
protoc_cmd = find_program('protoc', required: true, version : '>=3.21.12')

grpc_cpp_plugin_cmd = find_program('grpc_cpp_plugin', required: true)

# gRPC definitions
lb_proto_dir = '../udplbd2/proto/loadbalancer'
lb_proto = lb_proto_dir + '/loadbalancer.proto'

# subdirectories
subdir('include')
subdir('src')
subdir('test')
subdir('bin')
subdir('scripts')

pkg_mod = import('pkgconfig')
pkg_mod.generate(
    libraries : libe2sar,
    version : meson.project_version(),
    name : 'libe2sar',
    filebase : 'e2sar',
    description : 'E2SAR: EJ-FAT Event Segmentation And Reassembly library',
    requires : ['grpc++', 'protobuf'],
    libraries_private : [
        '-lboost_log',
        '-lboost_log_setup',
        '-lboost_thread',
        '-lboost_filesystem',
        '-lboost_program_options',
        '-lboost_url',
        '-lboost_chrono'
    ] + linker_flags)

summary({
  'prefix': get_option('prefix'),
}, section: 'Directories')

summary({
  'Build type': get_option('buildtype'),
  'C++ standard': get_option('cpp_std'),
}, section: 'Configuration')
