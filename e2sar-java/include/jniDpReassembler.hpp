#include <jni.h>
#include "jnie2sarHelper.hpp"
#include "jnie2sarEjfatUri.hpp"
#include "e2sar.hpp"

/* Header for class org_jlab_hpdf_Reassembler */

#ifndef _Included_org_jlab_hpdf_Reassembler
#define _Included_org_jlab_hpdf_Reassembler
#ifdef __cplusplus
extern "C" {
#endif

const std::string javaReassemblerFlagsClass = "org/jlab/hpdf/config/ReassemblerFlags";
const std::string javaReassembledEventClass = "org/jlab/hpdf/messages/ReassembledEvent";
const std::string javaLostEventClass = "org/jlab/hpdf/messages/LostEvent";
const std::string javaRecvStatsClass = "org/jlab/hpdf/messages/RecvStats";

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    initReassembler
 * Signature: (Lorg/jlab/hpdf/EjfatURI;Ljava/net/InetAddress;ILjava/util/List;Lorg/jlab/hpdf/config/ReassemblerFlags;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2ILjava_util_List_2Lorg_jlab_hpdf_config_ReassemblerFlags_2
  (JNIEnv *, jobject, jobject, jobject, jint, jobject, jobject);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    initReassembler
 * Signature: (Lorg/jlab/hpdf/EjfatURI;Ljava/net/InetAddress;ILjava/util/List;Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2ILjava_util_List_2Ljava_lang_String_2
  (JNIEnv *, jobject, jobject, jobject, jint, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    initReassembler
 * Signature: (Lorg/jlab/hpdf/EjfatURI;Ljava/net/InetAddress;ILjava/util/List;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2ILjava_util_List_2
  (JNIEnv *, jobject, jobject, jobject, jint, jobject);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    initReassembler
 * Signature: (Lorg/jlab/hpdf/EjfatURI;Ljava/net/InetAddress;IJLorg/jlab/hpdf/config/ReassemblerFlags;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2IJLorg_jlab_hpdf_config_ReassemblerFlags_2
  (JNIEnv *, jobject, jobject, jobject, jint, jlong, jobject);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    initReassembler
 * Signature: (Lorg/jlab/hpdf/EjfatURI;Ljava/net/InetAddress;IJLjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2IJLjava_lang_String_2
  (JNIEnv *, jobject, jobject, jobject, jint, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    initReassembler
 * Signature: (Lorg/jlab/hpdf/EjfatURI;Ljava/net/InetAddress;IJ)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2IJ
  (JNIEnv *, jobject, jobject, jobject, jint, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    registerWorker
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_registerWorker
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    deregisterWorker
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_deregisterWorker
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    openAndStart
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_openAndStart
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    getEvent
 * Signature: (J)Ljava/util/Optional;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getEvent
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    recvEvent
 * Signature: (JJ)Ljava/util/Optional;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_recvEvent
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    getStats
 * Signature: (J)Lorg/jlab/hpdf/messages/RecvStats;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getStats
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    getLostEvent
 * Signature: (J)Ljava/util/Optional;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getLostEvent
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    getNumRecvThreads
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_getNumRecvThreads
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    getRecvPorts
 * Signature: (J)Ljava/util/List;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getRecvPorts
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    getPortRange
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_Reassembler_getPortRange
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_Reassembler
 * Method:    freeNativePointer
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_freeNativePointer
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif