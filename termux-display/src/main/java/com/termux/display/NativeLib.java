package com.termux.display;

public class NativeLib {

    // Used to load the 'display' library on application startup.
    static {
        System.loadLibrary("display");
    }

    /**
     * A native method that is implemented by the 'display' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
}
