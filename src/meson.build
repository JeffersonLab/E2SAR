subdir('grpc')

e2sar_sources = ['e2sarUtil.cpp', 'e2sarCP.cpp',
    'e2sarDPSegmenter.cpp', 'e2sarDPReassembler.cpp',
    'e2sarNetUtil.cpp', 'e2sarAffinity.cpp']

# Extract just the header files from custom targets to create build dependency
# Index 0 is the .h file in each custom target's output list
grpc_generated_headers = [lb_grpc_ch[0], lb_pb_ch[0]]

# Exportable dependency: forces grpc codegen to complete before any consumer's
# compile step begins.  Add to dependencies: in every executable target that
# (even transitively) includes the generated protobuf/grpc headers.
grpc_hdr_dep = declare_dependency(sources: grpc_generated_headers)

libe2sar_t = static_library('e2sar_t',
                        e2sar_sources + grpc_generated_headers,
                        include_directories : inc,
                        dependencies : [grpc_dep, boost_dep, protobuf_dep],
                        install : true)

# build one library from the E2SAR and gRPC library pieces
libe2sar = static_library('e2sar',
                        objects : [libe2sar_t.extract_all_objects(recursive: false),
                                   liblbgrpc.extract_all_objects(recursive: false)],
			install : true)

# The pybind
subdir('pybind')

# High-level Python package (pure Python ergonomics layer)
subdir('python')
