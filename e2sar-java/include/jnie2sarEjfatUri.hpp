#include <jni.h>
#include "jnie2sarHelper.hpp"
#include "e2sarUtil.hpp"

#ifndef _Included_org_jlab_hpdf_EjfatURI
#define _Included_org_jlab_hpdf_EjfatURI
#ifdef __cplusplus
extern "C" {
#endif

const std::string nativeEjfatUri = "nativeEjfatURI";
e2sar::EjfatURI* getEjfatUriFromField(JNIEnv *env, jobject jEjfatUri);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    initEjfatUri
 * Signature: (Ljava/lang/String;IZ)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_EjfatURI_initEjfatUri
  (JNIEnv *, jclass, jstring, jint, jboolean);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getUriFromFile
 * Signature: (Ljava/lang/String;IZ)J
 */
JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_EjfatURI_getUriFromFile
  (JNIEnv *, jclass, jstring, jint, jboolean);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getUseTls
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_getUseTls
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setInstanceToken
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setInstanceToken
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setSessionToken
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSessionToken
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getSessionToken
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getSessionToken
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getAdminToken
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getAdminToken
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setLbName
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setLbName
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setLbid
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setLbid
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setSessionId
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSessionId
  (JNIEnv *, jobject, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setSyncAddr
 * Signature: (Ljava/net/InetSocketAddress;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSyncAddr
  (JNIEnv *, jobject, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setDataAddr
 * Signature: (Ljava/net/InetSocketAddress;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setDataAddr
  (JNIEnv *, jobject, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getLbName
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getLbName
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getLbid
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getLbid
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getSessionId
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getSessionId
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getCpAddr
 * Signature: ()Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getCpAddr
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasDataAddrv4
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddrv4
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasDataAddrv6
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddrv6
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasDataAddr
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddr
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasSyncAddr
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasSyncAddr
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getDataAddrv4
 * Signature: ()Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getDataAddrv4
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getDataAddrv6
 * Signature: ()Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getDataAddrv6
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getSyncAddr
 * Signature: ()Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getSyncAddr
  (JNIEnv *, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    toString
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_toString
  (JNIEnv *, jobject, jint);

#ifdef __cplusplus
}
#endif
#endif
