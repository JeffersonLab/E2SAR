#include <jni.h>
#include "jnie2sarHelper.hpp"
#include "e2sarUtil.hpp"

#ifndef _Jni_e2sar_ejfatUri
#define _Jni_e2sar_ejfatUri

e2sar::EjfatURI parseJavaUri(JNIEnv *env, jobject ejfatUri);
  
#endif