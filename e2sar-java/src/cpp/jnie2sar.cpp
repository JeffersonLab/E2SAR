#include "jnie2sar.hpp"


/*
 * Class:     org_jlab_hpdf_EjfatURI
 * Method:    createEjfatURI
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_EjfatURI_createEjfatURI
  (JNIEnv* env, jobject callObj, jstring uri){
        jboolean isCopy;
        const char *convertedValue = (env)->GetStringUTFChars(uri, &isCopy);
        std::string string = convertedValue;
        e2sar::EjfatURI* ejfat_uri = new e2sar::EjfatURI(string);
        std::cout << "Created ejfatUri" << std::endl;
        return (jlong)ejfat_uri;
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_E2sarUtil_getE2sarVersion
  (JNIEnv *env, jclass jCallObj){
      return env->NewStringUTF(e2sar::get_Version().data());
  }




