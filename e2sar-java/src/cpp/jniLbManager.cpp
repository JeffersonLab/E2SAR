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

e2sar::LBManager* getLBManagerFromField(JNIEnv *env, jobject jLbManager){
  e2sar::LBManager* lbman = reinterpret_cast<e2sar::LBManager*>(getLongField(env,jLbManager,nativeLbField.data()));
  return lbman;
}

JNIEXPORT jlong JNICALL Java_org_jlab_hpdf_LbManager_initLbManager
  (JNIEnv *env, jobject jCallObj, jobject jEjfatUuri, jboolean jValidateServer, jboolean jUseHostAddress, jobjectArray jSslCredentialOptions, jboolean jSslCredentialOptionsFromFile){
    e2sar::EjfatURI cp_uri = parseJavaUri(env, jEjfatUuri);
    grpc::SslCredentialsOptions opts = parseSslCredemtialOptions(env,jSslCredentialOptions,jSslCredentialOptionsFromFile);
    e2sar::LBManager* lbman = new e2sar::LBManager(cp_uri, jValidateServer, jUseHostAddress, opts);
    return (jlong)lbman;
  } 
  
JNIEXPORT jint JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2Ljava_lang_String_2Ljava_util_List_2
  (JNIEnv *env, jobject jLbManager, jstring jLbid, jstring jDuration, jobject jSenders){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::string lbid = jstring2string(env, jLbid);
    std::string duration = jstring2string(env, jDuration);
    boost::posix_time::time_duration timeDuration;
    try{
      timeDuration = boost::posix_time::duration_from_string(duration);
    }
    catch(const boost::bad_lexical_cast& e){
      throwJavaException(env, "Unable to convert duration string " + duration);
      return -1;
    }
    std::cout << timeDuration << std::endl;

    std::vector<std::string> senders = jstringList2Vector(env, jSenders);
    
    auto res = lbman->reserveLB(lbid, timeDuration, senders);
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return -1;
    }
    return res.value();
  }

JNIEXPORT jint JNICALL Java_org_jlab_hpdf_LbManager_reserveLB__Ljava_lang_String_2DLjava_util_List_2
  (JNIEnv *env, jobject jLbManager, jstring jLbid, jdouble jSeconds, jobject jSenders){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::string lbid = jstring2string(env, jLbid);
    double seconds = jSeconds;

    std::vector<std::string> senders = jstringList2Vector(env, jSenders);
    
    auto res = lbman->reserveLB(lbid, seconds, senders);
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return -1;
    }
    return res.value();

  }   

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_getLB__Ljava_lang_String_2
  (JNIEnv *env, jobject jLbManager, jstring jLbid){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::string lbid = jstring2string(env, jLbid);
    
    auto res = lbman->getLB(lbid);
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return;
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_getLB__
  (JNIEnv *env, jobject jLbManager){
    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    auto res = lbman->getLB();
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return;
    }
  }

// JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getWorkerStatusList
//   (JNIEnv *env, jobject jLbManager){

//   }

jobject convertToJWorkerStatus(JNIEnv *env, WorkerStatus wstatus){
  jstring jName = env->NewStringUTF(wstatus.name().data());
  jfloat jFillPercent = static_cast<jfloat>(wstatus.fillpercent());
  jfloat jControlSignal = static_cast<jfloat>(wstatus.controlsignal());
  jint jSlotsAssigned = static_cast<jint>(wstatus.slotsassigned());
  jobject jLastUpdated = convertTimestampToInstant(env, wstatus.lastupdated());

  jclass jWorkerStatusClass = env->FindClass(javaWorkerStatusClass.data());
    if (jWorkerStatusClass == nullptr) {
        throwJavaException(env, "Could not find class: " + javaWorkerStatusClass);
    }

    jmethodID constructor = env->GetMethodID(jWorkerStatusClass, "<init>", "(Ljava/lang/String;FFILjava/time/Instant;)V");
    if (constructor == nullptr) {
        std::cerr << "Error: Could not find the constructor." << std::endl;
        return nullptr;
    }
    jobject myObject = env->NewObject(jWorkerStatusClass, constructor, jName, jFillPercent, jControlSignal, jSlotsAssigned, jLastUpdated);

    env->DeleteLocalRef(jName);
    env->DeleteLocalRef(jLastUpdated);

    return myObject;
}

jobject convertToJLBStatus(JNIEnv *env, e2sar::LBStatus lbstatus){
  jobject jInstantTimestamp = convertTimestampToInstant(env, lbstatus.timestamp);
  jobject jExpiresAt = convertTimestampToInstant(env, lbstatus.expiresAt);
  jobject jSenderAddresses = convertStringVectorToArrayList(env, lbstatus.senderAddresses);
  jlong jCurrentEpoch = static_cast<jlong>(lbstatus.currentEpoch);
  jlong jCurrentPredictedEventNumber = static_cast<jlong>(lbstatus.currentPredictedEventNumber);

  std::vector<jobject> jWorkerVec;
  for(auto wStatus : lbstatus.workers){
    jWorkerVec.push_back(convertToJWorkerStatus(env, wStatus));
  }

  jobject jWorkerArrayList = convertJobjectVectorToArrayList(env, jWorkerVec);

  for(auto jWorker :jWorkerVec){
    env->DeleteLocalRef(jWorker);
  }

  jclass jLBStatusClass = env->FindClass(javaLBStatusClass.data());
  if (jLBStatusClass == nullptr) {
      throwJavaException(env, "Could not find class: " + javaWorkerStatusClass);
  }

  jmethodID constructor = env->GetMethodID(jLBStatusClass, "<init>", "(Ljava/time/Instant;Ljava/time/Instant;JJLjava/util/List;Ljava/util/List;)V");
  if (constructor == nullptr) {
      std::cerr << "Error: Could not find the constructor." << std::endl;
      return nullptr;
  }
  jobject myObject = env->NewObject(jLBStatusClass, constructor, jInstantTimestamp, jExpiresAt, jCurrentEpoch, jCurrentPredictedEventNumber, jWorkerArrayList, jSenderAddresses);

  env->DeleteLocalRef(jInstantTimestamp);
  env->DeleteLocalRef(jExpiresAt);

  return myObject;
}

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getStatus__
  (JNIEnv *env, jobject jLbManager){
    
    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);

    auto res = lbman->getLBStatus();
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto lbstatus = e2sar::LBManager::asLBStatus(res.value());
      return convertToJLBStatus(env, *lbstatus);
    }
  }

  JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getStatus__Ljava_lang_String_2
  (JNIEnv *env, jobject jLbManager, jstring jlbid){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::string lbid = jstring2string(env, jlbid);

    auto res = lbman->getLBStatus(lbid);
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto lbstatus = e2sar::LBManager::asLBStatus(res.value());
      return convertToJLBStatus(env, *lbstatus);
    }
  }

  jobject convertToLBOverivew(JNIEnv *env, e2sar::OverviewEntry overviewEntry){
    jstring jName = env->NewStringUTF(overviewEntry.name.data());
    jstring jLbid = env->NewStringUTF(overviewEntry.lbid.data());
    jobject jSyncAndPort = convertBoostIpAndPortToInetSocketAddress(env, overviewEntry.syncAddressAndPort.first, overviewEntry.syncAddressAndPort.second);
    jobject jDataIPv4 = convertBoostIpToInetAddress(env, overviewEntry.dataIPv4);
    jobject jDataIPv6 = convertBoostIpToInetAddress(env, overviewEntry.dataIPv6);
    jint jFpgaLbid = overviewEntry.fpgaLBId;
    jobject jLBStatus = convertToJLBStatus(env, overviewEntry.status);

    jclass jOverviewClass = env->FindClass(javaLBOverviewClass.data());
    if (jOverviewClass == nullptr) {
        std::cerr << "Error: Could not find Java class com/example/MyClass." << std::endl;
        return nullptr;
    }

    // Step 2: Get the constructor ID with the specified signature
    jmethodID constructor = env->GetMethodID(jOverviewClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/net/InetSocketAddress;Ljava/net/InetAddress;Ljava/net/InetAddress;ILBStatus;)V");
    if (constructor == nullptr) {
        std::cerr << "Error: Could not find the specified constructor." << std::endl;
        return nullptr;
    }

    // Step 4: Create the Java object using the constructor
    jobject jLBOveriew = env->NewObject(
        jOverviewClass,
        constructor,
        jName,
        jLbid,
        jSyncAndPort,
        jDataIPv4,
        jDataIPv6,
        jFpgaLbid,
        jLBStatus);

    return jLBOveriew;
  }

  JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_getOverview
  (JNIEnv *env, jobject jLbManager){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);

    auto res = lbman->overview();
    if (res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto lbOverview = e2sar::LBManager::asOverviewMessage(res.value());
      std::vector<jobject> jOverviewEntryVec;
      for(auto overviewEntry : lbOverview){
        jobject jOverviewEntry = convertToLBOverivew(env, overviewEntry);
        jOverviewEntryVec.push_back(jOverviewEntry);
      }
      return convertJobjectVectorToArrayList(env, jOverviewEntryVec);
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_addSenders
  (JNIEnv *env, jobject jLbManager, jobject jSenderList){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::vector<std::string> senders = jstringList2Vector(env, jSenderList);
    auto res = lbman->addSenders(senders);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_removeSenders
  (JNIEnv *env, jobject jLbManager, jobject jSenderList){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::vector<std::string> senders = jstringList2Vector(env, jSenderList);
    auto res = lbman->removeSenders(senders);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeLB__Ljava_lang_String_2
  (JNIEnv *env, jobject jLbManager, jstring jLbid){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::string lbid = jstring2string(env, jLbid);
    auto res = lbman->freeLB(lbid);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_freeLB__
  (JNIEnv *env, jobject jLbManager){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    auto res = lbman->freeLB();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_registerWorker
  (JNIEnv *env, jobject jLbManager, jstring jNodeName, jstring jNodeIp, jint jNodePort, 
  jfloat jWeight, jint jSourceCount, jfloat jMinFactor, jfloat jMaxFactor){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    std::string nodeName = jstring2string(env, jNodeName);
    std::string ipAddressStr = jstring2string(env, jNodeIp);
    std::pair<boost::asio::ip::address,u_int16_t> node_ip_port = std::make_pair(boost::asio::ip::make_address(ipAddressStr),jNodePort);

    auto res = lbman->registerWorker(nodeName, node_ip_port, jWeight, jSourceCount, jMinFactor, jMaxFactor);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_deregisteWorker
  (JNIEnv *env, jobject jLbManager){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    auto res = lbman->deregisterWorker();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_sendState__FFZ
  (JNIEnv *env, jobject jLbManager, jfloat fill_percent, jfloat control_signal, jboolean is_ready){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    auto res = lbman->sendState(fill_percent, control_signal, is_ready);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT void JNICALL Java_org_jlab_hpdf_LbManager_sendState__FFZLjava_time_Instant_2
  (JNIEnv *env, jobject jLbManager, jfloat fill_percent, jfloat control_signal, jboolean is_ready, jobject jInstant){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    google::protobuf::Timestamp ts = convertInstantToTimestamp(env, jInstant);
    auto res = lbman->sendState(fill_percent, control_signal, is_ready, ts);
    if(res.has_error()){
      throwJavaException(env, res.error().message());
    }
  }

JNIEXPORT jobject JNICALL Java_org_jlab_hpdf_LbManager_version
  (JNIEnv *env, jobject jLbManager){

    e2sar::LBManager* lbman = getLBManagerFromField(env, jLbManager);
    auto res = lbman->version();
    if(res.has_error()){
      throwJavaException(env, res.error().message());
      return nullptr;
    }
    else{
      auto versionTuple = res.value();
      std::vector<std::string> versionVec;
      versionVec.push_back(versionTuple.get<0>());
      versionVec.push_back(versionTuple.get<1>());
      versionVec.push_back(versionTuple.get<2>());
      return convertStringVectorToArrayList(env, versionVec);
    }
  }