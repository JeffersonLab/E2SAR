#include "jniDpReassembler.hpp"

e2sar::Reassembler::ReassemblerFlags parseReassemblerFlags(JNIEnv *env, jobject jReassemblerFlags){
  e2sar::Reassembler::ReassemblerFlags rFlags;

  // Get class reference
  jclass cls = env->GetObjectClass(jReassemblerFlags);  

  // Retrieve boolean fields
  jfieldID fid_useCP = env->GetFieldID(cls, "useCP", "Z");
  jfieldID fid_useHostAddress = env->GetFieldID(cls, "useHostAddress", "Z");
  jfieldID fid_validateCert = env->GetFieldID(cls, "validateCert", "Z");
  jfieldID fid_withLBHeader = env->GetFieldID(cls, "withLBHeader", "Z");
  
  rFlags.useCP = env->GetBooleanField(jReassemblerFlags, fid_useCP);
  rFlags.useHostAddress = env->GetBooleanField(jReassemblerFlags, fid_useHostAddress);
  rFlags.validateCert = env->GetBooleanField(jReassemblerFlags, fid_validateCert);
  rFlags.withLBHeader = env->GetBooleanField(jReassemblerFlags, fid_withLBHeader);
  
  // Retrieve integer fields
  jfieldID fid_period_ms = env->GetFieldID(cls, "period_ms", "I");
  jfieldID fid_portRange = env->GetFieldID(cls, "portRange", "I");
  jfieldID fid_eventTimeout_ms = env->GetFieldID(cls, "eventTimeout_ms", "I");
  jfieldID fid_rcvSocketBufSize = env->GetFieldID(cls, "rcvSocketBufSize", "I");
  
  rFlags.period_ms = env->GetIntField(jReassemblerFlags, fid_period_ms);
  rFlags.portRange = env->GetIntField(jReassemblerFlags, fid_portRange);
  rFlags.eventTimeout_ms = env->GetIntField(jReassemblerFlags, fid_eventTimeout_ms);
  rFlags.rcvSocketBufSize = env->GetIntField(jReassemblerFlags, fid_rcvSocketBufSize);
  
  // Retrieve float fields
  jfieldID fid_Ki = env->GetFieldID(cls, "Ki", "F");
  jfieldID fid_Kp = env->GetFieldID(cls, "Kp", "F");
  jfieldID fid_Kd = env->GetFieldID(cls, "Kd", "F");
  jfieldID fid_setPoint = env->GetFieldID(cls, "setPoint", "F");
  jfieldID fid_weight = env->GetFieldID(cls, "weight", "F");
  jfieldID fid_min_factor = env->GetFieldID(cls, "min_factor", "F");
  jfieldID fid_max_factor = env->GetFieldID(cls, "max_factor", "F");
  
  rFlags.Ki = env->GetFloatField(jReassemblerFlags, fid_Ki);
  rFlags.Kp = env->GetFloatField(jReassemblerFlags, fid_Kp);
  rFlags.Kd = env->GetFloatField(jReassemblerFlags, fid_Kd);
  rFlags.setPoint = env->GetFloatField(jReassemblerFlags, fid_setPoint);
  rFlags.weight = env->GetFloatField(jReassemblerFlags, fid_weight);
  rFlags.min_factor = env->GetFloatField(jReassemblerFlags, fid_min_factor);
  rFlags.max_factor = env->GetFloatField(jReassemblerFlags, fid_max_factor);
  
  // Retrieve long field
  jfieldID fid_epoch_ms = env->GetFieldID(cls, "epoch_ms", "J");
  rFlags.epoch_ms = env->GetLongField(jReassemblerFlags, fid_epoch_ms);

  return rFlags;
}

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2ILjava_util_List_2Lorg_jlab_hpdf_config_ReassemblerFlags_2
  (JNIEnv *env, jobject jReassembler, jobject jDpUri, jobject jInetAddress, jint jStartingPort, jobject jCpuCoreList, jobject jReassemblerFlags){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    e2sar::Reassembler::ReassemblerFlags rFlags = parseReassemblerFlags(env, jReassemblerFlags);
    std::vector<int> cpuCoreList = jIntList2Vector(env, jCpuCoreList);
    std::optional<boost::asio::ip::address> ipAddress = convertInetAddressToBoostIp(env, jInetAddress);
    if(!ipAddress.has_value()){
        return -1;
    }

    e2sar::Reassembler* reassembler = new e2sar::Reassembler(*dpUri, ipAddress.value(), (u_int16_t) jStartingPort, cpuCoreList, rFlags);
    return (jlong) reassembler;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2ILjava_util_List_2Ljava_lang_String_2
  (JNIEnv *env, jobject jReassembler, jobject jDpUri, jobject jInetAddress, jint jStartingPort, jobject jCpuCoreList, jstring jIniFile){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    std::vector<int> cpuCoreList = jIntList2Vector(env, jCpuCoreList);
    std::optional<boost::asio::ip::address> ipAddress = convertInetAddressToBoostIp(env, jInetAddress);
    if(!ipAddress.has_value()){
        return -1;
    }
    std::string iniFile = jstring2string(env, jIniFile);
    auto res = e2sar::Reassembler::ReassemblerFlags::getFromINI(iniFile);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return -1;
    }
    auto rFlags = res.value();

    e2sar::Reassembler* reassembler = new e2sar::Reassembler(*dpUri, ipAddress.value(), (u_int16_t) jStartingPort, cpuCoreList, rFlags);
    return (jlong) reassembler;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2ILjava_util_List_2
  (JNIEnv *env, jobject jReassembler, jobject jDpUri, jobject jInetAddress, jint jStartingPort, jobject jCpuCoreList){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    std::vector<int> cpuCoreList = jIntList2Vector(env, jCpuCoreList);
    std::optional<boost::asio::ip::address> ipAddress = convertInetAddressToBoostIp(env, jInetAddress);
    if(!ipAddress.has_value()){
        return -1;
    }

    e2sar::Reassembler* reassembler = new e2sar::Reassembler(*dpUri, ipAddress.value(), (u_int16_t) jStartingPort, cpuCoreList);
    return (jlong) reassembler;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2IJLorg_jlab_hpdf_config_ReassemblerFlags_2
  (JNIEnv *env, jobject jReassembler, jobject jDpUri, jobject jInetAddress, jint jStartingPort, jlong jNumRecvThreads, jobject jReassemblerFlags){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    e2sar::Reassembler::ReassemblerFlags rFlags = parseReassemblerFlags(env, jReassemblerFlags);
    std::optional<boost::asio::ip::address> ipAddress = convertInetAddressToBoostIp(env, jInetAddress);
    if(!ipAddress.has_value()){
        return -1;
    }

    e2sar::Reassembler* reassembler = new e2sar::Reassembler(*dpUri, ipAddress.value(), (u_int16_t) jStartingPort, (size_t) jNumRecvThreads, rFlags);
    return (jlong) reassembler;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2IJLjava_lang_String_2
  (JNIEnv *env, jobject jReassembler, jobject jDpUri, jobject jInetAddress, jint jStartingPort, jlong jNumRecvThreads, jstring jIniFile){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    std::optional<boost::asio::ip::address> ipAddress = convertInetAddressToBoostIp(env, jInetAddress);
    if(!ipAddress.has_value()){
        return -1;
    }
    std::string iniFile = jstring2string(env, jIniFile);
    auto res = e2sar::Reassembler::ReassemblerFlags::getFromINI(iniFile);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return -1;
    }
    auto rFlags = res.value();

    e2sar::Reassembler* reassembler = new e2sar::Reassembler(*dpUri, ipAddress.value(), (u_int16_t) jStartingPort, (size_t) jNumRecvThreads, rFlags);
    return (jlong) reassembler;
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_initReassembler__Lorg_jlab_hpdf_EjfatURI_2Ljava_net_InetAddress_2IJ
  (JNIEnv *env, jobject jReassembler, jobject jDpUri, jobject jInetAddress, jint jStartingPort, jlong jNumRecvThreads){
    e2sar::EjfatURI* dpUri = getEjfatUriFromField(env, jDpUri);
    e2sar::Reassembler::ReassemblerFlags rFlags;
    std::optional<boost::asio::ip::address> ipAddress = convertInetAddressToBoostIp(env, jInetAddress);
    if(!ipAddress.has_value()){
        return -1;
    }

    e2sar::Reassembler* reassembler = new e2sar::Reassembler(*dpUri, ipAddress.value(), (u_int16_t) jStartingPort, (size_t) jNumRecvThreads, rFlags);
    return (jlong) reassembler;
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_registerWorker
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler, jstring jNodeName){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);
    std::string nodeName = jstring2string(env, jNodeName);

    auto res = reassembler->registerWorker(nodeName);
    if(res.has_error()){
        throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_deregisterWorker
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    auto res = reassembler->deregisterWorker();
    if(res.has_error()){
        throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_openAndStart
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    auto res = reassembler->openAndStart();
    if(res.has_error()){
        throwJavaException(env, res.error().message());
    }
  }

jobject createDirectByteBuffer(JNIEnv* env, void* data, size_t size) {
    jobject jDirectBuffer = env->NewDirectByteBuffer(data, size);
    if(jDirectBuffer == nullptr){
        throwJavaException(env, "Failed to create DirectByteBuffer");
    }
    return jDirectBuffer;   
}

jobject createOptionalJavaReassembledEvent(JNIEnv *env, jobject jDirectBuff, e2sar::EventNum_t eventNum, u_int16_t recDataId){
    // Get Optional class
    jclass optionalClass = env->FindClass("java/util/Optional");
    jclass jEventClass = env->FindClass(javaReassembledEventClass.data());

    if(jEventClass == nullptr){
        throwJavaException(env, "Could not find class: " + javaReassembledEventClass);
        return nullptr;
    }
    jmethodID constructor = env->GetMethodID(jEventClass, "<init>", "(Ljava/nio/ByteBuffer;JI)V");

    if (constructor == nullptr) {
        throwJavaException(env, "Could not find the constructor of class: " + javaReassembledEventClass);
        return nullptr;
    }

    jobject eventObj = env->NewObject(jEventClass, constructor, jDirectBuff, (jlong) eventNum, (jint) recDataId);
    jmethodID ofMethod = env->GetStaticMethodID(optionalClass, "of", "(Ljava/lang/Object;)Ljava/util/Optional;");
    return env->CallStaticObjectMethod(optionalClass, ofMethod, eventObj);
}

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getEvent
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    u_int8_t *eventBuf{nullptr};
    size_t eventLen;
    e2sar::EventNum_t eventNum;
    u_int16_t recDataId;

    auto res = reassembler->getEvent(&eventBuf, &eventLen, &eventNum, &recDataId);
    if(res.has_error()){
        jclass optionalClass = env->FindClass("java/util/Optional");
        jmethodID emptyMethod = env->GetStaticMethodID(optionalClass, "empty", "()Ljava/util/Optional;");
        return env->CallStaticObjectMethod(optionalClass, emptyMethod);
    }

    jobject jDirectBuffer = createDirectByteBuffer(env, eventBuf, eventLen);
    return createOptionalJavaReassembledEvent(env, jDirectBuffer, eventNum, recDataId);
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_recvEvent
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler, jlong jWaitTime){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    u_int8_t *eventBuf{nullptr};
    size_t eventLen;
    e2sar::EventNum_t eventNum;
    u_int16_t recDataId;

    auto res = reassembler->recvEvent(&eventBuf, &eventLen, &eventNum, &recDataId, jWaitTime);
    if(res.has_error()){
        jclass optionalClass = env->FindClass("java/util/Optional");
        jmethodID emptyMethod = env->GetStaticMethodID(optionalClass, "empty", "()Ljava/util/Optional;");
        return env->CallStaticObjectMethod(optionalClass, emptyMethod);
    }
    
    jobject jDirectBuffer = createDirectByteBuffer(env, eventBuf, eventLen);
    return createOptionalJavaReassembledEvent(env, jDirectBuffer, eventNum, recDataId);
  }

jobject convertToJRecvStats(JNIEnv *env, boost::tuple<e2sar::EventNum_t, e2sar::EventNum_t, int, int, int, e2sar::E2SARErrorc> stats){
    // Extract values from tuple
    jlong enqueueLoss = static_cast<jlong>(boost::get<0>(stats));
    jlong eventSuccess = static_cast<jlong>(boost::get<1>(stats));
    jint lastErrorNo = static_cast<jint>(boost::get<2>(stats));
    jint grpcErrCount = static_cast<jint>(boost::get<3>(stats));
    jint dataErrCount = static_cast<jint>(boost::get<4>(stats));
    jint lastE2sarError = static_cast<jint>(boost::get<5>(stats));

    // Get RecvStats Java class
    jclass recvStatsClass = env->FindClass(javaRecvStatsClass.data());
    if (!recvStatsClass) {
        throw std::runtime_error("Failed to find RecvStats class");
    }

    // Get constructor (RecvStats(long, long, int, int, int, int))
    jmethodID constructor = env->GetMethodID(recvStatsClass, "<init>", "(JJIIII)V");
    if (!constructor) {
        throw std::runtime_error("Failed to find RecvStats constructor");
    }

    // Create Java RecvStats object
    jobject recvStatsObj = env->NewObject(recvStatsClass, constructor, enqueueLoss, eventSuccess, lastErrorNo, grpcErrCount, dataErrCount, lastE2sarError);

    return recvStatsObj;
}

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getStats
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    return convertToJRecvStats(env, reassembler->getStats());
  }

jobject convertLostEventToOptionalJLostEvent(JNIEnv *env, std::pair<e2sar::EventNum_t, u_int16_t> lostEvent){
    // Extract values from the pair
    jlong eventNum = static_cast<jlong>(lostEvent.first);
    jint dataId = static_cast<jint>(lostEvent.second);

    // Find the Java class LostEvent
    jclass lostEventClass = env->FindClass(javaLostEventClass.data());
    if (!lostEventClass) {
        throw std::runtime_error("Failed to find LostEvent class");
    }

    // Find the constructor (LostEvent(long, int))
    jmethodID constructor = env->GetMethodID(lostEventClass, "<init>", "(JI)V");
    if (!constructor) {
        throw std::runtime_error("Failed to find LostEvent constructor");
    }

    // Create a new LostEvent object
    jobject lostEventObj = env->NewObject(lostEventClass, constructor, eventNum, dataId);

    jclass optionalClass = env->FindClass("java/util/Optional");
    jmethodID ofMethod = env->GetStaticMethodID(optionalClass, "of", "(Ljava/lang/Object;)Ljava/util/Optional;");
    return env->CallStaticObjectMethod(optionalClass, ofMethod, lostEventObj);
}   

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getLostEvent
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    auto res = reassembler->get_LostEvent();
    if(res.has_error()){
        jclass optionalClass = env->FindClass("java/util/Optional");
        jmethodID emptyMethod = env->GetStaticMethodID(optionalClass, "empty", "()Ljava/util/Optional;");
        return env->CallStaticObjectMethod(optionalClass, emptyMethod);
    }

    return convertLostEventToOptionalJLostEvent(env, res.value());
  }

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_Reassembler_getNumRecvThreads
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    return (jlong) reassembler->get_numRecvThreads();
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_Reassembler_getRecvPorts
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);
    
    auto recvPortsPair = reassembler->get_recvPorts();
    std::vector<int> recvPorts = {recvPortsPair.first, recvPortsPair.second};
    

    return convertIntVectorToArrayList(env, recvPorts);
  }

JNIEXPORT jint JNICALL Java_org_jlab_hpdf_Reassembler_getPortRange
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);

    return reassembler->get_portRange();
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_freeDirectBytebBuffer
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler, jobject jDirectByteBuffer){
    if (jDirectByteBuffer != nullptr) {
        u_int8_t* data = (u_int8_t*)env->GetDirectBufferAddress(jDirectByteBuffer);
        if (data) {
            delete[] data;
        }
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_Reassembler_freeNativePointer
  (JNIEnv *env, jobject jReassembler, jlong jNativeReassembler){
    e2sar::Reassembler* reassembler = reinterpret_cast<e2sar::Reassembler*>(jNativeReassembler);
    delete reassembler;
  }