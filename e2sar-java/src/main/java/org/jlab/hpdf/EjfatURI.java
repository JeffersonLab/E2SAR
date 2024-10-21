import org.bytedeco.javacpp.*;
import org.bytedeco.javacpp.annotation.*;
import org.bytedeco.javacpp.tools.*;

@Properties(
    value = @Platform(
        include = {"e2sarUtil.hpp", "e2sar.hpp"},
        link = "e2sar",
        includepath = {"/opt/homebrew/include"},
        linkpath = {"/opt/homebrew/lib"},
        compiler = "cpp17"
        )
)
@Namespace("e2sar")
public class EjfatURI extends Pointer {

    // Declare the native methods for memory allocation and deallocation
    private native void allocate(@Cast("const std::string&") String uri);

    public native @StdString String get_lbName();

    static {
        // Load the native library
        Loader.load();
    }

    public EjfatURI(String uri){
        allocate(uri);
    }

    public static void main(String[] args) {
        EjfatURI ejfatURI = new EjfatURI("ejfat://token@192.188.29.6:18020/lb/36?sync=192.188.29.6:19020&data=192.188.29.20");
        
        // Get the URI
        System.out.println("get_lbName: " + ejfatURI.get_lbName());
    }
}