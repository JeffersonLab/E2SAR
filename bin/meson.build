executable('lbadm', 'lbadm.cpp', 
            include_directories: inc,
            link_with: libe2sar,
            install : true,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])
executable('lbmon', 'lbmonitor.cpp', 
            include_directories: inc,
            link_with: libe2sar,
            install : true,
            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])