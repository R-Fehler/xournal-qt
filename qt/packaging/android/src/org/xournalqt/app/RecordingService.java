// xournal-qt: keeps a recording going while the app is in the background or the screen is off (qt/docs/audio.md,
// "Android"). Android stops the microphone of an app that is not in front unless a foreground service of type
// "microphone" runs, with a notification the user sees. The native side starts it when a recording starts, tells it
// when the recording pauses or resumes, and stops it when the recording ends (XournalActivity.setRecording). The
// recording itself runs in the app's process as before; the service holds a partial wake lock meanwhile, so the CPU
// keeps taking the microphone's samples with the screen off.
//
// The notification: "Recording" with the document's title and the time (a clock that runs on from the recorded time;
// while paused it shows where it stopped), Pause or Resume, and Stop. The buttons come back here as intents and go on
// to the native side (XournalActivity.sendRecordingCommand -> AudioControl::platformCommand); a tap on the
// notification opens the app.
package org.xournalqt.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

import java.util.Locale;

public class RecordingService extends Service {
    private static final String CHANNEL = "recording";
    private static final int NOTIFICATION = 7301;
    private static final String ACTION_PAUSE = "org.xournalqt.app.recording.PAUSE";
    private static final String ACTION_RESUME = "org.xournalqt.app.recording.RESUME";
    private static final String ACTION_STOP = "org.xournalqt.app.recording.STOP";
    /// The commands the native side takes (AudioControl::PlatformCommand)
    private static final int PAUSE = 1, RESUME = 2, STOP = 3;
    /// The texts in `labels`, translated by the native side (main.cpp)
    private static final int LABEL_RECORDING = 0, LABEL_PAUSED = 1, LABEL_PAUSE = 2, LABEL_RESUME = 3, LABEL_STOP = 4;
    private static final String[] DEFAULT_LABELS = {"Recording", "Recording paused", "Pause", "Resume", "Stop"};
    /// At most this long (a forgotten recording does not keep the CPU awake for days)
    private static final long WAKE_LOCK_MS = 12L * 3600 * 1000;

    // What the native side said last (XournalActivity.setRecording, from the Qt thread)
    private static final Object lock = new Object();
    private static boolean paused = false;
    private static long recordedMs = 0;
    /// When recordedMs was told (System.currentTimeMillis): the clock runs on from there
    private static long toldAt = 0;
    private static String title = "";
    private static String[] labels = DEFAULT_LABELS;
    private static RecordingService running = null;

    private PowerManager.WakeLock wakeLock;
    private final Handler main = new Handler(Looper.getMainLooper());

    /// The native side's news. True when the service runs (its notification shows it now); false: it is to be
    /// started, and reads this when it starts.
    static boolean update(boolean on, boolean isPaused, long ms, String documentTitle, String[] texts) {
        final RecordingService s;
        synchronized (lock) {
            paused = isPaused;
            recordedMs = ms;
            toldAt = System.currentTimeMillis();
            title = documentTitle != null ? documentTitle : "";
            labels = texts != null && texts.length >= DEFAULT_LABELS.length ? texts : DEFAULT_LABELS;
            s = running;
        }
        if (s == null) {
            return false;
        }
        if (on) {
            s.main.post(s::refresh);
        }
        return true;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null && nm.getNotificationChannel(CHANNEL) == null) {
            // Low: in the drawer and the status bar, without a sound
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, DEFAULT_LABELS[LABEL_RECORDING],
                                                                 NotificationManager.IMPORTANCE_LOW));
        }
        PowerManager pm = getSystemService(PowerManager.class);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "xournal-qt:recording");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire(WAKE_LOCK_MS);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_PAUSE.equals(action)) {
            XournalActivity.sendRecordingCommand(PAUSE);
        } else if (ACTION_RESUME.equals(action)) {
            XournalActivity.sendRecordingCommand(RESUME);
        } else if (ACTION_STOP.equals(action)) {
            XournalActivity.sendRecordingCommand(STOP);
        }
        // Every start shows the notification (startForegroundService wants it within seconds)
        Notification n = build();
        if (Build.VERSION.SDK_INT >= 30) {  // (the microphone type exists from Android 11)
            startForeground(NOTIFICATION, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        } else {
            startForeground(NOTIFICATION, n);
        }
        synchronized (lock) {
            running = this;
        }
        return START_NOT_STICKY;  // (killed with the app: the recording is gone with it, nothing to restart)
    }

    /// The notification again, after a pause or a resume
    private void refresh() {
        synchronized (lock) {
            if (running != this) {
                return;
            }
        }
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null) {
            nm.notify(NOTIFICATION, build());
        }
    }

    private Notification build() {
        final boolean isPaused;
        final long ms, at;
        final String text;
        final String[] l;
        synchronized (lock) {
            isPaused = paused;
            ms = recordedMs;
            at = toldAt;
            text = title;
            l = labels;
        }
        // A tap on the notification brings the app back
        Intent open = new Intent(this, XournalActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pending =
            PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        String heading = isPaused ? l[LABEL_PAUSED] + " · " + clock(ms) : l[LABEL_RECORDING];
        Notification.Builder b = new Notification.Builder(this, CHANNEL)
                                     .setContentTitle(heading)
                                     .setContentText(text)
                                     .setSmallIcon(android.R.drawable.ic_btn_speak_now)
                                     .setContentIntent(pending)
                                     .setCategory(Notification.CATEGORY_SERVICE)
                                     .setOngoing(true)
                                     .setOnlyAlertOnce(true);
        if (isPaused) {
            b.setShowWhen(false).setUsesChronometer(false);
        } else {
            // The chronometer counts from `when`: the recorded time when it was told, plus what passed since
            b.setShowWhen(true).setUsesChronometer(true).setWhen(at - ms);
        }
        b.addAction(action(isPaused ? android.R.drawable.ic_media_play : android.R.drawable.ic_media_pause,
                           isPaused ? l[LABEL_RESUME] : l[LABEL_PAUSE], isPaused ? ACTION_RESUME : ACTION_PAUSE,
                           isPaused ? RESUME : PAUSE));
        b.addAction(action(android.R.drawable.ic_menu_close_clear_cancel, l[LABEL_STOP], ACTION_STOP, STOP));
        if (Build.VERSION.SDK_INT >= 31) {  // (shown at once, not after Android's delay of 10 s for services)
            b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return b.build();
    }

    private Notification.Action action(int icon, String label, String what, int code) {
        Intent intent = new Intent(this, RecordingService.class).setAction(what);
        PendingIntent pi = PendingIntent.getService(this, code, intent,
                                                    PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Action.Builder(Icon.createWithResource(this, icon), label, pi).build();
    }

    /// "1:05:09", "4:07"
    private static String clock(long ms) {
        long s = Math.max(0, ms) / 1000;
        long h = s / 3600, m = s / 60 % 60;
        s %= 60;
        return h > 0 ? String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s)
                     : String.format(Locale.ROOT, "%d:%02d", m, s);
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        stopSelf();  // (the app was swiped away)
    }

    @Override
    public void onDestroy() {
        synchronized (lock) {
            if (running == this) {
                running = null;
            }
        }
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        super.onDestroy();
    }
}
