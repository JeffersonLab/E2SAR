#include "jnie2sarHelper.hpp"
#include "jnie2sarEjfatUri.hpp"
#include "e2sar.hpp"

#ifndef _Included_org_jlab_hpdf_LbManager
#define _Included_org_jlab_hpdf_LbManager
#ifdef __cplusplus
extern "C" {
#endif
const std::string nativeLbField = "nativeLbManager";
const std::string javaWorkerStatusClass = "org/jlab/hpdf/messages/WorkerStatus";
const std::string javaLBStatusClass = "org/jlab/hpdf/messages/LBStatus";
const std::string javaLBOverviewClass = "org/jlab/hpdf/messages/LBOverview";
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
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2Ljava_lang_String_2Ljava_util_List_2
  (JNIEnv *, jobject, jstring, jstring, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    reserveLB
 * Signature: (Ljava/lang/String;D[Ljava/lang/String;)V
 */
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2DLjava_util_List_2
  (JNIEnv *, jobject, jstring, jdouble, jobject);   

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getLB
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_getLB__Ljava_lang_String_2
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getLB
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_getLB__
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getStatus
 * Signature: ()Lorg/jlab/hpdf/messages/LBStatus;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getStatus__
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getStatus
 * Signature: (Ljava/lang/String;)Lorg/jlab/hpdf/messages/LBStatus;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getStatus__Ljava_lang_String_2
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getOverview
 * Signature: ()Lorg/jlab/hpdf/messages/LBOverview;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getOverview
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    addSenders
 * Signature: (Ljava/util/List;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_addSenders
  (JNIEnv *, jobject, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    removeSenders
 * Signature: (Ljava/util/List;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_removeSenders
  (JNIEnv *, jobject, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    freeLB
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeLB__Ljava_lang_String_2
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    freeLB
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeLB__
  (JNIEnv *, jobject);


/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    registerWorker
 * Signature: (Ljava/lang/String;Ljava/net/InetSocketAddress;FIFF)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_registerWorker
  (JNIEnv *, jobject, jstring, jstring, jint, jfloat, jint, jfloat, jfloat);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    deregisteWorker
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_deregisteWorker
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    sendState
 * Signature: (FFZ)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_sendState__FFZ
  (JNIEnv *, jobject, jfloat, jfloat, jboolean);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    sendState
 * Signature: (FFZLjava/time/Instant;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_sendState__FFZLjava_time_Instant_2
  (JNIEnv *, jobject, jfloat, jfloat, jboolean, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    version
 * Signature: ()Ljava/util/List;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_version
  (JNIEnv *, jobject);   

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getAddrString
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_LbManager_getAddrString
  (JNIEnv *, jobject);

#ifdef __cplusplus
}
#endif
#endif