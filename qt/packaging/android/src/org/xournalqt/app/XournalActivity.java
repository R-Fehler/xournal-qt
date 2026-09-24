// xournal-qt: the app's activity. Qt's QtActivity plus what the native code cannot get by itself:
// - the files other apps hand over ("Open with": ACTION_VIEW; the share sheet: ACTION_SEND, ACTION_SEND_MULTIPLE),
//   at start and while the app runs. They are content:// URIs; the native side copies them into the library
//   (AndroidContent.cpp, AppController::receiveFiles).
// - whether the device has a stylus (the default of "draw with the finger").
// - "All files access" (MANAGE_EXTERNAL_STORAGE; the storage permission before Android 11), asked for when the user
//   opens a folder of the shared storage as a library: whether the app has it, and the system's page to allow it.
// See qt/docs/android.md.
package org.xournalqt.app;

import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Parcelable;
import android.provider.Settings;
import android.view.InputDevice;

import java.lang.ref.WeakReference;
import java.util.ArrayList;

import org.qtproject.qt.android.bindings.QtActivity;

public class XournalActivity extends QtActivity {
    /// What a share of text alone (no file) is reported as: the native side says that only files can be received.
    public static final String SHARED_TEXT = "xournal-qt:shared-text";

    private static final ArrayList<String> pending = new ArrayList<>();
    /// The native side has asked once (takeIncomingFiles), so it is running and wants to be told of new files.
    private static boolean nativeListening = false;

    /// Registered by the native side (AndroidContent.cpp): files are waiting in takeIncomingFiles().
    private static native void incomingFilesArrived();
    /// The running activity (for the static methods the native side calls).
    private static WeakReference<XournalActivity> current = new WeakReference<>(null);
    private static final int STORAGE_REQUEST = 4711;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        current = new WeakReference<>(this);
        if (savedInstanceState == null) {  // (recreated: that intent was handled before)
            receive(getIntent());
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        receive(intent);
    }

    private static void receive(Intent intent) {
        ArrayList<String> uris = urisOf(intent);
        if (uris.isEmpty()) {
            return;
        }
        boolean notify;
        synchronized (pending) {
            pending.addAll(uris);
            notify = nativeListening;
        }
        if (notify) {
            incomingFilesArrived();
        }
    }

    @SuppressWarnings("deprecation")  // the typed getParcelable*Extra need API 33; min SDK is 28
    private static ArrayList<String> urisOf(Intent intent) {
        ArrayList<String> uris = new ArrayList<>();
        if (intent == null || intent.getAction() == null) {
            return uris;
        }
        String action = intent.getAction();
        if (Intent.ACTION_VIEW.equals(action)) {
            if (intent.getData() != null) {
                uris.add(intent.getData().toString());
            }
        } else if (Intent.ACTION_SEND.equals(action)) {
            Parcelable stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (stream instanceof Uri) {
                uris.add(stream.toString());
            }
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            ArrayList<Parcelable> streams = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (streams != null) {
                for (Parcelable p : streams) {
                    if (p instanceof Uri) {
                        uris.add(p.toString());
                    }
                }
            }
        } else {
            return uris;
        }
        // Some apps put the files only into the clip data
        ClipData clip = intent.getClipData();
        if (uris.isEmpty() && clip != null) {
            for (int i = 0; i < clip.getItemCount(); ++i) {
                Uri u = clip.getItemAt(i).getUri();
                if (u != null) {
                    uris.add(u.toString());
                }
            }
        }
        if (uris.isEmpty() && intent.getStringExtra(Intent.EXTRA_TEXT) != null) {
            uris.add(SHARED_TEXT);
        }
        return uris;
    }

    /// The files handed over since the last call (content:// or file:// URIs). From then on the native side is told
    /// of new ones through incomingFilesArrived().
    public static String[] takeIncomingFiles() {
        synchronized (pending) {
            nativeListening = true;
            String[] files = pending.toArray(new String[0]);
            pending.clear();
            return files;
        }
    }

    /// The app may read and write the whole shared storage (Android 11+: "All files access"; before: the storage
    /// permission), so a folder there can be a library.
    public static boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        XournalActivity a = current.get();
        return a != null
            && a.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    /// Show the system's page to allow "All files access" for this app (before Android 11: the permission dialog).
    /// The app goes to the background meanwhile; the native side looks again when it is back (applicationStateChanged).
    public static boolean requestAllFilesAccess() {
        final XournalActivity a = current.get();
        if (a == null) {
            return false;
        }
        a.runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT >= 30) {
                try {
                    a.startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                               Uri.parse("package:" + a.getPackageName())));
                } catch (ActivityNotFoundException e) {  // (some devices only have the list of all apps)
                    a.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                }
            } else {
                a.requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE,
                                                   Manifest.permission.WRITE_EXTERNAL_STORAGE},
                                     STORAGE_REQUEST);
            }
        });
        return true;
    }

    /// A pen digitizer is attached (a built-in S Pen layer or a Bluetooth stylus). Touch screens of phones without a
    /// pen report only TOUCHSCREEN.
    public static boolean hasStylus() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && (device.getSources() & InputDevice.SOURCE_STYLUS) == InputDevice.SOURCE_STYLUS) {
                return true;
            }
        }
        return false;
    }
}
