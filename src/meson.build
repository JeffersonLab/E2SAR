subdir('grpc')

e2sar_sources = ['e2sar.cpp', 'cp-api.cpp', 'e2sarUtil.cpp', 'e2sarCP.cpp']

libe2sar_t = static_library('e2sar_t',
                        e2sar_sources,
                        include_directories : inc,
                        dependencies : [grpc_dep, boost_dep],
                        install : true)

# build one library from the E2SAR and gRPC library pieces
libe2sar = static_library('e2sar',
                        objects : [libe2sar_t.extract_all_objects(recursive: false),
                                   liblbgrpc.extract_all_objects(recursive: false)])
                                   