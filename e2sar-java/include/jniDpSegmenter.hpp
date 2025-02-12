
#include <jni.h>
#include "jnie2sarHelper.hpp"
#include "jnie2sarEjfatUri.hpp"
#include "e2sar.hpp"
/* Header for class org_jlab_hpdf_Segmenter */

#ifndef _Included_org_jlab_hpdf_Segmenter
#define _Included_org_jlab_hpdf_Segmenter
#ifdef __cplusplus
extern "C" {
#endif

const std::string javaSegmenterFlagsClass = "org/jlab/hpdf/config/SegmenterFlags";
const std::string javaSyncStatsClass = "org/jlab/hpdf/messages/SyncStats";
const std::string javaSendStatsClass = "org/jlab/hpdf/messages/SendStats";
/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    initSegmentor
 * Signature: (Lorg/jlab/hpdf/EjfatURI;IJLorg/jlab/hpdf/config/SegmenterFlags;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_initSegmentor__Lorg_jlab_hpdf_EjfatURI_2IJLorg_jlab_hpdf_config_SegmenterFlags_2
  (JNIEnv *, jobject, jobject, jint, jlong, jobject);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    initSegmentor
 * Signature: (Lorg/jlab/hpdf/EjfatURI;IJLjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_initSegmentor__Lorg_jlab_hpdf_EjfatURI_2IJLjava_lang_String_2
  (JNIEnv *, jobject, jobject, jint, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    initSegmentor
 * Signature: (Lorg/jlab/hpdf/EjfatURI;IJ)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_initSegmentor__Lorg_jlab_hpdf_EjfatURI_2IJ
  (JNIEnv *, jobject, jobject, jint, jlong);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    openAndStart
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_openAndStart
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    sendEventDirect
 * Signature: (JLjava/nio/ByteBuffer;IJII)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_sendEventDirect
  (JNIEnv *, jobject, jlong, jobject, jint, jlong, jint, jint);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    addToSendQueueDirect
 * Signature: (JLjava/nio/ByteBuffer;IJII)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_addToSendQueueDirect
  (JNIEnv *, jobject, jlong, jobject, jint, jlong, jint, jint);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    getMTU
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_Segmenter_getMTU
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    getMaxPayloadLength
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_getMaxPayloadLength
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    getSyncStats
 * Signature: (J)Lorg/jlab/hpdf/messages/SyncStats;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Segmenter_getSyncStats
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    getSendStats
 * Signature: (J)Lorg/jlab/hpdf/messages/SendStats;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Segmenter_getSendStats
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Segmenter
 * Method:    freeNativePointer
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_freeNativePointer
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif
