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
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep, stdcppfs_dep])

e2sar_lbcp_live_test = executable('e2sar_lbcp_live_test', 'e2sar_lbcp_live_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

e2sar_sync_test = executable('e2sar_sync_test', 'e2sar_sync_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

e2sar_sync_live_test = executable('e2sar_sync_live_test', 'e2sar_sync_live_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

e2sar_seg_test = executable('e2sar_seg_test', 'e2sar_seg_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

e2sar_seg_live_test = executable('e2sar_seg_live_test', 'e2sar_seg_live_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

e2sar_reas_live_test = executable('e2sar_reas_live_test', 'e2sar_reas_live_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])

e2sar_reas_test = executable('e2sar_reas_test', 'e2sar_reas_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])        

# these tests have conditional compilation and may be NOOPs on non-linux platforms
e2sar_netutil_test = executable('e2sar_netutil_test', 'e2sar_netutil_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep, protobuf_dep])   

# these are standalone unit tests with no dependencies
test('URITests', e2sar_uri_test, suite: 'unit')
test('LBCPTests', e2sar_lbcp_test, suite: 'unit')
test('DPSyncTests', e2sar_sync_test, suite: 'unit')
test('DPSegTests', e2sar_seg_test, suite: 'unit')
test('DPReasTests', e2sar_reas_test, suite: 'unit')
test('NetUtilTests', e2sar_netutil_test, suite: 'unit')
# these tests require a live instance of control plane
test('LBCPLiveTests', e2sar_lbcp_live_test, suite: 'live')
test('DPSyncLiveTests', e2sar_sync_live_test, suite: 'live')
test('DPSegLiveTests', e2sar_seg_live_test, suite: 'live')
test('DPReasLiveTests', e2sar_reas_live_test, suite: 'live')

