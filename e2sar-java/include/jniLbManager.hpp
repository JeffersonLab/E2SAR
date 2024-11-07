#include "jnie2sarHelper.hpp"
#include "jnie2sarEjfatUri.hpp"
#include "e2sar.hpp"

#ifndef _Included_org_jlab_hpdf_LbManager
#define _Included_org_jlab_hpdf_LbManager
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    initLbManager
 * Signature: (Lorg/jlab/hpdf/EjfatURI;ZZ[Ljava/lang/String;Z)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_LbManager_initLbManager
  (JNIEnv *, jobject, jobject, jboolean, jboolean, jobjectArray, jboolean);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    reserveLB
 * Signature: (Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2Ljava_lang_String_2_3Ljava_lang_String_2
  (JNIEnv *, jobject, jstring, jstring, jobjectArray);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    reserveLB
 * Signature: (Ljava/lang/String;D[Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2D_3Ljava_lang_String_2
  (JNIEnv *, jobject, jstring, jdouble, jobjectArray);  

#ifdef __cplusplus
}
#endif
#endif