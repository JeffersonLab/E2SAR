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

long getLongField(JNIEnv *env, jobject javaObject, const char* fieldName){
    jclass cls = env->GetObjectClass(javaObject);
    if (cls == nullptr) {
        std::cerr << "Error: Class not found." << std::endl;
        return -1;
    }

    // Get the field ID of the private long field
    jfieldID fieldID = env->GetFieldID(cls, fieldName, "J");
    if (fieldID == nullptr) {
        std::cerr << "Error: Field " << fieldName << " not found." << std::endl;
        return -1;
    }

    // Access the private long field using GetLongField
    jlong fieldValue = env->GetLongField(javaObject, fieldID);

    return static_cast<long>(fieldValue);
}

void throwJavaException(JNIEnv *env, std::string message){
    jclass newExcCls = env->FindClass(javaExceptionClass.data());
    if (newExcCls != NULL)
        env->ThrowNew(newExcCls, message.data());
    else{
        std::cout << "Could not throw Java Exception" << std::endl;
        exit(-1); 
    }
}

std::vector<std::string> jstringList2Vector(JNIEnv *env, jobject javaList){
    std::vector<std::string> stringVector;

    // Get the List and Iterator classes and their methods
    jclass listClass = env->FindClass("java/util/List");
    jmethodID iteratorMethodID = env->GetMethodID(listClass, "iterator", "()Ljava/util/Iterator;");

    jclass iteratorClass = env->FindClass("java/util/Iterator");
    jmethodID hasNextMethodID = env->GetMethodID(iteratorClass, "hasNext", "()Z");
    jmethodID nextMethodID = env->GetMethodID(iteratorClass, "next", "()Ljava/lang/Object;");

    // Get the iterator for the Java list
    jobject iterator = env->CallObjectMethod(javaList, iteratorMethodID);

    // Loop through the iterator
    while (env->CallBooleanMethod(iterator, hasNextMethodID)) {
        // Get the next element in the iterator
        jstring javaString = (jstring) env->CallObjectMethod(iterator, nextMethodID);

        // Convert jstring to std::string
        const char *nativeString = env->GetStringUTFChars(javaString, nullptr);
        std::string cppString(nativeString);

        // Add the std::string to the vector
        stringVector.push_back(cppString);

        // Release the resources
        env->ReleaseStringUTFChars(javaString, nativeString);
        env->DeleteLocalRef(javaString);  // Clean up the local reference
    }

    // Clean up the iterator local reference
    env->DeleteLocalRef(iterator);

    return stringVector;
}

jobject convertTimestampToInstant(JNIEnv *env, const google::protobuf::Timestamp &timestamp){
    int64_t seconds = timestamp.seconds();
    int32_t nanos = timestamp.nanos();

    // Find the Instant class and method ID for ofEpochSecond
    jclass instantClass = env->FindClass(javaInstantClass.data());
    if (instantClass == nullptr) {
        throwJavaException(env, "Error: Could not find java.time.Instant class");
        return nullptr;
    }

    jmethodID ofEpochSecondMethod = env->GetStaticMethodID(instantClass, "ofEpochSecond", "(JJ)Ljava/time/Instant;");
    if (ofEpochSecondMethod == nullptr) {
        std::cerr << "Error: Could not find ofEpochSecond method in java.time.Instant class." << std::endl;
        return nullptr;
    }

    // Call Instant.ofEpochSecond with seconds and nanoseconds
    jobject instantObject = env->CallStaticObjectMethod(instantClass, ofEpochSecondMethod, static_cast<jlong>(seconds), static_cast<jlong>(nanos));

    // Return the Instant object
    return instantObject;
}

google::protobuf::Timestamp convertInstantToTimestamp(JNIEnv *env, jobject jInstant){
    jclass instantClass = env->GetObjectClass(jInstant);

    // Get the seconds from Instant
    jmethodID getEpochSecondMethod = env->GetMethodID(instantClass, "getEpochSecond", "()J");
    jlong seconds = env->CallLongMethod(jInstant, getEpochSecondMethod);

    // Get the nanoseconds from Instant
    jmethodID getNanoMethod = env->GetMethodID(instantClass, "getNano", "()I");
    jint nanos = env->CallIntMethod(jInstant, getNanoMethod);

    // Convert to google::protobuf::Timestamp
    google::protobuf::Timestamp timestamp;
    timestamp.set_seconds(seconds);
    timestamp.set_nanos(nanos);

    return timestamp;
}

jobject convertJobjectVectorToArrayList(JNIEnv *env, const std::vector<jobject> &jVec){
    // Find the ArrayList class and its constructor
    jclass arrayListClass = env->FindClass(javaArrayListClass.data());
    if (arrayListClass == nullptr) {
        throwJavaException(env, "Error: Could not find ArrayList class.");
        return nullptr;
    }

    jmethodID arrayListConstructor = env->GetMethodID(arrayListClass, "<init>", "()V");
    if (arrayListConstructor == nullptr) {
        throwJavaException(env, "Error: Could not find ArrayList constructor.");
        return nullptr;
    }

    // Create a new ArrayList instance
    jobject arrayList = env->NewObject(arrayListClass, arrayListConstructor);
    if (arrayList == nullptr) {
        throwJavaException(env, "Error: Could not find ArrayList instance.");
        return nullptr;
    }

    // Get the add method ID for ArrayList
    jmethodID arrayListAddMethod = env->GetMethodID(arrayListClass, "add", "(Ljava/lang/Object;)Z");
    if (arrayListAddMethod == nullptr) {
        throwJavaException(env, "Error: Could not find ArrayList add method.");
        return nullptr;
    }
    
    // Add each jobject to the ArrayList
    for (const jobject &javaObject : jVec) {
        env->CallBooleanMethod(arrayList, arrayListAddMethod, javaObject);
        env->DeleteLocalRef(javaObject);  // Release local reference after adding to ArrayList
    }

    return arrayList;
}

jobject convertStringVectorToArrayList(JNIEnv *env, const std::vector<std::string> &vec){
    std::vector<jobject> jObjectVec;

    // Add each element of the std::vector to the ArrayList
    for (const std::string &str : vec) {
        // Convert std::string to jstring
        jobject jStr = env->NewStringUTF(str.c_str());
        jObjectVec.push_back(jStr);
    }
    jobject jArrayList = convertJobjectVectorToArrayList(env, jObjectVec);

    //cleanup
    for(jobject obj : jObjectVec){
        env->DeleteLocalRef(obj);
    }

    return jArrayList;
}

jobject convertBoostIpToInetAddress(JNIEnv *env, const boost::asio::ip::address &address) {
    std::string ipStr = address.to_string();

    jstring jIpStr = env->NewStringUTF(ipStr.c_str());

    // Step 3: Find the InetAddress class
    jclass jInetAddressClass = env->FindClass(javaInetAddressClass.data());
    if (jInetAddressClass == nullptr) {
        throwJavaException(env, "Error: Could not find java.net.InetAddress class.");
        env->DeleteLocalRef(jIpStr);
        return nullptr;
    }

    // Step 4: Get the method ID for InetAddress.getByName(String)
    jmethodID getByNameMethod = env->GetStaticMethodID(jInetAddressClass, "getByName", "(Ljava/lang/String;)Ljava/net/InetAddress;");
    if (getByNameMethod == nullptr) {
        throwJavaException(env, "Error: Could not find InetAddress.getByName method.");
        env->DeleteLocalRef(jIpStr);
        return nullptr;
    }

    // Step 5: Call InetAddress.getByName with the IP string to create the InetAddress object
    jobject jInetAddress = env->CallStaticObjectMethod(jInetAddressClass, getByNameMethod, jIpStr);

    // Clean up local references
    env->DeleteLocalRef(jIpStr);

    return jInetAddress;
}

jobject convertBoostIpAndPortToInetSocketAddress(JNIEnv *env, const boost::asio::ip::address &address, int port) {
    // Step 1: Convert boost::asio::ip::address to std::string
    std::string ipStr = address.to_string();

    // Step 2: Convert std::string to jstring
    jstring jIpStr = env->NewStringUTF(ipStr.c_str());

    // Step 3: Find the InetSocketAddress class
    jclass inetSocketAddressClass = env->FindClass(javaInetSocketAddressClass.data());
    if (inetSocketAddressClass == nullptr) {
        throwJavaException(env, "Error: Could not find java.net.InetSocketAddress class.");
        env->DeleteLocalRef(jIpStr);
        return nullptr;
    }

    // Step 4: Get the constructor ID for InetSocketAddress(String, int)
    jmethodID inetSocketAddressConstructor = env->GetMethodID(inetSocketAddressClass, "<init>", "(Ljava/lang/String;I)V");
    if (inetSocketAddressConstructor == nullptr) {
        throwJavaException(env, "Error: Could not find InetSocketAddress constructor." );
        env->DeleteLocalRef(jIpStr);
        return nullptr;
    }

    // Step 5: Create the InetSocketAddress object using the constructor
    jobject inetSocketAddress = env->NewObject(inetSocketAddressClass, inetSocketAddressConstructor, jIpStr, port);

    // Clean up local references
    env->DeleteLocalRef(jIpStr);

    return inetSocketAddress;
}

std::pair<boost::asio::ip::address, u_int16_t> convertInetSocketAddress(JNIEnv* env, jobject inetSocketAddress) {
    // Step 1: Get the InetSocketAddress class and methods
    jclass inetSocketAddressClass = env->GetObjectClass(inetSocketAddress);

    // Get IP address as a string using getAddress().getHostAddress()
    jmethodID getAddressMethod = env->GetMethodID(inetSocketAddressClass, "getAddress", "()Ljava/net/InetAddress;");
    jobject inetAddress = env->CallObjectMethod(inetSocketAddress, getAddressMethod);

    jclass inetAddressClass = env->GetObjectClass(inetAddress);
    jmethodID getHostAddressMethod = env->GetMethodID(inetAddressClass, "getHostAddress", "()Ljava/lang/String;");
    jstring ipAddressString = (jstring)env->CallObjectMethod(inetAddress, getHostAddressMethod);

    // Convert the Java IP address string to a C++ std::string
    const char* ipAddressChars = env->GetStringUTFChars(ipAddressString, nullptr);
    std::string ipAddress(ipAddressChars);
    env->ReleaseStringUTFChars(ipAddressString, ipAddressChars);

    // Get the port number from InetSocketAddress
    jmethodID getPortMethod = env->GetMethodID(inetSocketAddressClass, "getPort", "()I");
    jint port = env->CallIntMethod(inetSocketAddress, getPortMethod);

    // Step 2: Convert IP address string to boost::asio::ip::address
    boost::asio::ip::address boostAddress = boost::asio::ip::make_address(ipAddress);

    // Step 3: Return as std::pair<boost::asio::ip::address, uint16_t>
    return std::make_pair(boostAddress, static_cast<u_int16_t>(port));
}