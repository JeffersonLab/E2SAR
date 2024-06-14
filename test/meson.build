e2sartest = executable('e2sar_test', 'e2sar_test.cpp',
                       include_directories : inc,
                       link_with : libe2sar,
                       dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

boosttest = executable('boost_test', 'boost_test.cpp',
                       include_directories: inc,
                       dependencies: [boost_dep, thread_dep, protobuf_dep])

e2sar_uri_test = executable('e2sar_uri_test', 'e2sar_uri_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, grpc_dep, protobuf_dep])

e2sar_lbcp_test = executable('e2sar_lbcp_test', 'e2sar_lbcp_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep],
                            link_args: ['-lstdc++fs'])

e2sar_lbcp_live_test = executable('e2sar_lbcp_live_test', 'e2sar_lbcp_live_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

# these are standalone unit tests with no dependencies
test('URITests', e2sar_uri_test, suite: 'unit')
test('LBCPTests', e2sar_lbcp_test, suite: 'unit')
# these tests require a live instance of control plane
test('LBCPLiveTests', e2sar_lbcp_live_test, suite: 'live')
