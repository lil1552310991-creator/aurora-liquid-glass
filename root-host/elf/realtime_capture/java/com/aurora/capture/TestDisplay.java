package com.aurora.capture;

import android.graphics.PixelFormat;
import android.hardware.HardwareBuffer;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.view.Surface;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/** A separate diagnostic source display. Never called by the production capture helper. */
public final class TestDisplay {
    public static void main(String[] args) {
        Object manager = null;
        Object callback = null;
        Method release = null;
        ImageReader reader = null;
        HandlerThread images = null;
        int displayId = -1;
        AtomicBoolean running = new AtomicBoolean(true);
        AtomicLong events = new AtomicLong();
        AtomicLong lastTimestamp = new AtomicLong();
        long started = System.nanoTime();
        try {
            int seconds = args.length > 0 ? Integer.parseInt(args[0]) : 120;
            int width = args.length > 1 ? Integer.parseInt(args[1]) : 1080;
            int height = args.length > 2 ? Integer.parseInt(args[2]) : 2376;
            float hz = args.length > 3 ? Float.parseFloat(args[3]) : 120.0f;
            boolean stdinEnds = seconds <= 0 || (args.length > 4 && args[4].equals("--stdin"));
            if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || hz < 1 || hz > 240)
                throw new IllegalArgumentException("seconds width height Hz [--stdin]; dimensions/rate out of range");
            images = new HandlerThread("AuroraTestDisplayImages");
            images.start();
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 4,
                    HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE);
            reader.setOnImageAvailableListener(source -> {
                if (!running.get()) return;
                try (Image image = source.acquireLatestImage()) {
                    if (image != null) {
                        long count = events.incrementAndGet();
                        lastTimestamp.set(image.getTimestamp());
                        if (count == 1 || count % 120 == 0)
                            System.err.println("TEST_DISPLAY_FRAME count=" + count + " timestampNs=" + image.getTimestamp());
                    }
                } catch (Throwable failure) {
                    failure.printStackTrace(System.err);
                    running.set(false);
                }
            }, new Handler(images.getLooper()));

            Class<?> builderClass = Class.forName("android.hardware.display.VirtualDisplayConfig$Builder");
            Object builder = builderClass.getConstructor(String.class, int.class, int.class, int.class)
                    .newInstance("AuroraPerformanceFixture", width, height, 640);
            int flags = 1 | 8 | 1024; // PUBLIC | OWN_CONTENT_ONLY | TRUSTED
            // Keep an activity launched here from stealing the user's physical-screen focus.
            Class<?> displayManagerClass = Class.forName("android.hardware.display.DisplayManager");
            try {
                flags |= displayManagerClass.getField("VIRTUAL_DISPLAY_FLAG_OWN_FOCUS").getInt(null);
                flags |= displayManagerClass.getField("VIRTUAL_DISPLAY_FLAG_STEAL_TOP_FOCUS_DISABLED").getInt(null);
            } catch (ReflectiveOperationException olderPlatform) { /* base source-display flags suffice */ }
            builderClass.getMethod("setFlags", int.class).invoke(builder, flags);
            builderClass.getMethod("setRequestedRefreshRate", float.class).invoke(builder, hz);
            builderClass.getMethod("setSurface", Surface.class).invoke(builder, reader.getSurface());
            Object config = builderClass.getMethod("build").invoke(builder);
            Class<?> callbackClass = Class.forName("android.hardware.display.DisplayManagerGlobal$VirtualDisplayCallback");
            Constructor<?> callbackConstructor = callbackClass.getDeclaredConstructor(VirtualDisplay.Callback.class, Executor.class);
            callbackConstructor.setAccessible(true);
            callback = callbackConstructor.newInstance(null, null);
            Class<?> serviceManager = Class.forName("android.os.ServiceManager");
            IBinder displayService = (IBinder) serviceManager.getMethod("getService", String.class).invoke(null, "display");
            manager = Class.forName("android.hardware.display.IDisplayManager$Stub")
                    .getMethod("asInterface", IBinder.class).invoke(null, displayService);
            Class<?> managerInterface = Class.forName("android.hardware.display.IDisplayManager");
            Class<?> callbackInterface = Class.forName("android.hardware.display.IVirtualDisplayCallback");
            release = managerInterface.getMethod("releaseVirtualDisplay", callbackInterface);
            displayId = (Integer) managerInterface.getMethod("createVirtualDisplay",
                    Class.forName("android.hardware.display.VirtualDisplayConfig"), callbackInterface,
                    Class.forName("android.media.projection.IMediaProjection"), String.class)
                    .invoke(manager, config, callback, null, null);
            if (displayId < 0) throw new IllegalStateException("test display creation failed: " + displayId);
            Class<?> globalClass = Class.forName("android.hardware.display.DisplayManagerGlobal");
            Object global = globalClass.getMethod("getInstance").invoke(null);
            Object info = globalClass.getMethod("getDisplayInfo", int.class).invoke(global, displayId);
            if (info == null) throw new IllegalStateException("test display info unavailable");
            int stack = info.getClass().getField("layerStack").getInt(info);
            System.out.println("TEST_DISPLAY_READY id=" + displayId + " stack=" + stack
                    + " width=" + width + " height=" + height + " requestedHz=" + hz
                    + " flags=" + flags + " seconds=" + seconds);
            System.out.flush();
            if (stdinEnds) {
                Thread commands = new Thread(() -> {
                    try { while (running.get() && System.in.read() >= 0) { /* EOF ends diagnostics. */ } }
                    catch (Exception failure) { System.err.println("TEST_DISPLAY_STDIN " + failure); }
                    running.set(false);
                }, "AuroraTestDisplayStdin");
                commands.setDaemon(true);
                commands.start();
            }
            started = System.nanoTime();
            final long deadline = seconds > 0 ? started + seconds * 1000000000L : Long.MAX_VALUE;
            while (running.get() && System.nanoTime() < deadline) Thread.sleep(100);
        } catch (Throwable failure) {
            failure.printStackTrace(System.err);
        } finally {
            running.set(false);
            if (displayId >= 0 && manager != null && release != null) {
                try { release.invoke(manager, callback); }
                catch (Throwable failure) { System.err.println("TEST_DISPLAY_RELEASE " + failure); }
            }
            if (reader != null) reader.close();
            if (images != null) images.quitSafely();
            System.out.println("TEST_DISPLAY_STOP id=" + displayId + " events=" + events.get()
                    + " timestampNs=" + lastTimestamp.get()
                    + " seconds=" + (System.nanoTime() - started) / 1000000000.0);
            System.out.flush();
        }
    }
}
