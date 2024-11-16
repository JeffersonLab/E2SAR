#include <jni.h>
#include <iostream>
#include <string>
#include <vector>
#include <google/protobuf/util/time_util.h>
#include <boost/asio.hpp>

#ifndef _Jni_e2sar_helper
#define _Jni_e2sar_helper
const std::string javaExceptionClass = "org/jlab/hpdf/exceptions/E2sarNativeException";
const std::string javaInstantClass = "java/time/Instant";
const std::string javaArrayListClass = "java/util/ArrayList";
const std::string javaInetAddressClass = "java/net/InetAddress";
const std::string javaInetSocketAddressClass = "java/net/InetSocketAddress";
std::string getClassName(JNIEnv *env, jclass cls);

void printClassMethods(JNIEnv *env, jclass cls);

void printClassFields(JNIEnv *env, jclass cls);

jmethodID getJMethodId(JNIEnv* env, jclass jClass, std::string methodName, std::string returnVal);

std::string jstring2string(JNIEnv *env, jstring jStr);

std::vector<std::string> jstringArray2Vector(JNIEnv *env, jobjectArray javaStringArray);

long getLongField(JNIEnv *env, jobject javaObject, const char* fieldName);

void throwJavaException(JNIEnv *env, std::string message="");

std::vector<std::string> jstringList2Vector(JNIEnv *env, jobject javaList); 

jobject convertTimestampToInstant(JNIEnv *env, const google::protobuf::Timestamp &timestamp);

google::protobuf::Timestamp convertInstantToTimestamp(JNIEnv *env, jobject jInstant);

jobject convertJobjectVectorToArrayList(JNIEnv *env, const std::vector<jobject> &jVec);

jobject convertStringVectorToArrayList(JNIEnv *env, const std::vector<std::string> &vec);

jobject convertBoostIpToInetAddress(JNIEnv *env, const boost::asio::ip::address &address);

jobject convertBoostIpAndPortToInetSocketAddress(JNIEnv *env, const boost::asio::ip::address &address, int port);

std::pair<boost::asio::ip::address, u_int16_t> convertInetSocketAddress(JNIEnv* env, jobject inetSocketAddress);
#endif
