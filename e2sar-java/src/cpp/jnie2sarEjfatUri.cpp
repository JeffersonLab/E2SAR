#include "jnie2sarEjfatUri.hpp"

e2sar::EjfatURI* getEjfatUriFromField(JNIEnv *env, jobject jEjfatUri){
  e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(getLongField(env, jEjfatUri, nativeEjfatUri.data()));
  return ejfatUri;
}

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_EjfatURI_initEjfatUri
  (JNIEnv *env, jclass jEjfatUri, jstring jUri, jint jTokenType, jboolean jPreferv6){
    std::string uri = jstring2string(env, jUri);
    e2sar::EjfatURI* ejfatUri;
    try{
      ejfatUri = new e2sar::EjfatURI(uri, e2sar::EjfatURI::TokenType(jTokenType), jPreferv6);
    }
    catch(const e2sar::E2SARException& e){
      throwJavaException(env, e);
    }
    return (jlong)ejfatUri;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_EjfatURI_getUriFromFile
  (JNIEnv *env, jclass jEjfatUri, jstring jFileName, jint jTokenType, jboolean jPreferv6){
    std::string fileName = jstring2string(env, jFileName);
    auto res = e2sar::EjfatURI::getFromFile(fileName);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return -1;
    }
    else{
      auto uri = res.value();
      e2sar::EjfatURI* ejfatUri = new e2sar::EjfatURI(uri);
      return (jlong) ejfatUri;
    }
  }

JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_getUseTls
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    return ejfatUri->get_useTls();
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setInstanceToken
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jstring jInstanceToken){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string instanceToken = jstring2string(env,jInstanceToken);
    ejfatUri->set_InstanceToken(instanceToken);
  }


JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSessionToken
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jstring jSessionToken){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string sessionToken = jstring2string(env,jSessionToken);
    ejfatUri->set_SessionToken(sessionToken);
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getInstanceToken
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);

    auto res = ejfatUri->get_InstanceToken();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      std::string instanceToken = res.value();
      return env->NewStringUTF(instanceToken.data());
    }
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getSessionToken
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_SessionToken();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      std::string sessionToken = res.value();
      return env->NewStringUTF(sessionToken.data());
    }
  }


JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getAdminToken
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_AdminToken();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      std::string adminToken = res.value();
      return env->NewStringUTF(adminToken.data());
    }

  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setLbName
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jstring jLbName){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string lbName = jstring2string(env, jLbName);
    ejfatUri->set_lbName(lbName);
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setLbid
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jstring jLbid){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string lbid = jstring2string(env, jLbid);
    ejfatUri->set_lbId(lbid);
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSessionId
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jstring jSessionId){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string sessionId = jstring2string(env, jSessionId);
    ejfatUri->set_sessionId(sessionId);
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setSyncAddr
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jobject jInetSocketAddress){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::pair<boost::asio::ip::address, u_int16_t> syncAddr = convertInetSocketAddress(env, jInetSocketAddress);
    ejfatUri->set_syncAddr(syncAddr);
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_setDataAddr
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jobject jInetSocketAddress){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::pair<boost::asio::ip::address, u_int16_t> syncAddr = convertInetSocketAddress(env, jInetSocketAddress);
    ejfatUri->set_dataAddr(syncAddr);
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getLbName
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string lbName = ejfatUri->get_lbName();
    return env->NewStringUTF(lbName.data());
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getLbid
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string lbid = ejfatUri->get_lbId();
    return env->NewStringUTF(lbid.data());
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_getSessionId
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    std::string sessionId = ejfatUri->get_sessionId();
    return env->NewStringUTF(sessionId.data());
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getCpAddr
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_cpAddr();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto cpAddr = res.value();
      return convertBoostIpAndPortToInetSocketAddress(env, cpAddr.first, cpAddr.second);
    }
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getCpHost
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_cpHost();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto cpHost = res.value();
      return convertHostNameAndPortToInetSocketAddress(env, cpHost);
    }
  }

JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddrv4
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    return ejfatUri->has_dataAddrv4();
  }


JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddrv6
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    return ejfatUri->has_dataAddrv6();
  }


JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasDataAddr
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    return ejfatUri->has_dataAddr();
  }

JNIEXPORT jboolean JNICALL Java_org_jlab_hpdf_EjfatURI_hasSyncAddr
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    return ejfatUri->has_syncAddr();
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getDataAddrv4
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_dataAddrv4();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto dataAddr = res.value();
      return convertBoostIpAndPortToInetSocketAddress(env, dataAddr.first, dataAddr.second);
    }
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getDataAddrv6
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_dataAddrv6();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto dataAddr = res.value();
      return convertBoostIpAndPortToInetSocketAddress(env, dataAddr.first, dataAddr.second);
    }
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_EjfatURI_getSyncAddr
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    auto res = ejfatUri->get_syncAddr();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto syncAddr = res.value();
      return convertBoostIpAndPortToInetSocketAddress(env, syncAddr.first, syncAddr.second);
    }
  }

JNIEXPORT jstring JNICALL Java_org_jlab_hpdf_EjfatURI_toString
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer, jint jToken){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    return env->NewStringUTF(ejfatUri->to_string(e2sar::EjfatURI::TokenType(jToken)).data());
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_EjfatURI_freeNativePointer
  (JNIEnv *env, jobject jEjfatUri, jlong jNativePointer){
    e2sar::EjfatURI* ejfatUri = reinterpret_cast<e2sar::EjfatURI*>(jNativePointer);
    delete ejfatUri;
  }