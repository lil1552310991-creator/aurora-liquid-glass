package com.aurora.runtime;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.graphics.Rect;
import android.util.DisplayMetrics;
import android.view.Surface;
import java.nio.ByteBuffer;

/** User-authorized desktop frame producer for the native glass host. */
public final class AuroraProjectionService extends Service {
    static final String EXTRA_RESULT_CODE = "projection_result_code";
    static final String EXTRA_RESULT_DATA = "projection_result_data";
    private static final String CHANNEL_ID = "aurora_projection";
    private static final int NOTIFICATION_ID = 1002;
    static { System.loadLibrary("aurora"); }

    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private ImageReader imageReader;
    private HandlerThread frameThread;
    private Handler frameHandler;
    private final MediaProjection.Callback callback = new MediaProjection.Callback() {
        @Override public void onStop() { stopSelf(); }
    };

    @Override public void onCreate() {
        super.onCreate();
        createChannel();
        final Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("Aurora desktop reflection")
                .setContentText("Screen sharing is active for liquid glass")
                .setSmallIcon(android.R.drawable.ic_menu_view).setOngoing(true).build();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification,
                    android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
        } else startForeground(NOTIFICATION_ID, notification);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (projection != null) return START_STICKY;
        if (intent == null) { stopSelf(); return START_NOT_STICKY; }
        final int resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, -1);
        final Intent data = intent.getParcelableExtra(EXTRA_RESULT_DATA);
        if (resultCode != android.app.Activity.RESULT_OK || data == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        final MediaProjectionManager manager =
                (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        if (manager == null) { stopSelf(); return START_NOT_STICKY; }
        projection = manager.getMediaProjection(resultCode, data);
        if (projection == null) { stopSelf(); return START_NOT_STICKY; }
        projection.registerCallback(callback, new Handler(getMainLooper()));
        startCapture();
        return START_STICKY;
    }

    private void startCapture() {
        final DisplayMetrics metrics = getResources().getDisplayMetrics();
        final int width = Math.max(2, Math.min(720, metrics.widthPixels));
        final int height = Math.max(2, Math.round(width * (metrics.heightPixels / (float) metrics.widthPixels)));
        final int dpi = Math.max(160, metrics.densityDpi);
        imageReader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 2);
        frameThread = new HandlerThread("AuroraProjectionFrames");
        frameThread.start();
        frameHandler = new Handler(frameThread.getLooper());
        imageReader.setOnImageAvailableListener(reader -> consume(reader, width, height), frameHandler);
        final Surface surface = imageReader.getSurface();
        virtualDisplay = projection.createVirtualDisplay("AuroraGlassProjection", width, height, dpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, surface, null, frameHandler);
    }

    private void consume(ImageReader reader, int width, int height) {
        try (Image image = reader.acquireLatestImage()) {
            if (image == null) return;
            final Image.Plane plane = image.getPlanes()[0];
            final ByteBuffer source = plane.getBuffer();
            final int pixelStride = plane.getPixelStride();
            final int rowStride = plane.getRowStride();
            final Rect crop = image.getCropRect();
            final int cropWidth = Math.min(width, Math.max(1, crop.width()));
            final int cropHeight = Math.min(height, Math.max(1, crop.height()));
            if (pixelStride < 4 || rowStride < crop.right * pixelStride) return;
            final int rowBytes = cropWidth * 4;
            final byte[] rgba = new byte[rowBytes * cropHeight];
            for (int row = 0; row < cropHeight; row++) {
                final int sourceRow = crop.top + row;
                final int sourceOffset = sourceRow * rowStride + crop.left * pixelStride;
                final int destinationOffset = (cropHeight - 1 - row) * rowBytes;
                for (int column = 0; column < cropWidth; column++) {
                    final int sourceOffsetPixel = sourceOffset + column * pixelStride;
                    final int destinationOffsetPixel = destinationOffset + column * 4;
                    if (sourceOffsetPixel + 4 > source.limit()) return;
                    source.position(sourceOffsetPixel);
                    source.get(rgba, destinationOffsetPixel, 4);
                }
            }
            nativeProjectionFrame(rgba, cropWidth, cropHeight);
        } catch (RuntimeException ignored) {
            // Projection teardown can invalidate the ImageReader between frames.
        }
    }

    @Override public void onDestroy() {
        if (projection != null) projection.unregisterCallback(callback);
        if (virtualDisplay != null) virtualDisplay.release();
        if (imageReader != null) imageReader.close();
        if (frameThread != null) frameThread.quitSafely();
        projection = null;
        super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }

    private void createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            final NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Aurora desktop reflection", NotificationManager.IMPORTANCE_LOW);
            final NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) manager.createNotificationChannel(channel);
        }
    }
    private static native void nativeProjectionFrame(byte[] rgba, int width, int height);
}
