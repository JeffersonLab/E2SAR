#include "jnie2sarHelper.hpp"

std::string getClassName(JNIEnv *env, jclass cls) {
    // Get the Class class
    jclass classClass = env->FindClass("java/lang/Class");
    if (classClass == nullptr) {
        std::cerr << "Error: Could not find java.lang.Class" << std::endl;
        return "";
    }

    // Get the getName method ID
    jmethodID getNameMethodID = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    if (getNameMethodID == nullptr) {
        std::cerr << "Error: Could not find getName method" << std::endl;
        return "";
    }

    // Call getName on the class object
    jstring classNameJava = (jstring) env->CallObjectMethod(cls, getNameMethodID);
    const char *className = env->GetStringUTFChars(classNameJava, 0);
    std::string returnString(className);
    // Clean up
    env->ReleaseStringUTFChars(classNameJava, className);
    env->DeleteLocalRef(classNameJava);

    return returnString;
}

void printClassMethods(JNIEnv *env, jclass cls) {
    // Get the Class class
    jclass classClass = env->FindClass("java/lang/Class");
    jclass methodClass = env->FindClass("java/lang/reflect/Method");

    // Get getDeclaredMethods method ID
    jmethodID getDeclaredMethodsID = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID getNameMethodID = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");

    // Call getDeclaredMethods to get an array of Method objects
    jobjectArray methodsArray = (jobjectArray) env->CallObjectMethod(cls, getDeclaredMethodsID);
    jsize methodCount = env->GetArrayLength(methodsArray);

    std::cout << "Methods in class:" << std::endl;

    for (jsize i = 0; i < methodCount; i++) {
        jobject methodObj = env->GetObjectArrayElement(methodsArray, i);
        jstring methodNameJava = (jstring) env->CallObjectMethod(methodObj, getNameMethodID);
        const char *methodName = env->GetStringUTFChars(methodNameJava, nullptr);

        // Print each method name
        std::cout << " - " << methodName << std::endl;

        // Clean up
        env->ReleaseStringUTFChars(methodNameJava, methodName);
        env->DeleteLocalRef(methodNameJava);
        env->DeleteLocalRef(methodObj);
    }

    // Clean up
    env->DeleteLocalRef(methodsArray);
}

void printClassFields(JNIEnv *env, jclass cls) {
    jclass classClass = env->FindClass("java/lang/Class");
    jclass fieldClass = env->FindClass("java/lang/reflect/Field");

    // Get getDeclaredFields and getName method IDs
    jmethodID getDeclaredFieldsID = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
    jmethodID getNameMethodID = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");

    // Call getDeclaredFields to get an array of Field objects
    jobjectArray fieldsArray = (jobjectArray) env->CallObjectMethod(cls, getDeclaredFieldsID);
    jsize fieldCount = env->GetArrayLength(fieldsArray);

    std::cout << "Fields in class:" << std::endl;

    for (jsize i = 0; i < fieldCount; i++) {
        jobject fieldObj = env->GetObjectArrayElement(fieldsArray, i);
        jstring fieldNameJava = (jstring) env->CallObjectMethod(fieldObj, getNameMethodID);
        const char *fieldName = env->GetStringUTFChars(fieldNameJava, nullptr);

        // Print each field name
        std::cout << " - " << fieldName << std::endl;

        // Clean up
        env->ReleaseStringUTFChars(fieldNameJava, fieldName);
        env->DeleteLocalRef(fieldNameJava);
        env->DeleteLocalRef(fieldObj);
    }

    // Clean up
    env->DeleteLocalRef(fieldsArray);
}

jmethodID getJMethodId(JNIEnv* env, jclass jClass, std::string methodName, std::string returnVal){
    jmethodID getUriID = env->GetMethodID(jClass, methodName.data(), returnVal.data());
    if(getUriID == nullptr) {
      std::cout << methodName << ":" << returnVal << " - Does not exist in class " << getClassName(env, jClass);
        exit(-1);
    } 
    return getUriID;
}

std::string jstring2string(JNIEnv *env, jstring jStr) {
    jboolean isCopy;
    const char *convertedValue = env->GetStringUTFChars(jStr, &isCopy);
    std::string returnString(convertedValue);
    env->ReleaseStringUTFChars(jStr,convertedValue);
    return returnString;
}

std::vector<std::string> jstringArray2Vector(JNIEnv *env, jobjectArray javaStringArray) {
    std::vector<std::string> stringVector;

    // Get the length of the Java array
    jsize arrayLength = env->GetArrayLength(javaStringArray);
    jboolean isCopy;
    // Loop over each element in the Java array
    for (jsize i = 0; i < arrayLength; i++) {
        // Get the jstring element at index i
        jstring javaString = (jstring) env->GetObjectArrayElement(javaStringArray, i);
        if(javaString != nullptr){
            // Convert jstring to C++ std::string
            const char *nativeString = env->GetStringUTFChars(javaString, &isCopy);
            std::string cppString(nativeString);

            // Add the std::string to the vector
            stringVector.push_back(cppString);

            // Release the resources
            env->ReleaseStringUTFChars(javaString, nativeString);
        }
        else{
            stringVector.push_back("");
        }
        
        env->DeleteLocalRef(javaString);  // Clean up the local reference
    }

    return stringVector;
}