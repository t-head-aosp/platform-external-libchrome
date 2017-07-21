// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus/mock_object_proxy.h"

namespace dbus {

MockObjectProxy::MockObjectProxy(Bus* bus,
                                 const std::string& service_name,
                                 const ObjectPath& object_path)
    : ObjectProxy(bus, service_name, object_path, DEFAULT_OPTIONS) {
}

MockObjectProxy::~MockObjectProxy() {
}

void MockObjectProxy::CallMethod(MethodCall* method_call,
                                 int timeout_ms,
                                 ResponseCallback callback) {
  DoCallMethod(method_call, timeout_ms, &callback);
}

void MockObjectProxy::CallMethodWithErrorCallback(
    MethodCall* method_call,
    int timeout_ms,
    ResponseCallback callback,
    ErrorCallback error_callback) {
  DoCallMethodWithErrorCallback(method_call, timeout_ms, &callback,
                                &error_callback);
}

}  // namespace dbus
