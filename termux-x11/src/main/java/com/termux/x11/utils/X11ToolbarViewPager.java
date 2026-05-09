package com.termux.x11.utils;

import com.termux.x11.LorieViewRuntimeApi;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.KeyEvent;
import android.view.KeyCharacterMap;

import android.widget.Button;
import android.widget.EditText;

import androidx.annotation.NonNull;
import androidx.viewpager.widget.PagerAdapter;
import androidx.viewpager.widget.ViewPager;

import com.termux.x11.extrakeys.TermuxExtraKeysView;
import com.termux.x11.R;

public class X11ToolbarViewPager {
    public static class PageAdapter extends PagerAdapter {

        final LorieViewRuntimeApi.ToolbarHost mHost;
        private final View.OnKeyListener mEventListener;

        public PageAdapter(LorieViewRuntimeApi.ToolbarHost host, View.OnKeyListener listen) {
            this.mHost = host;
            this.mEventListener = listen;
        }

        @Override
        public int getCount() {
            return 2;
        }

        @Override
        public boolean isViewFromObject(@NonNull View view, @NonNull Object object) {
            return view == object;
        }

        @SuppressLint("ClickableViewAccessibility")
        @NonNull
        @Override
        public Object instantiateItem(@NonNull ViewGroup collection, int position) {
            LayoutInflater inflater = LayoutInflater.from(mHost.getActivity());
            View layout;
            if (position == 0) {
                layout = inflater.inflate(R.layout.display_view_terminal_toolbar_extra_keys, collection, false);
                TermuxExtraKeysView termuxExtraKeysView = (TermuxExtraKeysView) layout;
                TermuxX11ExtraKeys extraKeys = new TermuxX11ExtraKeys(mEventListener, mHost, termuxExtraKeysView);
                mHost.setTermuxX11ExtraKeys(extraKeys);
                int mTerminalToolbarDefaultHeight = mHost.getDisplayTerminalToolbarViewPager().getLayoutParams().height;
                int height = mTerminalToolbarDefaultHeight *
                        ((extraKeys.getExtraKeysInfo() == null) ? 0 : extraKeys.getExtraKeysInfo().getMatrix().length);
                termuxExtraKeysView.reload(extraKeys.getExtraKeysInfo(), height);
                termuxExtraKeysView.setExtraKeysViewClient(extraKeys);
                termuxExtraKeysView.setOnHoverListener((v, e) -> true);
                termuxExtraKeysView.setOnGenericMotionListener((v, e) -> true);
            } else {
                layout = inflater.inflate(R.layout.display_view_terminal_toolbar_text_input, collection, false);
                final EditText editText = layout.findViewById(R.id.display_terminal_toolbar_text_input);
                final Button back = layout.findViewById(R.id.display_terminal_toolbar_back_button);

                editText.setOnEditorActionListener((v, actionId, event) -> {
                    String textToSend = editText.getText().toString();
                    if (textToSend.length() == 0) textToSend = "\r";
                    KeyEvent e = new KeyEvent(0, textToSend, KeyCharacterMap.VIRTUAL_KEYBOARD, 0);
                    mEventListener.onKey(mHost.getLorieView(), 0, e);

                    editText.setText("");
                    return true;
                });

                editText.setOnCapturedPointerListener((v2, e2) -> {
                    v2.releasePointerCapture();
                    return false;
                });

                back.setOnClickListener(v -> mHost.getDisplayTerminalToolbarViewPager().setCurrentItem(0, true));
                back.setTextColor(0xFFFFFFFF);
                back.setPadding(0, 0, 0, 0);
                back.setBackground(new ColorDrawable(Color.BLACK) {
                    public boolean isStateful() {
                        return true;
                    }
                    public boolean hasFocusStateSpecified() {
                        return true;
                    }
                });
                back.setOnTouchListener((view, event) -> {
                    switch (event.getAction()) {
                        case MotionEvent.ACTION_DOWN:
                            view.setBackgroundColor(0xFF7F7F7F);
                            break;

                        case MotionEvent.ACTION_UP:
                        case MotionEvent.ACTION_CANCEL:
                            view.setBackgroundColor(0x00000000);
                            break;
                    }
                    return false;
                });
            }
            collection.addView(layout);
            return layout;
        }

        @Override
        public void destroyItem(@NonNull ViewGroup collection, int position, @NonNull Object view) {
            collection.removeView((View) view);
        }

    }

    public static class OnPageChangeListener extends ViewPager.SimpleOnPageChangeListener {

        final LorieViewRuntimeApi.ToolbarHost mHost;
        final ViewPager mTerminalToolbarViewPager;

        public OnPageChangeListener(LorieViewRuntimeApi.ToolbarHost host, ViewPager viewPager) {
            this.mHost = host;
            this.mTerminalToolbarViewPager = viewPager;
        }

        @Override
        public void onPageSelected(int position) {
            if (position == 0) {
                mHost.getLorieView().requestFocus();
            } else {
                final EditText editText = mTerminalToolbarViewPager.findViewById(R.id.display_terminal_toolbar_text_input);
                if (editText != null) editText.requestFocus();
            }
        }
    }
}
