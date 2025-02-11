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
 * Signature: (JLjava/lang/String;Ljava/lang/String;Ljava/util/List;)I
 */
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__JLjava_lang_String_2Ljava_lang_String_2Ljava_util_List_2
  (JNIEnv *, jobject, jlong, jstring, jstring, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    reserveLB
 * Signature: (JLjava/lang/String;DLjava/util/List;)I
 */
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__JLjava_lang_String_2DLjava_util_List_2
  (JNIEnv *, jobject, jlong, jstring, jdouble, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getLB
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_getLB__JLjava_lang_String_2
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getLB
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_getLB__J
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getStatus
 * Signature: (J)Lorg/jlab/hpdf/messages/LBStatus;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getStatus__J
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getStatus
 * Signature: (JLjava/lang/String;)Lorg/jlab/hpdf/messages/LBStatus;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getStatus__JLjava_lang_String_2
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getOverview
 * Signature: (J)Ljava/util/List;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getOverview
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    addSenders
 * Signature: (JLjava/util/List;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_addSenders
  (JNIEnv *, jobject, jlong, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    removeSenders
 * Signature: (JLjava/util/List;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_removeSenders
  (JNIEnv *, jobject, jlong, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    freeLB
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeLB__JLjava_lang_String_2
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    freeLB
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeLB__J
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    registerWorker
 * Signature: (JLjava/lang/String;Ljava/lang/String;IFIFF)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_registerWorker
  (JNIEnv *, jobject, jlong, jstring, jstring, jint, jfloat, jint, jfloat, jfloat);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    deregisteWorker
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_deregisteWorker
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    sendState
 * Signature: (JFFZ)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_sendState__JFFZ
  (JNIEnv *, jobject, jlong, jfloat, jfloat, jboolean);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    sendState
 * Signature: (JFFZLjava/time/Instant;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_sendState__JFFZLjava_time_Instant_2
  (JNIEnv *, jobject, jlong, jfloat, jfloat, jboolean, jobject);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    version
 * Signature: (J)Ljava/util/List;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_version
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getAddrString
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_LbManager_getAddrString
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    getInternalUri
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_LbManager_getInternalUri
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_LbManager
 * Method:    freeNativePointer
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeNativePointer
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif