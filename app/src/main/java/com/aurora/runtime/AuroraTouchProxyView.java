package com.aurora.runtime;

import android.content.Context;
import android.view.MotionEvent;
import android.view.View;
import android.view.KeyEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/** Full-screen input shield; only a captured sequence that starts in Aurora is forwarded. */
final class AuroraTouchProxyView extends View {
    private final View renderView;
    private final int[] renderOrigin = new int[2];
    private final int[] proxyOrigin = new int[2];
    private boolean imeRequested;
    private boolean forwardingSequence;
    private float interactiveLeft;
    private float interactiveTop;
    private float interactiveRight;
    private float interactiveBottom;

    AuroraTouchProxyView(Context context, View renderView) {
        super(context);
        this.renderView = renderView;
        setBackgroundColor(android.graphics.Color.TRANSPARENT);
        setFocusable(false);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN) {
            forwardingSequence = containsInteractive(event.getX(index), event.getY(index));
        }
        if (forwardingSequence) {
            forwardEvent(event, index);
            postDelayed(this::syncIme, 24L);
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            forwardingSequence = false;
        }
        // The shield consumes every event, including touches outside Aurora's panel.
        return true;
    }

    private boolean containsInteractive(float x, float y) {
        return interactiveRight > interactiveLeft && interactiveBottom > interactiveTop
                && x >= interactiveLeft && x < interactiveRight
                && y >= interactiveTop && y < interactiveBottom;
    }

    private void forwardEvent(MotionEvent event, int index) {
        renderView.getLocationOnScreen(renderOrigin);
        getLocationOnScreen(proxyOrigin);
        final float offsetX = proxyOrigin[0] - renderOrigin[0];
        final float offsetY = proxyOrigin[1] - renderOrigin[1];
        final int action = event.getActionMasked();
        final double timeSeconds = event.getEventTime() * 0.001;
        if (action == MotionEvent.ACTION_MOVE) {
            for (int pointer = 0; pointer < event.getPointerCount(); pointer++) {
                nativeTouch(action, event.getPointerId(pointer),
                        event.getX(pointer) + offsetX, event.getY(pointer) + offsetY,
                        event.getPressure(pointer), event.getSize(pointer),
                        event.getToolType(pointer), timeSeconds);
            }
        } else if (action == MotionEvent.ACTION_CANCEL) {
            nativeTouch(action, -1, 0.0f, 0.0f, 0.0f, 0.0f, 0, timeSeconds);
        } else {
            nativeTouch(action, event.getPointerId(index),
                    event.getX(index) + offsetX, event.getY(index) + offsetY,
                    event.getPressure(index), event.getSize(index),
                    event.getToolType(index), timeSeconds);
        }
    }

    void setInteractiveRegion(float left, float top, float right, float bottom) {
        interactiveLeft = left;
        interactiveTop = top;
        interactiveRight = right;
        interactiveBottom = bottom;
    }

    void setImeRequested(boolean requested) {
        if (imeRequested == requested) return;
        imeRequested = requested;
        if (requested) {
            setFocusable(true);
            setFocusableInTouchMode(true);
            requestFocus();
            postDelayed(this::syncIme, 24L);
        } else {
            final InputMethodManager manager = inputMethodManager();
            if (manager != null && hasWindowFocus()) {
                manager.hideSoftInputFromWindow(getWindowToken(), 0);
            }
            clearFocus();
            setFocusable(false);
            setFocusableInTouchMode(false);
        }
    }

    private void syncIme() {
        if (!imeRequested || !isAttachedToWindow() || !hasWindowFocus()) return;
        final InputMethodManager manager = inputMethodManager();
        if (manager != null) {
            manager.restartInput(this);
            manager.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) postDelayed(this::syncIme, 24L);
    }

    @Override
    protected void onFocusChanged(boolean focused, int direction,
                                  android.graphics.Rect previouslyFocusedRect) {
        super.onFocusChanged(focused, direction, previouslyFocusedRect);
        if (focused) postDelayed(this::syncIme, 24L);
    }

    private InputMethodManager inputMethodManager() {
        return (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return imeRequested;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        if (!imeRequested) return null;
        outAttrs.inputType = android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE;
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
                for (int i = 0; i < Math.max(1, beforeLength); i++) nativeDeleteBackward();
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

    private static native void nativeTouch(int action, int pointerId, float x, float y, float pressure,
                                           float size, int toolType, double timeSeconds);
    static native float[] nativeGetInteractiveRegion();
    static native boolean nativeWantsTextInput();
    private static native void nativeText(String text);
    private static native void nativeDeleteBackward();
}
