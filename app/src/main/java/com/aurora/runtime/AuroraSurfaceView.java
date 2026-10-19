package com.aurora.runtime;

import android.content.Context;
import android.graphics.PixelFormat;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.BaseInputConnection;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.KeyEvent;

final class AuroraSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("aurora");
    }

    AuroraSurfaceView(Context context) {
        this(context, false, 0);
    }

    AuroraSurfaceView(Context context, boolean overlayHost) {
        this(context, overlayHost, 0);
    }

    AuroraSurfaceView(Context context, boolean overlayHost, int backendKind) {
        super(context);
        if (overlayHost) {
            getHolder().setFormat(PixelFormat.TRANSLUCENT);
            setZOrderOnTop(true);
            setBackgroundColor(android.graphics.Color.TRANSPARENT);
        }
        nativeSetHostMode(overlayHost ? 1 : 0);
        nativeSetBackendKind(overlayHost ? 0 : backendKind);
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    void setInitialShowcase(int tab) {
        nativeSetInitialTab(tab);
        nativeSetLiquidGlassVisible(true);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        nativeSurfaceCreated(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeSurfaceChanged(width, height, getResources().getDisplayMetrics().density);
        post(this::syncSafeRegion);
    }

    private void syncSafeRegion() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            final WindowInsets insets = getRootWindowInsets();
            if (insets != null) {
                final android.graphics.Insets safe = insets.getInsets(
                        WindowInsets.Type.displayCutout() | WindowInsets.Type.navigationBars());
                final float density = getResources().getDisplayMetrics().density;
                nativeSafeRegion(safe.left / density, safe.top / density,
                        (getWidth() - safe.right) / density, (getHeight() - safe.bottom) / density);
            }
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSurfaceDestroyed();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        final double timeSeconds = event.getEventTime() * 0.001;
        if (action == MotionEvent.ACTION_MOVE) {
            for (int pointer = 0; pointer < event.getPointerCount(); pointer++) {
                nativeTouch(action, event.getPointerId(pointer), event.getX(pointer), event.getY(pointer),
                        event.getPressure(pointer), event.getSize(pointer), event.getToolType(pointer), timeSeconds);
            }
        } else if (action == MotionEvent.ACTION_CANCEL) {
            nativeTouch(action, -1, 0.0f, 0.0f, 0.0f, 0.0f, 0, timeSeconds);
        } else {
            nativeTouch(action, event.getPointerId(index), event.getX(index), event.getY(index),
                    event.getPressure(index), event.getSize(index), event.getToolType(index), timeSeconds);
        }
        postDelayed(this::syncIme, 16L);
        return true;
    }

    private void syncIme() {
        if (nativeWantsTextInput()) {
            focusSurfaceForIme();
            InputMethodManager manager = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            if (manager != null) {
                manager.restartInput(this);
                manager.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
            }
        }
    }

    private void focusSurfaceForIme() {
        if (!hasFocus()) {
            requestFocus();
        }
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE;
        outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE;
        return new BaseInputConnection(this, false) {
            @Override
            public boolean commitText(CharSequence text, int newCursorPosition) {
                nativeText(text == null ? "" : text.toString());
                return true;
            }

            @Override
            public boolean setComposingText(CharSequence text, int newCursorPosition) {
                nativeText(text == null ? "" : text.toString());
                return true;
            }

            @Override
            public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                for (int i = 0; i < Math.max(1, beforeLength); i++) {
                    nativeDeleteBackward();
                }
                return true;
            }

            @Override
            public boolean sendKeyEvent(KeyEvent event) {
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
                        nativeDeleteBackward();
                        return true;
                    }
                    final int unicode = event.getUnicodeChar();
                    if (unicode != 0) {
                        nativeText(new String(Character.toChars(unicode)));
                        return true;
                    }
                }
                return super.sendKeyEvent(event);
            }
        };
    }

    @Override
    protected void onFocusChanged(boolean focused, int direction, android.graphics.Rect previouslyFocusedRect) {
        super.onFocusChanged(focused, direction, previouslyFocusedRect);
        if (focused) {
            postDelayed(this::syncIme, 16L);
        }
    }

    void pauseRenderer() {
        nativePause();
    }

    void resumeRenderer() {
        nativeResume();
    }

    private static native void nativeSurfaceCreated(Surface surface);
    private static native void nativeSetHostMode(int mode);
    private static native void nativeSetBackendKind(int kind);
    private static native void nativeSetInitialTab(int tab);
    private static native void nativeSetLiquidGlassVisible(boolean visible);
    private static native void nativeSurfaceChanged(int width, int height, float density);
    private static native void nativeSafeRegion(float left, float top, float right, float bottom);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeTouch(int action, int pointerId, float x, float y, float pressure,
                                           float size, int toolType, double timeSeconds);
    private static native void nativeText(String text);
    private static native void nativeDeleteBackward();
    private static native boolean nativeWantsTextInput();

    private static native void nativePause();
    private static native void nativeResume();
}
