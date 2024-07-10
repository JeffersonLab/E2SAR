# protoc -I ../../ersap-grpc --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` ../../ersap-grpc/loadbalancer.proto
# protoc -I ../../ersap-grpc --cpp_out=. ../../ersap-grpc/loadbalancer.proto

# build gRPC stub using .proto definition from UDPLBd repo
# FIXME: using '@INPUT@' doesnt work as meson prepends ../src/grpc to the name of the input file and protoc barfs
lb_grpc_ch = custom_target(
    'grpc_ch_id',
    output: ['loadbalancer.grpc.pb.h', 'loadbalancer.grpc.pb.cc'],
    command: [protoc_cmd, '-I', lb_proto_dir, '--grpc_out=src/grpc', '--plugin=protoc-gen-grpc=' + grpc_cpp_plugin_cmd.full_path(), lb_proto],
    install: false,
)

# build protobuf stub using .proto definition from UDPLBd repo
lb_pb_ch = custom_target(
    'pb_ch_id',
    output: ['loadbalancer.pb.h', 'loadbalancer.pb.cc'],
    command: [protoc_cmd, '-I', lb_proto_dir, '--cpp_out=src/grpc', lb_proto],
    install: false,
)

# build a static library from the stubs
liblbgrpc = static_library('lbgrpc', [lb_grpc_ch, lb_pb_ch], 
                           dependencies: [grpc_dep, protobuf_dep])

# install generated headers
meson.add_install_script('install.sh')

