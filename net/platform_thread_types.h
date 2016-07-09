/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_BASE_PLATFORM_THREAD_TYPES_H_
#define WEBRTC_BASE_PLATFORM_THREAD_TYPES_H_

#include <pthread.h>
#include <unistd.h>

namespace net {
typedef pid_t PlatformThreadId;
typedef pthread_t PlatformThreadRef;
}  // namespace rtc

#endif  // WEBRTC_BASE_PLATFORM_THREAD_TYPES_H_
