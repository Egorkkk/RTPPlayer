/*
 * Copyright (C) 2015, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 */

package org.freedesktop.gstreamer.androidmedia;

import android.graphics.SurfaceTexture;
import android.graphics.SurfaceTexture.OnFrameAvailableListener;

public class GstAmcOnFrameAvailableListener implements OnFrameAvailableListener {
    private long context = 0;

    @Override
    public synchronized void onFrameAvailable(SurfaceTexture surfaceTexture) {
        native_onFrameAvailable(context, surfaceTexture);
    }

    public synchronized long getContext() {
        return context;
    }

    public synchronized void setContext(long c) {
        context = c;
    }

    private native void native_onFrameAvailable(long context, SurfaceTexture surfaceTexture);
}
