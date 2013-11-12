// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/system/local_message_pipe_endpoint.h"

#include <string.h>

#include "base/logging.h"
#include "mojo/system/dispatcher.h"
#include "mojo/system/message_in_transit.h"

namespace mojo {
namespace system {

LocalMessagePipeEndpoint::MessageQueueEntry::MessageQueueEntry()
    : message(NULL) {
}

// See comment in header file.
LocalMessagePipeEndpoint::MessageQueueEntry::MessageQueueEntry(
    const MessageQueueEntry& other)
    : message(NULL) {
  DCHECK(!other.message);
  DCHECK(other.dispatchers.empty());
}

LocalMessagePipeEndpoint::MessageQueueEntry::~MessageQueueEntry() {
  if (message)
    message->Destroy();
}

LocalMessagePipeEndpoint::LocalMessagePipeEndpoint()
    : is_open_(true),
      is_peer_open_(true) {
}

LocalMessagePipeEndpoint::~LocalMessagePipeEndpoint() {
  DCHECK(!is_open_);
}

void LocalMessagePipeEndpoint::Close() {
  DCHECK(is_open_);
  is_open_ = false;
  message_queue_.clear();
}

bool LocalMessagePipeEndpoint::OnPeerClose() {
  DCHECK(is_open_);
  DCHECK(is_peer_open_);

  MojoWaitFlags old_satisfied_flags = SatisfiedFlags();
  MojoWaitFlags old_satisfiable_flags = SatisfiableFlags();
  is_peer_open_ = false;
  MojoWaitFlags new_satisfied_flags = SatisfiedFlags();
  MojoWaitFlags new_satisfiable_flags = SatisfiableFlags();

  if (new_satisfied_flags != old_satisfied_flags ||
      new_satisfiable_flags != old_satisfiable_flags) {
    waiter_list_.AwakeWaitersForStateChange(new_satisfied_flags,
                                            new_satisfiable_flags);
  }

  return true;
}

MojoResult LocalMessagePipeEndpoint::CanEnqueueMessage(
    const MessageInTransit* /*message*/,
    const std::vector<Dispatcher*>* dispatchers) {
  // TODO(vtl)
  if (dispatchers) {
    NOTIMPLEMENTED();
    return MOJO_RESULT_UNIMPLEMENTED;
  }
  return MOJO_RESULT_OK;
}

void LocalMessagePipeEndpoint::EnqueueMessage(
    MessageInTransit* message,
    std::vector<scoped_refptr<Dispatcher> >* dispatchers) {
  DCHECK(is_open_);
  DCHECK(is_peer_open_);

  // TODO(vtl)
  DCHECK(!dispatchers || dispatchers->empty());

  bool was_empty = message_queue_.empty();
  message_queue_.push_back(MessageQueueEntry());
  message_queue_.back().message = message;
  if (dispatchers)
    message_queue_.back().dispatchers.swap(*dispatchers);
  if (was_empty) {
    waiter_list_.AwakeWaitersForStateChange(SatisfiedFlags(),
                                            SatisfiableFlags());
  }
}

void LocalMessagePipeEndpoint::CancelAllWaiters() {
  DCHECK(is_open_);
  waiter_list_.CancelAllWaiters();
}

// TODO(vtl): Support receiving handles.
MojoResult LocalMessagePipeEndpoint::ReadMessage(
    void* bytes, uint32_t* num_bytes,
    uint32_t max_num_dispatchers,
    std::vector<scoped_refptr<Dispatcher> >* dispatchers,
    MojoReadMessageFlags flags) {
  DCHECK(is_open_);

  const uint32_t max_bytes = num_bytes ? *num_bytes : 0;

  if (message_queue_.empty()) {
    return is_peer_open_ ? MOJO_RESULT_NOT_FOUND :
                           MOJO_RESULT_FAILED_PRECONDITION;
  }

  // TODO(vtl): If |flags & MOJO_READ_MESSAGE_FLAG_MAY_DISCARD|, we could pop
  // and release the lock immediately.
  bool not_enough_space = false;
  MessageInTransit* const message = message_queue_.front().message;
  if (num_bytes)
    *num_bytes = message->data_size();
  if (message->data_size() <= max_bytes)
    memcpy(bytes, message->data(), message->data_size());
  else
    not_enough_space = true;

  if (!not_enough_space || (flags & MOJO_READ_MESSAGE_FLAG_MAY_DISCARD)) {
    message_queue_.pop_front();

    // Now it's empty, thus no longer readable.
    if (message_queue_.empty()) {
      // It's currently not possible to wait for non-readability, but we should
      // do the state change anyway.
      waiter_list_.AwakeWaitersForStateChange(SatisfiedFlags(),
                                              SatisfiableFlags());
    }
  }

  if (not_enough_space)
    return MOJO_RESULT_RESOURCE_EXHAUSTED;

  return MOJO_RESULT_OK;
}

MojoResult LocalMessagePipeEndpoint::AddWaiter(Waiter* waiter,
                                               MojoWaitFlags flags,
                                               MojoResult wake_result) {
  DCHECK(is_open_);

  if ((flags & SatisfiedFlags()))
    return MOJO_RESULT_ALREADY_EXISTS;
  if (!(flags & SatisfiableFlags()))
    return MOJO_RESULT_FAILED_PRECONDITION;

  waiter_list_.AddWaiter(waiter, flags, wake_result);
  return MOJO_RESULT_OK;
}

void LocalMessagePipeEndpoint::RemoveWaiter(Waiter* waiter) {
  DCHECK(is_open_);
  waiter_list_.RemoveWaiter(waiter);
}

MojoWaitFlags LocalMessagePipeEndpoint::SatisfiedFlags() {
  MojoWaitFlags satisfied_flags = 0;
  if (!message_queue_.empty())
    satisfied_flags |= MOJO_WAIT_FLAG_READABLE;
  if (is_peer_open_)
    satisfied_flags |= MOJO_WAIT_FLAG_WRITABLE;
  return satisfied_flags;
}

MojoWaitFlags LocalMessagePipeEndpoint::SatisfiableFlags() {
  MojoWaitFlags satisfiable_flags = 0;
  if (!message_queue_.empty() || is_peer_open_)
    satisfiable_flags |= MOJO_WAIT_FLAG_READABLE;
  if (is_peer_open_)
    satisfiable_flags |= MOJO_WAIT_FLAG_WRITABLE;
  return satisfiable_flags;
}

}  // namespace system
}  // namespace mojo
