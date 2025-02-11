#include "jniDpSegmenter.hpp"

e2sar::Segmenter::SegmenterFlags parseSegmenterFlags(JNIEnv *env, jobject jSegmenterFlags){
  e2sar::Segmenter::SegmenterFlags sFlags;

  // Get class reference
  jclass cls = env->GetObjectClass(jSegmenterFlags);

  // Get field IDs
  jfieldID dpV6ID = env->GetFieldID(cls, "dpV6", "Z");
  jfieldID zeroCopyID = env->GetFieldID(cls, "zeroCopy", "Z");
  jfieldID connectedSocketID = env->GetFieldID(cls, "connectedSocket", "Z");
  jfieldID useCPID = env->GetFieldID(cls, "useCP", "Z");
  jfieldID zeroRateID = env->GetFieldID(cls, "zeroRate", "Z");
  jfieldID usecAsEventNumID = env->GetFieldID(cls, "usecAsEventNum", "Z");
  jfieldID syncPeriodMsID = env->GetFieldID(cls, "syncPeriodMs", "I");
  jfieldID syncPeriodsID = env->GetFieldID(cls, "syncPeriods", "I");
  jfieldID mtuID = env->GetFieldID(cls, "mtu", "I");
  jfieldID numSendSocketsID = env->GetFieldID(cls, "numSendSockets", "J");
  jfieldID sndSocketBufSizeID = env->GetFieldID(cls, "sndSocketBufSize", "I");

  // Retrieve values from Java object
  sFlags.dpV6 = env->GetBooleanField(jSegmenterFlags, dpV6ID);
  sFlags.zeroCopy = env->GetBooleanField(jSegmenterFlags, zeroCopyID);
  sFlags.connectedSocket = env->GetBooleanField(jSegmenterFlags, connectedSocketID);
  sFlags.useCP = env->GetBooleanField(jSegmenterFlags, useCPID);
  sFlags.zeroRate = env->GetBooleanField(jSegmenterFlags, zeroRateID);
  sFlags.usecAsEventNum = env->GetBooleanField(jSegmenterFlags, usecAsEventNumID);
  sFlags.syncPeriodMs = env->GetIntField(jSegmenterFlags, syncPeriodMsID);
  sFlags.syncPeriods = env->GetIntField(jSegmenterFlags, syncPeriodsID);
  sFlags.mtu = env->GetIntField(jSegmenterFlags, mtuID);
  sFlags.numSendSockets = env->GetLongField(jSegmenterFlags, numSendSocketsID);
  sFlags.sndSocketBufSize = env->GetIntField(jSegmenterFlags, sndSocketBufSizeID);

  return sFlags;
}

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_initSegmentor__Lorg_jlab_hpdf_EjfatURI_2IJLorg_jlab_hpdf_config_SegmenterFlags_2
  (JNIEnv *env, jobject jSegmenter, jobject jDpUri, jint jDataId, jlong jEventSrcId, jobject jSegmenterFlags){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    e2sar::Segmenter::SegmenterFlags sFlags = parseSegmenterFlags(env, jSegmenterFlags);
    e2sar::Segmenter* segmenter = new e2sar::Segmenter(*dpUri, (u_int16_t)jDataId, (u_int32_t) jEventSrcId, sFlags);

    return (jlong) segmenter;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_initSegmentor__Lorg_jlab_hpdf_EjfatURI_2IJLjava_lang_String_2
  (JNIEnv *env, jobject jSegmenter, jobject jDpUri, jint jDataId, jlong jEventSrcId, jstring jIniFile){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    std::string iniFile = jstring2string(env, jIniFile);
    auto res = e2sar::Segmenter::SegmenterFlags::getFromINI(iniFile);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return -1;
    }
    auto sFlags = res.value();
    e2sar::Segmenter* segmenter = new e2sar::Segmenter(*dpUri, (u_int16_t)jDataId, (u_int32_t) jEventSrcId, sFlags);

    return (jlong) segmenter;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_initSegmentor__Lorg_jlab_hpdf_EjfatURI_2IJ
  (JNIEnv *env, jobject jSegmenter, jobject jDpUri, jint jDataId, jlong jEventSrcId){
    e2sar::Segmenter::SegmenterFlags sFlags;
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    e2sar::Segmenter* segmenter = new e2sar::Segmenter(*dpUri, (u_int16_t)jDataId, (u_int32_t) jEventSrcId, sFlags); 
    return (jlong) segmenter;
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_openAndStart
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter){
    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    auto res = segmenter->openAndStart();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_sendEventDirect
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter, jobject jByteBuffer, jint jSize, jlong jEventNumber, jint jDataId, jint jEntropy){
    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    u_int8_t* event = getDirectByteBufferPointer(env, jByteBuffer);

    auto res = segmenter->sendEvent(event, jSize, jEventNumber, jDataId, jEntropy);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

void freeGloablByteBuffer(boost::any a){
  std::pair<JNIEnv *, jobject> globalByteBuffer = boost::any_cast<std::pair<JNIEnv *, jobject>>(a);
  JNIEnv *env = globalByteBuffer.first;
  jobject jGlobalByteBuffer = globalByteBuffer.second;
  env->DeleteGlobalRef(jGlobalByteBuffer);
}

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_addToSendQueueDirect
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter, jobject jByteBuffer, jint jSize, jlong jEventNumber, jint jDataId, jint jEntropy){
    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    u_int8_t* event = getDirectByteBufferPointer(env, jByteBuffer);

    //Need a GlobalReference so that the JVM does not deallocate this object
    jobject jGlobalByteBufferRef = env->NewGlobalRef(jByteBuffer);
    auto res = segmenter->addToSendQueue(event, jSize, jEventNumber, jDataId, jEntropy, &freeGloablByteBuffer, std::make_pair(env, jGlobalByteBufferRef));
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT jint JNICALL Java_org_jlab_hpdf_Segmenter_getMTU
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter){

    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    return segmenter->getMTU();
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Segmenter_getMaxPayloadLength
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter){

    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    return segmenter->getMaxPldLen();
  }

jobject convertToJSyncStats(JNIEnv *env, boost::tuple<u_int64_t, u_int64_t, int> syncStats){

  jlong jSyncMsgCount = static_cast<jlong>(boost::get<0>(syncStats));
  jlong syncErrCount = static_cast<jlong>(boost::get<1>(syncStats));
  jint lastErrorNo = static_cast<jint>(boost::get<2>(syncStats));

  jclass jSyncStatsClass = env->FindClass(javaSyncStatsClass.data());
  if (jSyncStatsClass == nullptr) {
    throwJavaException(env, "Could not find class: " + javaSyncStatsClass);
    return nullptr;
  }

  jmethodID constructor = env->GetMethodID(jSyncStatsClass, "<init>", "(JJI)V");
  if (constructor == nullptr) {
    throwJavaException(env, "Could not find the constructor of class: " + javaSyncStatsClass);
    return nullptr;
  }
  jobject jSyncStatsObject = env->NewObject(jSyncStatsClass, constructor, jSyncMsgCount, syncErrCount, lastErrorNo);

  return jSyncStatsObject;
}
JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Segmenter_getSyncStats
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter){

    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    return convertToJSyncStats(env, segmenter->getSyncStats());
  }

jobject convertToJSendStats(JNIEnv *env, boost::tuple<u_int64_t, u_int64_t, int> syncStats){

  jlong jEventDatagramCount = static_cast<jlong>(boost::get<0>(syncStats));
  jlong jEventDatagramErrCount = static_cast<jlong>(boost::get<1>(syncStats));
  jint jLastErrNo = static_cast<jint>(boost::get<2>(syncStats));

  jclass jSendStatsClass = env->FindClass(javaSendStatsClass.data());
  if (jSendStatsClass == nullptr) {
    throwJavaException(env, "Could not find class: " + javaSendStatsClass);
    return nullptr;
  }

  jmethodID constructor = env->GetMethodID(jSendStatsClass, "<init>", "(JJI)V");
  if (constructor == nullptr) {
    throwJavaException(env, "Could not find the constructor of class: " + javaSendStatsClass);
    return nullptr;
  }
  jobject jSyncStatsObject = env->NewObject(jSendStatsClass, constructor, jEventDatagramCount, jEventDatagramErrCount, jLastErrNo);

  return jSyncStatsObject;
}

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Segmenter_getSendStats
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter){

    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    return convertToJSendStats(env, segmenter->getSendStats());
  }


JNIEXPORT void JNICALL Java_org_jlab_hpdf_Segmenter_freeNativePointer
  (JNIEnv *env, jobject jSegmenter, jlong jNativeSegmenter){
    e2sar::Segmenter* segmenter = reinterpret_cast<e2sar::Segmenter*>(jNativeSegmenter);
    delete segmenter;
  }