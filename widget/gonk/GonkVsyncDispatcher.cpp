/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GonkVsyncDispatcher.h"
#include "mozilla/layers/VsyncEventParent.h"
#include "mozilla/layers/VsyncEventChild.h"
#include "mozilla/layers/CompositorParent.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/VsyncDispatcherUtils.h"
#include "base/thread.h"
#include "HwcComposer2D.h"
#include "nsThreadUtils.h"
#include "nsRefreshDriver.h"
#include "nsAppShell.h"
#include "GeckoProfiler.h"

//#define DEBUG_VSYNC
#ifdef DEBUG_VSYNC
#define VSYNC_PRINT(...) do { printf_stderr("VDispatcher: " __VA_ARGS__); } while (0)
#else
#define VSYNC_PRINT(...) do { } while (0)
#endif

// Enable vsycn recievers to get vsync event.
// 1. Enable Vsync driven refresh driver. Define this flag only after original
//    timer trigger flow have been removed.
#define ENABLE_REFRESHDRIVER_NOTIFY
// 2. Enable Vsync driven input dispatch. Define this flag only after original
//    NativeEventProcess flow be removed.
#define ENABLE_INPUTDISPATCHER_NOTIFY
// 3. Enable Vsync driven composition. Define this flag only after original
//    SchdedulCompositor callers been removed.
#define ENABLE_COMPOSITOR_NOTIFY

namespace mozilla {

using namespace layers;

const VsyncLogType LOG_TYPE = VSYNC_LOG_ALL;
const int LOG_NUM = 300;

base::Thread* sVsyncDispatchThread = nullptr;
MessageLoop* sVsyncDispatchMessageLoop = nullptr;

static bool
CreateThread()
{
  if (sVsyncDispatchThread) {
    return true;
  }

  sVsyncDispatchThread = new base::Thread("Vsync dispatch thread");

  if (!sVsyncDispatchThread->Start()) {
    delete sVsyncDispatchThread;
    sVsyncDispatchThread = nullptr;
    return false;
  }

  sVsyncDispatchMessageLoop = sVsyncDispatchThread->message_loop();

  return true;
}

// Singleton
// TODO: where to distroy sGonkVsyncDispatcher?
// Caller should not be able to call any publuc member function of this
// singleton after GonkVsyncDispatcher::Shutdown
static StaticRefPtr<GonkVsyncDispatcher> sGonkVsyncDispatcher;

// TODO:
// Generically, introduce a new singleton casue trouble in at_exit process.
// Try to find a holder to host GonkVsyncDispatcher.
/*static*/ GonkVsyncDispatcher*
GonkVsyncDispatcher::GetInstance()
{
  if (sGonkVsyncDispatcher.get() == nullptr) {
    if (XRE_GetProcessType() == GeckoProcessType_Default) {
      StartUp();
    }
    else{
      StartUpOnExistedMessageLoop(MessageLoop::current());
    }

    sGonkVsyncDispatcher = new GonkVsyncDispatcher();
  }

  return sGonkVsyncDispatcher;
}

/*static*/ void
GonkVsyncDispatcher::StartUp()
{
  //only b2g need to create a new thread
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  //MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  if (!sVsyncDispatchMessageLoop) {
    CreateThread();
  }
}

/*static*/ void
GonkVsyncDispatcher::StartUpOnExistedMessageLoop(MessageLoop* aMessageLoop)
{
  if (!sVsyncDispatchMessageLoop) {
    sVsyncDispatchMessageLoop = aMessageLoop;
  }
}

// TODO:
// 1. VSyncDispather::GetInstance and Shutdown need to be thread safe
// 2. Call Shutdown at? gfxPlatform or?
void
GonkVsyncDispatcher::Shutdown()
{
  if (sGonkVsyncDispatcher.get()) {
    delete sGonkVsyncDispatcher;
    sGonkVsyncDispatcher = nullptr;
  }
}

GonkVsyncDispatcher::GonkVsyncDispatcher()
  : mCompositorListMutex("compositor list mutex")
  , mRefreshDriverTimerListMutex("refresh driver list mutex")
  , mVsyncEventParentListMutex("vsync parent list mutex")
  , EnableInputDispatch(false)
  , mInputMonitor("vsync main thread input monitor")
  , mEnableInputDispatchMutex("input dispatcher flag mutex")
  , mVsyncListenerMutex("vsync listener list mutex")
  , mFrameNumber(0)
  , mEnableVsyncNotification(false)
  , mPrintLog(false)
{
}

GonkVsyncDispatcher::~GonkVsyncDispatcher()
{
}

void
GonkVsyncDispatcher::EnableVsyncDispatcher()
{
    // todo: handle listener num to enable/disable vsync(compositor parent cound should thread safe)
//  HwcComposer2D *hwc = HwcComposer2D::GetInstance();
//
//  if (hwc->Initialized()){
//    hwc->EnableVsync(true);
//  }
}

void
GonkVsyncDispatcher::DisableVsyncDispatcher()
{
  // todo: handle listener num to enable/disable vsync(compositor parent cound should thread safe)
//  HwcComposer2D *hwc = HwcComposer2D::GetInstance();
//
//  if (hwc->Initialized()){
//    hwc->EnableVsync(false);
//  }
}

int
GonkVsyncDispatcher::GetRegistedObjectCount() const
{
   int count = 0;

   count += mCompositorList.Length();
   count += mRefreshDriverTimerList.Length();
   count += mVsyncEventParentList.Length();
   count += (EnableInputDispatch ? 1 : 0);

   return count;
}

void
GonkVsyncDispatcher::CheckVsyncNotification()
{
  if (!!GetRegistedObjectCount() !=  mEnableVsyncNotification) {
    mEnableVsyncNotification = !mEnableVsyncNotification;

    if (XRE_GetProcessType() == GeckoProcessType_Default) {
      if (mEnableVsyncNotification) {
        EnableVsyncDispatcher();
      }
      else {
        DisableVsyncDispatcher();
      }
    }
    else{
      if (mEnableVsyncNotification) {
        VsyncEventChild::GetSingleton()->SendEnableVsyncEventNotification();
      }
      else {
        VsyncEventChild::GetSingleton()->SendDisableVsyncEventNotification();
      }
    }
  }
}

MessageLoop*
GonkVsyncDispatcher::GetMessageLoop()
{
  return sVsyncDispatchMessageLoop;
}

void
GonkVsyncDispatcher::RegisterInputDispatcher()
{
  VSYNC_PRINT("RegisterInputDispatcher");

  // This function should be called in chrome process only.
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  //MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  //MutexAutoLock lock(mHasInputDispatcherMutex);
  //MutexAutoLock lock(mVsyncListenerMutex);

  //mHasInputDispatcher = true;

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::SetInputDispatcherInternal,
                             true));
}

void
GonkVsyncDispatcher::UnregisterInputDispatcher()
{
  VSYNC_PRINT("UnregisterInputDispatcher");

  // This function should be called in chrome process only.
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  //MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  //MutexAutoLock lock(mHasInputDispatcherMutex);
  //MutexAutoLock lock(mVsyncListenerMutex);

  //mHasInputDispatcher = false;

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::SetInputDispatcherInternal,
                             false));
}

void
GonkVsyncDispatcher::SetInputDispatcherInternal(bool aReg)
{
  EnableInputDispatch = aReg;
}

void
GonkVsyncDispatcher::RegisterCompositer(layers::CompositorParent* aCompositorParent)
{
  // You should only see this log while screen is updating.
  // While screen is not update, this log should not appear.
  // Otherwise, there is a bug in CompositorParent side.
  VSYNC_PRINT("RegisterCompositor\n");

  // This function should be called in chrome process only.
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  //MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  MutexAutoLock lock(mCompositorListMutex);
  //MutexAutoLock lock(mVsyncListenerMutex);

  ChangeList(&mCompositorList, aCompositorParent, true);
  //ChangeList(&mCompositorList, aCompositorParent, true);
}

void
GonkVsyncDispatcher::UnregisterCompositer(layers::CompositorParent* aCompositorParent)
{
  // You should only see this log while screen is updating.
  // While screen is not update, this log should not appear.
  // Otherwise, there is a bug in CompositorParent side.
  VSYNC_PRINT("UnregisterCompositor\n");

  // This function should be called in chrome process only.
  MOZ_RELEASE_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  //MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  MutexAutoLock lock(mCompositorListMutex);
  //MutexAutoLock lock(mVsyncListenerMutex);

  ChangeList(&mCompositorList, aCompositorParent, false);
  //ChangeList(&mCompositorList, aCompositorParent, mVsyncListenerMutex, false);
}

void
GonkVsyncDispatcher::RegisterRefreshDriverTimer(VsyncRefreshDriverTimer *aRefreshDriverTimer)
{
  VSYNC_PRINT("RegisterRefreshDriver");

  //ChangeList(&mRefreshDriverTimerList, aRefreshDriverTimer, mRefreshDriverTimerListMutex, true);
  //ChangeList(&mRefreshDriverTimerList, aRefreshDriverTimer, mVsyncListenerMutex, true);

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::ChangeList<VsyncRefreshDriverTimer>,
                             &mRefreshDriverTimerList,
                             aRefreshDriverTimer,
                             true));
}

void
GonkVsyncDispatcher::UnregisterRefreshDriverTimer(VsyncRefreshDriverTimer *aRefreshDriverTimer)
{
  VSYNC_PRINT("UnregisterRefreshDriver");

  //ChangeList(&mRefreshDriverTimerList, aRefreshDriverTimer, mRefreshDriverTimerListMutex, false);
  //ChangeList(&mRefreshDriverTimerList, aRefreshDriverTimer, mVsyncListenerMutex, false);

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::ChangeList<VsyncRefreshDriverTimer>,
                             &mRefreshDriverTimerList,
                             aRefreshDriverTimer,
                             false));
}

void
GonkVsyncDispatcher::RegisterVsyncEventParent(VsyncEventParent* aVsyncEventParent)
{
  VSYNC_PRINT("RegisterVsyncEventParent");

  // only be called by ipc system, and it ensures be call at vsync dispatch thread.
  // thus, we don't post a new task to add into the list
  //MutexAutoLock lock(mVsyncEventParentListMutex);
  //MutexAutoLock lock(mVsyncListenerMutex);

  //ChangeList(&mVsyncEventParentList, aVsyncEventParent, mVsyncEventParentListMutex, true);
  //ChangeList(&mVsyncEventParentList, aVsyncEventParent, mVsyncListenerMutex, true);

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::ChangeList<VsyncEventParent>,
                             &mVsyncEventParentList,
                             aVsyncEventParent,
                             true));
}

void
GonkVsyncDispatcher::UnregisterVsyncEventParent(VsyncEventParent* aVsyncEventParent)
{
  VSYNC_PRINT("UnregisterVsyncEventParent");

  // only be called by ipc system, and it ensures be call at vsync dispatch thread.
  // thus, we don't post a new task to add into the list
  //MutexAutoLock lock(mVsyncEventParentListMutex);
  //MutexAutoLock lock(mVsyncListenerMutex);

  //ChangeList(&mVsyncEventParentList, aVsyncEventParent, mVsyncEventParentListMutex, false);
  //ChangeList(&mVsyncEventParentList, aVsyncEventParent, mVsyncListenerMutex, false);

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::ChangeList<VsyncEventParent>,
                             &mVsyncEventParentList,
                             aVsyncEventParent,
                             false));
}

void
GonkVsyncDispatcher::NotifyVsync(int64_t aTimestamp)
{
  ++mFrameNumber;

  GetMessageLoop()->PostTask(FROM_HERE,
                             NewRunnableMethod(this,
                             &GonkVsyncDispatcher::DispatchVsync,
                             VsyncData(aTimestamp, mFrameNumber)));
}

void
GonkVsyncDispatcher::NotifyInputEventProcessed()
{
  {
    MonitorAutoLock inputLock(mInputMonitor);

    inputLock.Notify();
  }

  //TODO:
  //schedule composition here to reduce the compositor latency
}

void
GonkVsyncDispatcher::DispatchVsync(const VsyncData& aVsyncData)
{
  PROFILER_LABEL("GonkVsyncDispatcher", "DispatchVsync",
            js::ProfileEntry::Category::GRAPHICS);

  mPrintLog = !(aVsyncData.frameNumber() % (LOG_NUM+30));

#ifdef ENABLE_INPUTDISPATCHER_NOTIFY
  //1. input
  if (EnableInputDispatch) {
    /*
    nsRefPtr<nsIRunnable> mainThreadInputTask =
        NS_NewRunnableMethodWithArg<const VsyncData&>(this,
                                                      &GonkVsyncDispatcher::InputEventDispatch,
                                                      aVsyncData);
    */
    nsRefPtr<nsPrintableCancelableRunnable> mainThreadInputTask =
        NewNSVsyncRunnableMethod<VSYNC_LOG_ALL, LOG_NUM>(aVsyncData.timeStamp(),
                                                         aVsyncData.frameNumber(),
                                                         "bignose input",
                                                         this,
                                                         &GonkVsyncDispatcher::InputEventDispatch,
                                                         aVsyncData);

    //block vsync event passing until main thread input module updated
    MonitorAutoLock inputLock(mInputMonitor);

    NS_DispatchToMainThread(mainThreadInputTask);

    inputLock.Wait(PR_MillisecondsToInterval(4));

    static VsyncLatencyLogger<LOG_NUM> inputMonitorLogger;
    int32_t diffTime = base::TimeTicks::HighResNow().ToInternalValue() - aVsyncData.timeStamp();
    if(diffTime>5000){
      //printf_stderr("bignose monitor timeout,%d",diffTime);
    }
    inputMonitorLogger.Update(diffTime, aVsyncData.frameNumber(), 0);
    if (mPrintLog) {
      inputMonitorLogger.Print("bignose monitor");
    }
  }
#endif

#ifdef ENABLE_COMPOSITOR_NOTIFY
  //2. compose
  Compose(aVsyncData);
#endif

#ifdef ENABLE_REFRESHDRIVER_NOTIFY
  //3. content process tick
  NotifyVsyncEventChild(aVsyncData);

  //4. current process tick
  /*
  nsRefPtr<nsIRunnable> mainThreadTickTask =
      NS_NewRunnableMethodWithArg<const VsyncData&>(this,
                                                    &GonkVsyncDispatcher::Tick,
                                                    aVsyncData);
  */
  nsRefPtr<nsPrintableCancelableRunnable> mainThreadTickTask =
      NewNSVsyncRunnableMethod<VSYNC_LOG_ALL, LOG_NUM>(aVsyncData.timeStamp(),
                                                       aVsyncData.frameNumber(),
                                                       "bignose tick",
                                                       this,
                                                       &GonkVsyncDispatcher::Tick,
                                                       aVsyncData);
  if (mPrintLog) {
    mainThreadTickTask->Print();
  }
  NS_DispatchToMainThread(mainThreadTickTask);
#endif
}

void
GonkVsyncDispatcher::InputEventDispatch(const VsyncData& aVsyncData)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  //MOZ_ASSERT(NS_IsMainThread());

  DispatchPendingEvent();
}

void
GonkVsyncDispatcher::Compose(const VsyncData& aVsyncData)
{
  MutexAutoLock lock(mCompositorListMutex);

  // CompositorParent::ScheduleComposition is an async call, assume it takes minor
  // period.
  for (CompositorList::size_type i = 0; i < mCompositorList.Length(); i++) {
    layers::CompositorParent *compositor = mCompositorList[i];
    /*
    CompositorParent::CompositorLoop()->PostTask(FROM_HERE,
                                        NewRunnableMethod(compositor,
                                        &CompositorParent::VsyncComposition));
    */

    PrintableCancelableTask* composeTask =
        NewVsyncRunnableMethod<VSYNC_LOG_ALL, LOG_NUM>(aVsyncData.timeStamp(),
                                                       aVsyncData.frameNumber(),
                                                       "bignose compositor",
                                                       compositor,
                                                       &CompositorParent::VsyncComposition);
    if (mPrintLog) {
      composeTask->Print();
    }
    CompositorParent::CompositorLoop()->PostTask(FROM_HERE, composeTask);
  }
}

void
GonkVsyncDispatcher::NotifyVsyncEventChild(const VsyncData& aVsyncData)
{
  // Tick all registered content process.
  for (VsyncEventParentList::size_type i = 0; i < mVsyncEventParentList.Length(); i++) {
    VsyncEventParent* parent = mVsyncEventParentList[i];
    parent->SendNotifyVsyncEvent(aVsyncData);
  }
}

void
GonkVsyncDispatcher::Tick(const VsyncData& aVsyncData)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  //MOZ_ASSERT(NS_IsMainThread());

  // Tick all registered refresh drivers.
  for (RefreshDriverTimerList::size_type i = 0; i < mRefreshDriverTimerList.Length(); i++) {
    VsyncRefreshDriverTimer* timer = mRefreshDriverTimerList[i];
    timer->Tick(aVsyncData.timeStamp(), aVsyncData.frameNumber());
  }
}

} // namespace mozilla
