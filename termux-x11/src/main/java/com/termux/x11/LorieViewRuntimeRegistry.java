package com.termux.x11;

import androidx.annotation.Keep;
import androidx.annotation.Nullable;

import java.lang.ref.WeakReference;

public final class LorieViewRuntimeRegistry {
    private static final LorieViewRuntimeRegistry INSTANCE = new LorieViewRuntimeRegistry();
    private static WeakReference<LorieViewRuntimeController> runtime = new WeakReference<>(null);

    private LorieViewRuntimeRegistry() {
    }

    static void register(LorieViewRuntimeController controller) {
        runtime = new WeakReference<>(controller);
    }

    static void unregister(LorieViewRuntimeController controller) {
        LorieViewRuntimeController current = runtime.get();
        if (current == controller)
            runtime.clear();
    }

    public static boolean hasLiveInstance() {
        return runtime.get() != null;
    }

    @Keep
    @Nullable
    public static LorieViewRuntimeRegistry getInstance() {
        return hasLiveInstance() ? INSTANCE : null;
    }

    @Keep
    void clientConnectedStateChanged() {
        LorieViewRuntimeController controller = runtime.get();
        if (controller != null)
            controller.clientConnectedStateChanged();
    }

    @Keep
    void onRenderConnectionChanged() {
        LorieViewRuntimeController controller = runtime.get();
        if (controller != null)
            controller.onRenderConnectionChanged();
    }
}
