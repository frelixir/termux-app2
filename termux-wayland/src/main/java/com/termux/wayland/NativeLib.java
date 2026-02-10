package com.termux.wayland;

public class NativeLib {

    // Used to load the 'wayland' library on application startup.
    static {
        System.loadLibrary("wayland");
    }

    /**
     * A native method that is implemented by the 'wayland' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
}
