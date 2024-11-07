#include <jni.h>
#include <iostream>
#include "jnie2sarHelper.hpp"
#include "e2sar.hpp"

#ifndef _Jni_e2sar
#define _Jni_e2sar

/*
 * Class:     org_jlab_hpdf_E2sarUtil
 * Method:    getE2sarVersion
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_E2sarUtil_getE2sarVersion
  (JNIEnv *, jclass);
  
#endif
