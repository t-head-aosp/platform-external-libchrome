// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_RADIO_UTILS_H_
#define BASE_ANDROID_RADIO_UTILS_H_

#include "base/android/jni_android.h"
#include "base/optional.h"

namespace base {
namespace android {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. Keep in sync with RadioSignalLevel
// in //tools/metrics/histograms/enums.xml.
enum class RadioSignalLevel {
  kNoneOrUnknown = 0,
  kPoor = 1,
  kModerate = 2,
  kGood = 3,
  kGreat = 4,
  kMaxValue = kGreat,
};

class BASE_EXPORT RadioUtils {
 public:
  static bool IsSupported();
  static bool IsWifiConnected();
  static Optional<RadioSignalLevel> GetCellSignalLevel();
};

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_RADIO_UTILS_H_
