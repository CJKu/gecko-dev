/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_GonkVsyncDispatcher_h
#define mozilla_GonkVsyncDispatcher_h

#include "mozilla/VsyncDispatcher.h"
#include "mozilla/layers/PVsyncEvent.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Monitor.h"
#include "nsTArray.h"

#include "base/ref_counted.h"
#include "base/message_loop.h"

namespace base {
  class Thread;
}

namespace mozilla {

class VsyncRefreshDriverTimer;

namespace layers {
class CompositorParent;
class VsyncEventParent;
}

// TODO:
// Describe the role of this object in vsync routing.
class GonkVsyncDispatcher : public VsyncDispatcher,
                            public base::RefCountedThreadSafe<GonkVsyncDispatcher>
{
  friend class base::RefCountedThreadSafe<GonkVsyncDispatcher>;

public:
  class GonkVsyncDispatcherInputProcessingHelper
  {
  public:
    GonkVsyncDispatcherInputProcessingHelper()
      : mNeedNotify(true)
    {

    }

    ~GonkVsyncDispatcherInputProcessingHelper()
    {
      Notify();
    }

    void Notify()
    {
      if (mNeedNotify) {
        mNeedNotify = false;
        GonkVsyncDispatcher::GetInstance()->NotifyInputEventProcessed();
      }
    }

  private:
    bool mNeedNotify;
  };

  // Start up VsyncDispatcher on internal message loop
  static void StartUp();
  // Start up VsyncDispatcher on internal message loop
  static void StartUpOnExistedMessageLoop(MessageLoop* aMessageLoop);

  static GonkVsyncDispatcher* GetInstance();

  // TODO
  // Find a correct place call Shutdown.
  static void Shutdown();

  virtual void EnableVsyncDispatcher() MOZ_OVERRIDE;
  virtual void DisableVsyncDispatcher() MOZ_OVERRIDE;

  // Get vsync dispatcher thread's message loop
  MessageLoop* GetMessageLoop();

  // notify all registered vsync observer
  void NotifyVsync(int64_t aTimestamp);

  // dispatch vsync event
  void DispatchVsync(const layers::VsyncData& aVsyncData);

  // tell dispatcher to start other remain vsync event passing
  void NotifyInputEventProcessed();

  // Register input dispatcher.
  void RegisterInputDispatcher();
  void UnregisterInputDispatcher();

  // Register compositor.
  void RegisterCompositer(layers::CompositorParent *aCompositorParent);
  void UnregisterCompositer(layers::CompositorParent *aCompositorParent);

  // Register refresh driver timer.
  void RegisterRefreshDriverTimer(VsyncRefreshDriverTimer *aRefreshDriverTimer);
  void UnregisterRefreshDriverTimer(VsyncRefreshDriverTimer *aRefreshDriverTimer);

  // Register content process ipc parent
  void RegisterVsyncEventParent(layers::VsyncEventParent* aVsyncEventParent);
  void UnregisterVsyncEventParent(layers::VsyncEventParent* aVsyncEventParent);

private:
  // Singleton object. Hide constructor and destructor.
  GonkVsyncDispatcher();
  ~GonkVsyncDispatcher();

  // Register Input dispather on the specific thread.
  void SetInputDispatcherInternal(bool aReg);

  // Tell the input dispatcher to handle input event.
  void InputEventDispatch(const layers::VsyncData& aVsyncData);

  // Tell compositors to do composition.
  void Compose(const layers::VsyncData& aVsyncData);

  // Sent vsync event to VsyncEventChild
  void NotifyVsyncEventChild(const layers::VsyncData& aVsyncData);

  // Tick refresh driver.
  void Tick(const layers::VsyncData& aVsyncData);

  // Return total registered object number.
  int GetRegistedObjectCount() const;

  // Check the observer number to enable/disable vsync notification
  void CheckVsyncNotification();

  template <typename Type>
  void ChangeList(nsTArray<Type*>* aList, Type* aItem, bool aAdd)
  {
    typedef nsTArray<Type*> ArrayType;

    if (aAdd) {
      //MOZ_RELEASE_ASSERT(!aList->Contains(aItem));
      //MOZ_ASSERT(!aList->Contains(aItem));

      if (!aList->Contains(aItem)) {
        aList->AppendElement(aItem);
      }
    }
    else {
      typename ArrayType::index_type index = aList->IndexOf(aItem);
      //MOZ_RELEASE_ASSERT(index != ArrayType::NoIndex);
      //MOZ_ASSERT(index != ArrayType::NoIndex);

      if (index != ArrayType::NoIndex) {
        aList->RemoveElementAt(index);
      }
    }

    CheckVsyncNotification();
    //GetMessageLoop()->PostTask(FROM_HERE,
    //                           NewRunnableMethod(this,
    //                           &GonkVsyncDispatcher::CheckVsyncNotification));
  }

private:
  // Registered compositors
  typedef nsTArray<layers::CompositorParent*> CompositorList;
  CompositorList mCompositorList;
  Mutex mCompositorListMutex;

  // Registered refresh drivers.
  typedef nsTArray<VsyncRefreshDriverTimer*> RefreshDriverTimerList;
  RefreshDriverTimerList mRefreshDriverTimerList;
  Mutex mRefreshDriverTimerListMutex;

  // Registered vsync ipc parent
  typedef nsTArray<layers::VsyncEventParent*> VsyncEventParentList;
  VsyncEventParentList mVsyncEventParentList;
  Mutex mVsyncEventParentListMutex;

  // Sent vsync event to input dispatcher.
  bool EnableInputDispatch;
  Mutex mEnableInputDispatchMutex;
  // Monotir for b2g main thread input event processing
  Monitor mInputMonitor;

  Mutex mVsyncListenerMutex;

  int32_t mFrameNumber;

  bool mEnableVsyncNotification;

  bool mPrintLog;
};

} // namespace mozilla

#endif // mozilla_GonkVsyncDispatcher_h
