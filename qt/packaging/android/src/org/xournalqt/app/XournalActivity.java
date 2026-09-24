// xournal-qt: the app's activity. Qt's QtActivity plus what the native code cannot get by itself:
// - the files other apps hand over ("Open with": ACTION_VIEW; the share sheet: ACTION_SEND, ACTION_SEND_MULTIPLE),
//   at start and while the app runs. They are content:// URIs; the native side copies them into the library
//   (AndroidContent.cpp, AppController::receiveFiles).
// - whether the device has a stylus (the default of "draw with the finger").
// See qt/docs/android.md.
package org.xournalqt.app;

import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Parcelable;
import android.view.InputDevice;

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

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
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
