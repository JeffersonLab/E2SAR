#include "jnie2sarEjfatUri.hpp"

e2sar::EjfatURI parseJavaUri(JNIEnv *env, jobject ejfatUri){

  jclass jEjfatUriCls = env->GetObjectClass(ejfatUri);
  if(jEjfatUriCls == nullptr){
    std::cout << "Could not find EjfatUri class exiting";
    exit(-1);
  } 

  std::cout << getClassName(env,jEjfatUriCls) << std::endl;

  jmethodID getUriID = getJMethodId(env, jEjfatUriCls, "getUri", "()Ljava/lang/String;");

  jstring jCpUri =  (jstring) env->CallObjectMethod(ejfatUri, getUriID);

  std::string cpUri = jstring2string(env, jCpUri);

  jmethodID getTokenIntID = getJMethodId(env, jEjfatUriCls, "getTokenInt", "()I");
  int tokenInt =  (int) env->CallIntMethod(ejfatUri, getTokenIntID);//TokenInt 0 - admin, 1 - instance, 2 - session

  jmethodID getPreferV6ID = getJMethodId(env, jEjfatUriCls, "getPreferv6", "()Z");
  bool preferV6 =  (bool) env->CallBooleanMethod(ejfatUri, getPreferV6ID);

  e2sar::EjfatURI cp_uri(cpUri,e2sar::EjfatURI::TokenType(tokenInt),preferV6);
  
  return cp_uri;
}