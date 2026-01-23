executable('lbadm', 'lbadm.cpp',
            include_directories: inc,
            link_with: libe2sar,
            install : true,
	        link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep] + absl_deps)
executable('lbmon', 'lbmonitor.cpp',
            include_directories: inc,
            link_with: libe2sar,
            install : true,
	        link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep] + absl_deps)

executable('e2sar_perf', 'e2sar_perf.cpp',
            include_directories: inc,
            link_with: libe2sar,
            install : true,
	        link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep] + absl_deps)

executable('e2sar_ft', 'e2sar_ft.cpp',
            include_directories: inc,
            link_with: libe2sar,
            install: true,
            link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep] + absl_deps)

executable('e2sar_udp_relay', 'e2sar_udp_relay.cpp',
            include_directories: inc,
            link_with: libe2sar,
            install: true,
            link_args: linker_flags,
            dependencies: [boost_dep, thread_dep])
