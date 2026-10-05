// xournal-qt: keeps a recording going while the app is in the background (qt/docs/audio.md, "Android"). Android stops
// the microphone of an app that is not in front unless a foreground service of type "microphone" runs, with a
// notification the user sees. The native side starts it when a recording starts and stops it when the recording ends
// (XournalActivity.setRecording). It does nothing itself: the recording runs in the app's process as before.
package org.xournalqt.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

public class RecordingService extends Service {
    private static final String CHANNEL = "recording";
    private static final int NOTIFICATION = 7301;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26 && nm != null && nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(
                    new NotificationChannel(CHANNEL, "Recording", NotificationManager.IMPORTANCE_LOW));
        }
        // A tap on the notification brings the app back
        Intent open = new Intent(this, XournalActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pending = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL)
                                                             : new Notification.Builder(this);
        Notification n = b.setContentTitle("Recording audio")
                .setContentText("Xournal Qt is recording. Tap to go back to the document.")
                .setSmallIcon(android.R.drawable.ic_btn_speak_now)
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= 30) {  // (the microphone type exists from Android 11)
            startForeground(NOTIFICATION, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        } else {
            startForeground(NOTIFICATION, n);
        }
        return START_NOT_STICKY;  // (killed with the app: the recording is gone with it, nothing to restart)
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        stopSelf();  // (the app was swiped away)
    }
}
