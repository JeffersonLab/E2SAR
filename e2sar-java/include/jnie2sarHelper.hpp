#include <jni.h>
#include <iostream>
#include <string>
#include <vector>

#ifndef _Jni_e2sar_helper
#define _Jni_e2sar_helper
std::string getClassName(JNIEnv *env, jclass cls);

void printClassMethods(JNIEnv *env, jclass cls);

void printClassFields(JNIEnv *env, jclass cls);

jmethodID getJMethodId(JNIEnv* env, jclass jClass, std::string methodName, std::string returnVal);

std::string jstring2string(JNIEnv *env, jstring jStr);

std::vector<std::string> jstringArray2Vector(JNIEnv *env, jobjectArray javaStringArray);
#endif
