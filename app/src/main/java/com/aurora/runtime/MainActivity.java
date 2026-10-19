package com.aurora.runtime;

import android.app.Activity;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;
import android.content.Intent;
import android.os.Build;
import android.media.projection.MediaProjectionManager;

public final class MainActivity extends Activity {
    private AuroraSurfaceView surfaceView;
    private boolean overlayLauncher;
    private static final int REQUEST_PROJECTION = 4071;
    private boolean pendingOverlayCapture;
    private Intent pendingOverlayIntent;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (getIntent().getBooleanExtra("overlay", false)) {
            overlayLauncher = true;
            pendingOverlayCapture = getIntent().getBooleanExtra("capture", false);
            pendingOverlayIntent = getIntent();
            if (pendingOverlayCapture) {
                requestProjectionPermission();
            } else {
                startOverlayService(pendingOverlayIntent);
                finish();
            }
            return;
        }
        final String backend = getIntent().getStringExtra("backend");
        final int backendKind = "vulkan".equalsIgnoreCase(backend) ? 1 : 0;
        surfaceView = new AuroraSurfaceView(this, false, backendKind);
        if ("glass".equalsIgnoreCase(getIntent().getStringExtra("showcase"))) {
            surfaceView.setInitialShowcase(0);
        }
        setContentView(surfaceView);
    }

    private void requestProjectionPermission() {
        final MediaProjectionManager manager =
                (MediaProjectionManager) getSystemService(MEDIA_PROJECTION_SERVICE);
        if (manager == null) {
            startOverlayService(pendingOverlayIntent);
            finish();
            return;
        }
        startActivityForResult(manager.createScreenCaptureIntent(), REQUEST_PROJECTION);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PROJECTION) return;
        if (resultCode == RESULT_OK && data != null) {
            final Intent projection = new Intent(this, AuroraProjectionService.class)
                    .putExtra(AuroraProjectionService.EXTRA_RESULT_CODE, resultCode)
                    .putExtra(AuroraProjectionService.EXTRA_RESULT_DATA, data);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(projection);
            else startService(projection);
        }
        startOverlayService(pendingOverlayIntent);
        finish();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (intent.getBooleanExtra("overlay", false)) {
            startOverlayService(intent);
            finish();
        }
    }

    private void startOverlayService(Intent source) {
        final Intent overlay = new Intent(this, AuroraOverlayService.class);
        if (source != null && "glass".equalsIgnoreCase(source.getStringExtra("showcase"))) {
            overlay.putExtra("showcase", "glass");
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(overlay);
        } else {
            startService(overlay);
        }
    }

    @Override
    protected void onPause() {
        if (surfaceView != null) {
            surfaceView.pauseRenderer();
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (surfaceView != null) {
            surfaceView.resumeRenderer();
        }
    }

    @Override
    protected void onDestroy() {
        if (!overlayLauncher) {
            stopService(new Intent(this, AuroraOverlayService.class));
        }
        super.onDestroy();
    }
}
