executable('lbadm', 'lbadm.cpp', 
            include_directories: inc,
            link_with: libe2sar,
            install : true,
	    link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])
executable('lbmon', 'lbmonitor.cpp', 
            include_directories: inc,
            link_with: libe2sar,
            install : true,
	    link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

executable('e2sar_perf', 'e2sar_perf.cpp',
            include_directories: inc,
            link_with: libe2sar,
            install : true,
	    link_args: linker_flags,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])    
