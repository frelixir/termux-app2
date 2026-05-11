package com.termux.app.terminal;

import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewConfiguration;
import android.widget.FrameLayout;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.view.GravityCompat;
import androidx.drawerlayout.widget.DrawerLayout;

import com.termux.view.TerminalView;
import com.termux.x11.TermuxScreenView;

public final class MainSurfaceController {
    private static final int INTERNAL_DRAWER_EDGE_INSET_DP = 24;
    private static final int INTERNAL_DRAWER_HOT_ZONE_WIDTH_DP = 72;
    private static final int INTERNAL_DRAWER_MIN_DISTANCE_DP = 96;
    private static final int INTERNAL_DRAWER_MAX_DISTANCE_DP = 220;
    private static final float INTERNAL_DRAWER_SHORT_SIDE_DISTANCE_RATIO = 0.24f;

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
    private int mTrackingInternalDrawerGravity;
    private float mInternalDrawerSwipeDownX;
    private float mInternalDrawerSwipeDownY;
    private final int mInternalDrawerEdgeInset;
    private final int mInternalDrawerHotZoneWidth;
    private final int mInternalDrawerMinDistance;
    private final int mInternalDrawerMaxDistance;
    @Nullable
    private DrawerLayout.DrawerListener mRestoreLockModeOnCloseListener;

    public MainSurfaceController(@NonNull DrawerLayout drawerLayout,
                                 @NonNull FrameLayout container,
                                 @NonNull TerminalView terminalView) {
        mDrawerLayout = drawerLayout;
        mContainer = container;
        mTerminalView = terminalView;

        float density = container.getResources().getDisplayMetrics().density;
        ViewConfiguration viewConfiguration = ViewConfiguration.get(container.getContext());
        mInternalDrawerEdgeInset = Math.round(INTERNAL_DRAWER_EDGE_INSET_DP * density);
        mInternalDrawerHotZoneWidth = Math.round(INTERNAL_DRAWER_HOT_ZONE_WIDTH_DP * density);
        mInternalDrawerMinDistance = Math.max(
            Math.round(INTERNAL_DRAWER_MIN_DISTANCE_DP * density),
            viewConfiguration.getScaledTouchSlop() * 4);
        mInternalDrawerMaxDistance = Math.round(INTERNAL_DRAWER_MAX_DISTANCE_DP * density);

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
        mDrawerLayout.setDrawerLockMode(DrawerLayout.LOCK_MODE_UNLOCKED, GravityCompat.START);
        ensureRestoreLockModeOnCloseListener();
        mDrawerLayout.openDrawer(GravityCompat.START);
    }

    public void openEndDrawerExplicitly() {
        mDrawerLayout.setDrawerLockMode(DrawerLayout.LOCK_MODE_UNLOCKED, GravityCompat.END);
        mDrawerLayout.openDrawer(GravityCompat.END);
    }

    public void toggleStartDrawerExplicitly() {
        if (mDrawerLayout.isDrawerOpen(GravityCompat.START))
            mDrawerLayout.closeDrawer(GravityCompat.START);
        else
            openStartDrawerExplicitly();
    }

    public void restoreDrawerLockMode() {
        applyDrawerLockMode();
    }

    public void openCurrentSurfaceDrawerExplicitly() {
        if (mMode == SurfaceMode.TERMINAL) {
            openStartDrawerExplicitly();
        } else if (mMode == SurfaceMode.DISPLAY && mDisplayView != null) {
            openEndDrawerExplicitly();
        }
    }

    public void handleInternalDrawerSwipe(@NonNull MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                mTrackingInternalDrawerGravity = getInternalDrawerSwipeGravity(event);
                mInternalDrawerSwipeDownX = event.getRawX();
                mInternalDrawerSwipeDownY = event.getRawY();
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_CANCEL:
                mTrackingInternalDrawerGravity = 0;
                break;
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_UP:
                if (mTrackingInternalDrawerGravity == 0)
                    return;
                if (shouldOpenDrawerFromInternalSwipe(event, mTrackingInternalDrawerGravity)) {
                    int drawerGravity = mTrackingInternalDrawerGravity;
                    mTrackingInternalDrawerGravity = 0;
                    if (drawerGravity == GravityCompat.START)
                        openStartDrawerExplicitly();
                    else
                        openEndDrawerExplicitly();
                } else if (event.getActionMasked() == MotionEvent.ACTION_UP) {
                    mTrackingInternalDrawerGravity = 0;
                }
                break;
        }
    }

    private void applyMode() {
        mTerminalView.setVisibility(mMode == SurfaceMode.TERMINAL ? View.VISIBLE : View.GONE);
        if (mDisplayView != null)
            mDisplayView.setVisibility(mMode == SurfaceMode.DISPLAY ? View.VISIBLE : View.GONE);
        applyDrawerLockMode();
    }

    private boolean canOpenDrawerFromInternalSwipe(int drawerGravity) {
        if (mDrawerLayout.isDrawerOpen(GravityCompat.START) || mDrawerLayout.isDrawerOpen(GravityCompat.END))
            return false;
        if (mTerminalCopyMode)
            return false;
        if (drawerGravity == GravityCompat.START)
            return mMode == SurfaceMode.TERMINAL;
        if (drawerGravity == GravityCompat.END)
            return mMode == SurfaceMode.DISPLAY;
        return false;
    }

    private boolean isTouchInsideContainer(@NonNull MotionEvent event) {
        int[] location = new int[2];
        mContainer.getLocationOnScreen(location);
        float x = event.getRawX();
        float y = event.getRawY();
        return x >= location[0]
            && x <= location[0] + mContainer.getWidth()
            && y >= location[1]
            && y <= location[1] + mContainer.getHeight();
    }

    private int getInternalDrawerSwipeGravity(@NonNull MotionEvent event) {
        if (!isTouchInsideContainer(event))
            return 0;
        if (canOpenDrawerFromInternalSwipe(GravityCompat.START) && isInInternalDrawerHotZone(event, GravityCompat.START))
            return GravityCompat.START;
        if (canOpenDrawerFromInternalSwipe(GravityCompat.END) && isInInternalDrawerHotZone(event, GravityCompat.END))
            return GravityCompat.END;
        return 0;
    }

    private boolean isInInternalDrawerHotZone(@NonNull MotionEvent event, int drawerGravity) {
        int[] location = new int[2];
        mContainer.getLocationOnScreen(location);
        float x = event.getRawX() - location[0];
        int width = mContainer.getWidth();
        boolean rtl = mContainer.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
        boolean useRightEdge = (drawerGravity == GravityCompat.START && rtl) || (drawerGravity == GravityCompat.END && !rtl);

        if (useRightEdge)
            return x <= width - mInternalDrawerEdgeInset
                && x >= width - mInternalDrawerEdgeInset - mInternalDrawerHotZoneWidth;

        return x >= mInternalDrawerEdgeInset
            && x <= mInternalDrawerEdgeInset + mInternalDrawerHotZoneWidth;
    }

    private boolean shouldOpenDrawerFromInternalSwipe(@NonNull MotionEvent event, int drawerGravity) {
        boolean rtl = mContainer.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
        boolean opensFromRight = (drawerGravity == GravityCompat.START && rtl) || (drawerGravity == GravityCompat.END && !rtl);
        float inwardDistance = opensFromRight
            ? mInternalDrawerSwipeDownX - event.getRawX()
            : event.getRawX() - mInternalDrawerSwipeDownX;
        float verticalDistance = Math.abs(event.getRawY() - mInternalDrawerSwipeDownY);
        int shortSide = Math.min(mContainer.getWidth(), mContainer.getHeight());
        int minDistance = Math.max(
            mInternalDrawerMinDistance,
            Math.min(Math.round(shortSide * INTERNAL_DRAWER_SHORT_SIDE_DISTANCE_RATIO), mInternalDrawerMaxDistance));
        return inwardDistance >= minDistance
            && inwardDistance > verticalDistance * 1.25f;
    }

    private void applyDrawerLockMode() {
        boolean terminalCanOpenStart = mMode == SurfaceMode.TERMINAL && !mTerminalCopyMode;
        boolean displayCanOpenEnd = mMode == SurfaceMode.DISPLAY;
        mDrawerLayout.setDrawerLockMode(
            terminalCanOpenStart ? DrawerLayout.LOCK_MODE_UNLOCKED : DrawerLayout.LOCK_MODE_LOCKED_CLOSED,
            GravityCompat.START);
        mDrawerLayout.setDrawerLockMode(
            displayCanOpenEnd ? DrawerLayout.LOCK_MODE_UNLOCKED : DrawerLayout.LOCK_MODE_LOCKED_CLOSED,
            GravityCompat.END);
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
