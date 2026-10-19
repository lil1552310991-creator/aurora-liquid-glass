package com.aurora.runtime;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.IBinder;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.WindowManager;

public final class AuroraOverlayService extends Service {
    private static final String CHANNEL_ID = "aurora_overlay";
    private static final int NOTIFICATION_ID = 1001;
    private static final String ACTION_STOP = "com.aurora.runtime.action.STOP_OVERLAY";

    private WindowManager windowManager;
    private AuroraSurfaceView surfaceView;
    private AuroraTouchProxyView touchProxy;
    private final int[] renderOrigin = new int[2];
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Runnable touchRegionSync = new Runnable() {
        @Override
        public void run() {
            syncTouchProxy();
            if (touchProxy != null) {
                mainHandler.postDelayed(this, 16L);
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            stopSelf();
            return;
        }
        createNotificationChannel();
        startForeground(NOTIFICATION_ID, buildNotification());

        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        surfaceView = new AuroraSurfaceView(this, true);
        final int windowType = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
        final WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                windowType,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = 0;
        params.y = 0;
        // The GLES surface clears untouched pixels to alpha zero; keep the window opaque
        // so transparent pixels do not dim the application behind the overlay.
        params.alpha = 1.0f;
        params.setTitle("Aurora Render");
        windowManager.addView(surfaceView, params);

        touchProxy = new AuroraTouchProxyView(this, surfaceView);
        final WindowManager.LayoutParams touchParams = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.MATCH_PARENT,
                windowType,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                PixelFormat.TRANSPARENT);
        touchParams.gravity = Gravity.TOP | Gravity.START;
        touchParams.x = 0;
        touchParams.y = 0;
        touchParams.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN;
        touchParams.setTitle("Aurora Input Shield");
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            touchParams.setFitInsetsTypes(0);
            touchParams.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        }
        windowManager.addView(touchProxy, touchParams);
        mainHandler.post(touchRegionSync);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && "glass".equalsIgnoreCase(intent.getStringExtra("showcase"))
                && surfaceView != null) {
            surfaceView.setInitialShowcase(0);
        }
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopSelf();
            return START_NOT_STICKY;
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        mainHandler.removeCallbacks(touchRegionSync);
        stopService(new Intent(this, AuroraProjectionService.class));
        if (surfaceView != null && windowManager != null) {
            if (touchProxy != null) {
                mainHandler.removeCallbacks(touchRegionSync);
                try {
                    windowManager.removeViewImmediate(touchProxy);
                } catch (IllegalArgumentException ignored) {
                    // The proxy may already have been removed during teardown.
                }
                touchProxy = null;
            }
            try {
                windowManager.removeViewImmediate(surfaceView);
            } catch (IllegalArgumentException ignored) {
                // The host may already have removed the Surface during teardown.
            }
            surfaceView = null;
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    private void syncTouchProxy() {
        if (touchProxy == null || windowManager == null || surfaceView == null) {
            return;
        }
        surfaceView.getLocationOnScreen(renderOrigin);
        final int[] proxyOrigin = new int[2];
        touchProxy.getLocationOnScreen(proxyOrigin);
        final int surfaceWidth = surfaceView.getWidth();
        final int surfaceHeight = surfaceView.getHeight();
        final float[] region = AuroraTouchProxyView.nativeGetInteractiveRegion();
        final int[] bounds = OverlayGeometry.map(region, surfaceWidth, surfaceHeight,
                renderOrigin[0], renderOrigin[1]);
        if (bounds == null) {
            touchProxy.setInteractiveRegion(0, 0, 0, 0);
        } else {
            touchProxy.setInteractiveRegion(
                    bounds[0] - proxyOrigin[0],
                    bounds[1] - proxyOrigin[1],
                    bounds[0] - proxyOrigin[0] + bounds[2],
                    bounds[1] - proxyOrigin[1] + bounds[3]);
        }

        final WindowManager.LayoutParams params = (WindowManager.LayoutParams) touchProxy.getLayoutParams();
        final boolean wantsIme = bounds != null && AuroraTouchProxyView.nativeWantsTextInput();
        final boolean notFocusable = (params.flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE) != 0;
        if (wantsIme == notFocusable) {
            if (wantsIme) {
                params.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            } else {
                params.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            }
            windowManager.updateViewLayout(touchProxy, params);
        }
        touchProxy.setImeRequested(wantsIme);
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        stopSelf();
        super.onTaskRemoved(rootIntent);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            final NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Aurora overlay", NotificationManager.IMPORTANCE_LOW);
            final NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private Notification buildNotification() {
        final Intent stopIntent = new Intent(this, AuroraOverlayService.class).setAction(ACTION_STOP);
        final PendingIntent stopPendingIntent = PendingIntent.getService(
                this, 0, stopIntent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        final Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder
                .setContentTitle("Aurora overlay")
                .setContentText("Aurora UI host is running")
                .setSmallIcon(android.R.drawable.ic_menu_view)
                .addAction(new Notification.Action.Builder(
                        android.graphics.drawable.Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel),
                        "Stop", stopPendingIntent).build())
                .setOngoing(true)
                .build();
    }
}
