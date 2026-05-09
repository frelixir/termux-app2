package com.termux.app.terminal;

import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.view.GravityCompat;
import androidx.drawerlayout.widget.DrawerLayout;

import com.termux.view.TerminalView;
import com.termux.x11.TermuxScreenView;

public final class MainSurfaceController {
    private static final long DISPLAY_START_DRAWER_UNLOCK_TIMEOUT_MS = 5000;

    public enum SurfaceMode {
        TERMINAL,
        DISPLAY
    }

    @NonNull
    private final DrawerLayout mDrawerLayout;
    @NonNull
    private final FrameLayout mContainer;
    @NonNull
    private final TerminalView mTerminalView;
    @Nullable
    private TermuxScreenView mDisplayView;
    @NonNull
    private SurfaceMode mMode = SurfaceMode.TERMINAL;
    private boolean mTerminalCopyMode;
    private boolean mDisplayStartDrawerGestureUnlocked;
    @Nullable
    private DrawerLayout.DrawerListener mRestoreLockModeOnCloseListener;
    @NonNull
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    @NonNull
    private final Runnable mLockDisplayStartDrawerGestureRunnable = this::lockDisplayStartDrawerGesture;

    public MainSurfaceController(@NonNull DrawerLayout drawerLayout,
                                 @NonNull FrameLayout container,
                                 @NonNull TerminalView terminalView) {
        mDrawerLayout = drawerLayout;
        mContainer = container;
        mTerminalView = terminalView;
        applyMode();
    }

    public void attachDisplayView(@NonNull TermuxScreenView displayView) {
        if (mDisplayView == displayView)
            return;

        if (displayView.getParent() != mContainer) {
            if (displayView.getParent() instanceof ViewGroup)
                ((ViewGroup) displayView.getParent()).removeView(displayView);
            mContainer.addView(displayView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        }

        mDisplayView = displayView;
        applyMode();
    }

    public void detachDisplayView() {
        if (mDisplayView != null && mDisplayView.getParent() == mContainer)
            mContainer.removeView(mDisplayView);
        mDisplayView = null;
        if (mMode == SurfaceMode.DISPLAY)
            showTerminal();
    }

    public void showTerminal() {
        mMode = SurfaceMode.TERMINAL;
        applyMode();
        mTerminalView.requestFocus();
    }

    public void showDisplay() {
        if (mDisplayView == null)
            return;
        mMode = SurfaceMode.DISPLAY;
        applyMode();
        mDisplayView.getLorieView().requestFocus();
    }

    @NonNull
    public SurfaceMode getMode() {
        return mMode;
    }

    public boolean isDisplayMode() {
        return mMode == SurfaceMode.DISPLAY;
    }

    public void setTerminalCopyMode(boolean copyMode) {
        mTerminalCopyMode = copyMode;
        applyDrawerLockMode();
    }

    public void openStartDrawerExplicitly() {
        mHandler.removeCallbacks(mLockDisplayStartDrawerGestureRunnable);
        mDrawerLayout.setDrawerLockMode(DrawerLayout.LOCK_MODE_UNLOCKED, GravityCompat.START);
        ensureRestoreLockModeOnCloseListener();
        mDrawerLayout.openDrawer(GravityCompat.START);
    }

    public void toggleStartDrawerExplicitly() {
        if (mDrawerLayout.isDrawerOpen(GravityCompat.START))
            mDrawerLayout.closeDrawer(GravityCompat.START);
        else
            openStartDrawerExplicitly();
    }

    public void restoreDrawerLockMode() {
        setDisplayStartDrawerGestureUnlocked(false);
        mHandler.removeCallbacks(mLockDisplayStartDrawerGestureRunnable);
        applyDrawerLockMode();
    }

    public void unlockDisplayStartDrawerGestureTemporarily() {
        if (mMode != SurfaceMode.DISPLAY || mDisplayView == null)
            return;

        setDisplayStartDrawerGestureUnlocked(true);
        applyDrawerLockMode();
        ensureRestoreLockModeOnCloseListener();
        mHandler.removeCallbacks(mLockDisplayStartDrawerGestureRunnable);
        mHandler.postDelayed(mLockDisplayStartDrawerGestureRunnable, DISPLAY_START_DRAWER_UNLOCK_TIMEOUT_MS);
    }

    private void applyMode() {
        if (mMode != SurfaceMode.DISPLAY)
            setDisplayStartDrawerGestureUnlocked(false);
        mTerminalView.setVisibility(mMode == SurfaceMode.TERMINAL ? View.VISIBLE : View.GONE);
        if (mDisplayView != null)
            mDisplayView.setVisibility(mMode == SurfaceMode.DISPLAY ? View.VISIBLE : View.GONE);
        applyDrawerLockMode();
    }

    private void applyDrawerLockMode() {
        int lockMode = mDisplayStartDrawerGestureUnlocked
            ? DrawerLayout.LOCK_MODE_UNLOCKED
            : (mMode == SurfaceMode.DISPLAY || mTerminalCopyMode)
            ? DrawerLayout.LOCK_MODE_LOCKED_CLOSED
            : DrawerLayout.LOCK_MODE_UNLOCKED;
        mDrawerLayout.setDrawerLockMode(lockMode, GravityCompat.START);
    }

    private void lockDisplayStartDrawerGesture() {
        setDisplayStartDrawerGestureUnlocked(false);
        if (mDrawerLayout.isDrawerOpen(GravityCompat.START))
            return;

        if (mRestoreLockModeOnCloseListener != null) {
            mDrawerLayout.removeDrawerListener(mRestoreLockModeOnCloseListener);
            mRestoreLockModeOnCloseListener = null;
        }
        applyDrawerLockMode();
    }

    private void setDisplayStartDrawerGestureUnlocked(boolean unlocked) {
        mDisplayStartDrawerGestureUnlocked = unlocked && mMode == SurfaceMode.DISPLAY && mDisplayView != null;
        if (mDisplayView != null)
            mDisplayView.setStartDrawerGestureUnlocked(mDisplayStartDrawerGestureUnlocked);
    }

    private void ensureRestoreLockModeOnCloseListener() {
        if (mRestoreLockModeOnCloseListener != null)
            return;

        mRestoreLockModeOnCloseListener = new DrawerLayout.SimpleDrawerListener() {
            @Override
            public void onDrawerClosed(@NonNull View drawerView) {
                if (mRestoreLockModeOnCloseListener == null)
                    return;
                mDrawerLayout.removeDrawerListener(mRestoreLockModeOnCloseListener);
                mRestoreLockModeOnCloseListener = null;
                restoreDrawerLockMode();
            }
        };
        mDrawerLayout.addDrawerListener(mRestoreLockModeOnCloseListener);
    }
}
