e2sartest = executable('e2sar_test', 'e2sar_test.cpp',
                       include_directories : inc,
                       link_with : libe2sar,
                       dependencies: [boost_dep, thread_dep, grpc_dep])

boosttest = executable('boost_test', 'boost_test.cpp',
                       include_directories: inc,
                       dependencies: [boost_dep, thread_dep])

e2sarboosttest = executable('e2sar_boost_test', 'e2sar_boost_test.cpp',
                            include_directories: inc,
                            link_with: libe2sar,
                            dependencies: [boost_dep, thread_dep, grpc_dep])

test('BasicTests', e2sarboosttest)