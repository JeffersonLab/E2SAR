subdir('grpc')

e2sar_sources = ['e2sarUtil.cpp', 'e2sarCP.cpp',
    'e2sarDPSegmenter.cpp', 'e2sarDPReassembler.cpp',
    'e2sarNetUtil.cpp', 'e2sarAffinity.cpp']

# Extract just the header files from custom targets to create build dependency
# Index 0 is the .h file in each custom target's output list
grpc_generated_headers = [lb_grpc_ch[0], lb_pb_ch[0]]

e2sar_t_deps = [grpc_dep, boost_dep, protobuf_dep]
if enable_fec
    e2sar_t_deps += [fec_interleaver_dep]
endif

libe2sar_t = static_library('e2sar_t',
                        e2sar_sources + grpc_generated_headers,
                        include_directories : inc,
                        dependencies : e2sar_t_deps,
                        install : true)

# build one library from the E2SAR and gRPC library pieces
libe2sar_objects = [libe2sar_t.extract_all_objects(recursive: false),
                    liblbgrpc.extract_all_objects(recursive: false)]
if enable_fec
    libe2sar_objects += [libfec_interleaver.extract_all_objects(recursive: false),
                         libfec_rs_decode_c.extract_all_objects(recursive: false)]
endif

libe2sar = static_library('e2sar',
                        objects : libe2sar_objects,
			install : true)

# The pybind
subdir('pybind')
