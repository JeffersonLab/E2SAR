#include "jniLbManager.hpp"


grpc::SslCredentialsOptions parseSslCredemtialOptions(JNIEnv *env, jobjectArray jSslCredentialOptions, jboolean jSslCredentialOptionsFromFile){
  std::vector<std::string> stringVector = jstringArray2Vector(env, jSslCredentialOptions);
  if(jSslCredentialOptionsFromFile){
    auto res = e2sar::LBManager::makeSslOptionsFromFiles(stringVector[0],stringVector[1],stringVector[2]);
    if(!res.has_error()){
      return res.value();
    }
    else{
      return grpc::SslCredentialsOptions();
    }
  }
  else{
    auto res = e2sar::LBManager::makeSslOptionsFromFiles(stringVector[0],stringVector[1],stringVector[2]);
    if(!res.has_error()){
      return res.value();
    }
    else{
      return grpc::SslCredentialsOptions();
    }
  }
}

e2sar::LBManager* getLBManagerFromFeld(JNIEnv *env, jobject jLbManager){

}


JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_LbManager_initLbManager
  (JNIEnv *env, jobject jCallObj, jobject jEjfatUuri, jboolean jValidateServer, jboolean jUseHostAddress, jobjectArray jSslCredentialOptions, jboolean jSslCredentialOptionsFromFile){
    e2sar::EjfatURI cp_uri = parseJavaUri(env, jEjfatUuri);
    grpc::SslCredentialsOptions opts = parseSslCredemtialOptions(env,jSslCredentialOptions,jSslCredentialOptionsFromFile);
    e2sar::LBManager* lbman = new e2sar::LBManager(cp_uri, jValidateServer, jUseHostAddress, opts);
    return (jlong)lbman;
  } 
  
JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2Ljava_lang_String_2_3Ljava_lang_String_2
  (JNIEnv *env, jobject jLbManager, jstring , jstring, jobjectArray);

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2D_3Ljava_lang_String_2
  (JNIEnv *, jobject, jstring, jdouble, jobjectArray);     