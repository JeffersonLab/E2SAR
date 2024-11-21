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
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_getUseTls
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setInstanceToken
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setInstanceToken
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setSessionToken
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSessionToken
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getSessionToken
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getSessionToken
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getAdminToken
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getAdminToken
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setLbName
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setLbName
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setLbid
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setLbid
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setSessionId
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSessionId
  (JNIEnv *, jobject, jlong, jstring);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setSyncAddr
 * Signature: (JLjava/net/InetSocketAddress;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSyncAddr
  (JNIEnv *, jobject, jlong, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    setDataAddr
 * Signature: (JLjava/net/InetSocketAddress;)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setDataAddr
  (JNIEnv *, jobject, jlong, jobject);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getLbName
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getLbName
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getLbid
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getLbid
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getSessionId
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getSessionId
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getCpAddr
 * Signature: (J)Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getCpAddr
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasDataAddrv4
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddrv4
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasDataAddrv6
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddrv6
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasDataAddr
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddr
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    hasSyncAddr
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasSyncAddr
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getDataAddrv4
 * Signature: (J)Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getDataAddrv4
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getDataAddrv6
 * Signature: (J)Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getDataAddrv6
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    getSyncAddr
 * Signature: (J)Ljava/net/InetSocketAddress;
 */
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getSyncAddr
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    toString
 * Signature: (JI)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_toString
  (JNIEnv *, jobject, jlong, jint);

/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    freeNativePointer
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_freeNativePointer
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif
