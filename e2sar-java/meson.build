#### JNI module

jni_dep = dependency('jni', required: true)

e2sar_jni_sources = files(
  'src/cpp/jnie2sar.cpp',
  'src/cpp/jnie2sarHelper.cpp',
  'src/cpp/jnie2sarEjfatUri.cpp',
  'src/cpp/jniLbManager.cpp',
)

include_dirs = include_directories('include')

jnie2sar = shared_library('jnie2sar',
  e2sar_jni_sources, 
  include_directories: [inc, include_dirs],
  dependencies: [jni_dep, boost_dep, grpc_dep, protobuf_dep, thread_dep],
  link_with: libe2sar, 
  install: true
)


