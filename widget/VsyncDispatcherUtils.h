/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_VsyncDispatcherUtils_h
#define mozilla_VsyncDispatcherUtils_h

#include <algorithm>
#include "base/ref_counted.h"
#include "base/task.h"
#include "nsThreadUtils.h"
#include "mozilla/Mutex.h"
#include "GeckoProfiler.h"

namespace mozilla {

template<uint32_t DataNum>
class VsyncLatencyLogger
{
public:
  VsyncLatencyLogger()
    : mDataMutex("vsync data mutex")
    , mDataArray(DataNum)
  {
    Reset();

    mDataArray.reserve(DataNum);
  }

  ~VsyncLatencyLogger()
  {

  }

  void Update(int32_t aUSLatencyData, uint32_t aFrameNumber, uint32_t aUSExecuteTime)
  {
    MutexAutoLock lock(mDataMutex);

    //if ( mNum >= DataNum) {
    //  return;
    //}

    mDataArray[mNextDataIndex] = VsyncLatencyData(aUSLatencyData, aFrameNumber, aUSExecuteTime);
    mNextDataIndex = (mNextDataIndex + 1) % DataNum;
    mNum = std::min(mNum+1, DataNum);
  }

  void Print(const char* aMsg)
  {
    std::vector<VsyncLatencyData> data;
    uint32_t dataNum;
    uint32_t nextDataIndex;

    {
      MutexAutoLock lock(mDataMutex);
      data = mDataArray;
      dataNum = mNum;
      nextDataIndex = mNextDataIndex;

      Reset();
    }

    PrintStatisticData(aMsg, data, dataNum, nextDataIndex);
    //PrintAllData(aMsg, data, dataNum, nextDataIndex);
  }

private:
  class VsyncLatencyData
  {
  public:
    VsyncLatencyData()
      : mLatencyUS(0)
      , mFrameNumber(0)
      , mExecuteTimeUS(0)
    {
    }

    VsyncLatencyData(uint32_t aLatencyUS, uint32_t aFrameNumber, uint32_t aUSExecuteTime)
    {
      mLatencyUS = aLatencyUS;
      mFrameNumber = aFrameNumber;
      mExecuteTimeUS = aUSExecuteTime;
    }

    VsyncLatencyData(const VsyncLatencyData& aData)
    {
      mLatencyUS = aData.mLatencyUS;
      mFrameNumber = aData.mFrameNumber;
      mExecuteTimeUS = aData.mExecuteTimeUS;
    }

    VsyncLatencyData& operator=(const VsyncLatencyData& rData)
    {
      mLatencyUS = rData.mLatencyUS;
      mFrameNumber = rData.mFrameNumber;
      mExecuteTimeUS = rData.mExecuteTimeUS;

      return *this;
    }

    uint32_t mLatencyUS;
    uint32_t mFrameNumber;
    uint32_t mExecuteTimeUS;
  };

  void Reset()
  {
    mNum = 0;
    mNextDataIndex = 0;
  }

  uint32_t GetStartDataIndex(uint32_t aTotalDataNum, uint32_t aNextDataIndex)
  {
    return (DataNum - aTotalDataNum + aNextDataIndex) % DataNum;
  }

  uint32_t GetMaxIndex(const std::vector<VsyncLatencyData>& aData, uint32_t aDataNum, uint32_t aNextDataIndex)
  {
    uint32_t maxValue = std::numeric_limits<uint32_t>::min();
    uint32_t maxIndex = 0;

    uint32_t index = GetStartDataIndex(aDataNum, aNextDataIndex);
    for (uint32_t i = 0; i < aDataNum; ++i) {
      if (maxValue < aData[index].mLatencyUS) {
        maxValue = aData[index].mLatencyUS;
        maxIndex = index;
      }
      index = (index + 1) % DataNum;
    }

    return maxIndex;
  }

  uint32_t GetMinIndex(const std::vector<VsyncLatencyData>& aData, uint32_t aDataNum, uint32_t aNextDataIndex)
  {
    uint32_t minValue = std::numeric_limits<uint32_t>::max();
    uint32_t minIndex = 0;

    uint32_t index = GetStartDataIndex(aDataNum, aNextDataIndex);
    for (uint32_t i = 0; i < aDataNum; ++i) {
      if (minValue > aData[index].mLatencyUS) {
        minValue = aData[index].mLatencyUS;
        minIndex = index;
      }
      index = (index + 1) % DataNum;
    }

    return minIndex;
  }

  float GetMSAVG(const std::vector<VsyncLatencyData>& aData, uint32_t aDataNum, uint32_t aNextDataIndex)
  {
    uint32_t total = 0;

    uint32_t index = GetStartDataIndex(aDataNum, aNextDataIndex);
    for (uint32_t i = 0; i < aDataNum; ++i) {
      total += aData[index].mLatencyUS;
      index = (index + 1) % DataNum;
    }

    return (float) total / aDataNum * 0.001f;
  }

  float GetMSSTD(const std::vector<VsyncLatencyData>& aData, uint32_t aDataNum, uint32_t aNextDataIndex)
  {
    float variance = 0.0f;
    float avg = GetMSAVG(aData, aDataNum, aNextDataIndex);

    uint32_t index = GetStartDataIndex(aDataNum, aNextDataIndex);
    for (uint32_t i = 0; i < aDataNum; ++i) {
      float delta = aData[index].mLatencyUS * 0.001f - avg;
      variance += delta * delta;
      index = (index + 1) % DataNum;
    }

    return std::sqrt(variance / aDataNum);
  }

  void PrintStatisticData(const char* aMsg, const std::vector<VsyncLatencyData>& aData, uint32_t aDataNum, uint32_t aNextDataIndex)
  {
    if (!aDataNum) {
      return;
    }

    uint32_t maxIndex = GetMaxIndex(aData, aDataNum, aNextDataIndex);
    uint32_t minIndex = GetMinIndex(aData, aDataNum, aNextDataIndex);

    printf_stderr("%-20s, avg:%5.3fms, std:%5.3f, max:(%d, %5.3fms, %5.3fms), min:(%d, %5.3fms, %5.3fms)",
                  aMsg,
                  GetMSAVG(aData, aDataNum, aNextDataIndex),
                  GetMSSTD(aData, aDataNum, aNextDataIndex),
                  aData[maxIndex].mFrameNumber,
                  aData[maxIndex].mLatencyUS * 0.001f,
                  aData[maxIndex].mExecuteTimeUS * 0.001f,
                  aData[minIndex].mFrameNumber,
                  aData[minIndex].mLatencyUS * 0.001f,
                  aData[minIndex].mExecuteTimeUS * 0.001f);
  }

  void PrintAllData(const char* aMsg, const std::vector<VsyncLatencyData>& aData, uint32_t aDataNum, uint32_t aNextDataIndex)
  {
    if (!aDataNum) {
      return;
    }

    uint32_t index = GetStartDataIndex(aDataNum, aNextDataIndex);
    for (uint32_t i = 0; i < aDataNum; ++i) {
      printf_stderr("%-20s, (%d, %5.3fms, %5.3fms)",
                    aMsg,
                    aData[index].mFrameNumber,
                    aData[index].mLatencyUS * 0.001f,
                    aData[index].mExecuteTimeUS * 0.001f);
      index = (index + 1) % DataNum;
    }
  }

  Mutex mDataMutex;

  std::vector<VsyncLatencyData> mDataArray;
  uint32_t mNum;
  uint32_t mNextDataIndex;
};

enum VsyncLogType{
  VSYNC_LOG_NONE,
  VSYNC_LOG_LATENCY,
  VSYNC_LOG_PROFILER_TAG,
  VSYNC_LOG_ALL,
};

template<VsyncLogType LogType>
class VsyncLogTypeClass
{
public:
  enum {
    Value = LogType
  };
};

class PrintableCancelableTask : public CancelableTask
{
public:
  virtual void Print() = 0;
};

class nsPrintableCancelableRunnable : public nsCancelableRunnable
{
public:
  virtual void Print() = 0;
};

template<class T, class Method, class Params, VsyncLogType LogType, int DataNum>
class VsyncRunnableMethod : public PrintableCancelableTask
{
public:
  VsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* aObj, Method aMeth, const Params& aParams)
    : mTimestamp(aTimestamp)
    , mFrameNumber(aFrameNumber)
    , mMsg(aMsg)
    , mObj(aObj)
    , mMethod(aMeth)
    , mParams(aParams)
  {
    mObj->AddRef();
  }

  virtual ~VsyncRunnableMethod()
  {
    ReleaseCallee();
  }

  virtual void Run() MOZ_OVERRIDE
  {
    if (mObj) {
      RunImpl(VsyncLogTypeClass<LogType>());
    }
  }

  virtual void Print() MOZ_OVERRIDE
  {
    mLogger.Print(mMsg);
  }

  virtual void Cancel() MOZ_OVERRIDE
  {
    ReleaseCallee();
  }

private:
  void ReleaseCallee() {
    if (mObj) {
      mObj->Release();
      mObj = NULL;
    }
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_NONE>)
  {
    DispatchToMethod(mObj, mMethod, mParams);
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_LATENCY>)
  {
    uint64_t dispatchTime = base::TimeTicks::HighResNow().ToInternalValue();
    uint32_t latency = dispatchTime- mTimestamp;

    DispatchToMethod(mObj, mMethod, mParams);

    uint32_t executeTime = base::TimeTicks::HighResNow().ToInternalValue()- dispatchTime;

    mLogger.Update(latency, mFrameNumber, executeTime);
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_PROFILER_TAG>)
  {
    PROFILER_LABEL("VsyncRunnableMethod", "Run",
        js::ProfileEntry::Category::GRAPHICS);

    DispatchToMethod(mObj, mMethod, mParams);
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_ALL>)
  {
    PROFILER_LABEL("VsyncRunnableMethod", "Run",
        js::ProfileEntry::Category::GRAPHICS);

    uint64_t dispatchTime = base::TimeTicks::HighResNow().ToInternalValue();
    uint32_t latency = dispatchTime- mTimestamp;

    DispatchToMethod(mObj, mMethod, mParams);

    uint32_t executeTime = base::TimeTicks::HighResNow().ToInternalValue()- dispatchTime;

    if(latency>5000){
      //PROFILER_LABEL("VsyncRunnableMethod", "Run timeout",
      //    js::ProfileEntry::Category::GRAPHICS);
      //printf_stderr("bignose %s timeout,%d",mMsg, latency);
    }

    mLogger.Update(latency, mFrameNumber, executeTime);
  }

  int64_t mTimestamp;
  int32_t mFrameNumber;
  const char* mMsg;

  T* mObj;
  Method mMethod;
  Params mParams;

  static VsyncLatencyLogger<DataNum> mLogger;
};

template<class T, class Method, class Params, VsyncLogType LogType, int DataNum>
VsyncLatencyLogger<DataNum> VsyncRunnableMethod<T, Method, Params, LogType, DataNum>::mLogger;

template<class T, class Method, class Params, VsyncLogType LogType, int DataNum>
class NSVsyncRunnableMethod : public nsPrintableCancelableRunnable
{
public:
  NSVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* aObj, Method aMeth, const Params& aParams)
    : mTimestamp(aTimestamp)
    , mFrameNumber(aFrameNumber)
    , mMsg(aMsg)
    , mObj(aObj)
    , mMethod(aMeth)
    , mParams(aParams)
  {
    if (aObj) {
      mObj->AddRef();
    }
  }

  ~NSVsyncRunnableMethod()
  {
    ReleaseCallee();
  }

  NS_IMETHOD Run() MOZ_OVERRIDE
  {
    if (mObj) {
      RunImpl(VsyncLogTypeClass<LogType>());
    }

    return NS_OK;
  }

  virtual void Print() MOZ_OVERRIDE
  {
    mLogger.Print(mMsg);
  }

  NS_IMETHOD Cancel() MOZ_OVERRIDE
  {
    ReleaseCallee();

    return NS_OK;
  }

private:
  void ReleaseCallee() {
    if (mObj) {
      mObj->Release();
      mObj = nullptr;
    }
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_NONE>)
  {
    DispatchToMethod(mObj, mMethod, mParams);
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_LATENCY>)
  {
    uint64_t dispatchTime = base::TimeTicks::HighResNow().ToInternalValue();
    uint32_t latency = dispatchTime- mTimestamp;

    DispatchToMethod(mObj, mMethod, mParams);

    uint32_t executeTime = base::TimeTicks::HighResNow().ToInternalValue()- dispatchTime;

    mLogger.Update(latency, mFrameNumber, executeTime);
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_PROFILER_TAG>)
  {
    PROFILER_LABEL("NSVsyncRunnableMethod", "Run",
        js::ProfileEntry::Category::GRAPHICS);

    DispatchToMethod(mObj, mMethod, mParams);
  }

  void RunImpl(VsyncLogTypeClass<VSYNC_LOG_ALL>)
  {
    PROFILER_LABEL("NSVsyncRunnableMethod", "Run",
        js::ProfileEntry::Category::GRAPHICS);

    uint64_t dispatchTime = base::TimeTicks::HighResNow().ToInternalValue();
    uint32_t latency = dispatchTime- mTimestamp;

    DispatchToMethod(mObj, mMethod, mParams);

    uint32_t executeTime = base::TimeTicks::HighResNow().ToInternalValue()- dispatchTime;

    if(latency>5000){
      //printf_stderr("bignose %s timeout,%d",mMsg, latency);
    }

    mLogger.Update(latency, mFrameNumber, executeTime);
  }

  int64_t mTimestamp;
  int32_t mFrameNumber;
  const char* mMsg;

  T* mObj;
  Method mMethod;
  Params mParams;

  static VsyncLatencyLogger<DataNum> mLogger;
};

template<class T, class Method, class Params, VsyncLogType LogType, int DataNum>
VsyncLatencyLogger<DataNum> NSVsyncRunnableMethod<T, Method, Params, LogType, DataNum>::mLogger;

template<VsyncLogType LogType, int DataNum, class T, class Method>
inline PrintableCancelableTask*
NewVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method)
{
  return new VsyncRunnableMethod<T, Method, Tuple0, LogType, DataNum>(aTimestamp,
                                                                      aFrameNumber,
                                                                      aMsg,
                                                                      object,
                                                                      method,
                                                                      MakeTuple());
}

template<VsyncLogType LogType, int DataNum, class T, class Method, class A>
inline PrintableCancelableTask*
NewVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method, const A& a)
{
  return new VsyncRunnableMethod<T, Method, Tuple1<A>, LogType, DataNum>(aTimestamp,
                                                                         aFrameNumber,
                                                                         aMsg,
                                                                         object,
                                                                         method,
                                                                         MakeTuple(a));
}

template<VsyncLogType LogType, int DataNum, class T, class Method, class A, class B>
inline PrintableCancelableTask*
NewVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method, const A& a, const B& b)
{
  return new VsyncRunnableMethod<T, Method, Tuple2<A, B>, LogType, DataNum>(aTimestamp,
                                                                            aFrameNumber,
                                                                            aMsg,
                                                                            object,
                                                                            method,
                                                                            MakeTuple(a, b));
}

template<VsyncLogType LogType, int DataNum, class T, class Method, class A, class B, class C>
inline PrintableCancelableTask*
NewVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method, const A& a, const B& b, const C& c)
{
  return new VsyncRunnableMethod<T, Method, Tuple3<A, B, C>, LogType, DataNum>(aTimestamp,
                                                                               aFrameNumber,
                                                                               aMsg,
                                                                               object,
                                                                               method,
                                                                               MakeTuple(a, b, c));
}

template<VsyncLogType LogType, int DataNum, class T, class Method, class A, class B, class C, class D>
inline PrintableCancelableTask*
NewVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method, const A& a, const B& b, const C& c, const D& d)
{
  return new VsyncRunnableMethod<T, Method, Tuple4<A, B, C, D>, LogType, DataNum>(aTimestamp,
                                                                                  aFrameNumber,
                                                                                  aMsg,
                                                                                  object,
                                                                                  method,
                                                                                  MakeTuple(a, b, c, d));
}

template<VsyncLogType LogType, int DataNum, class T, class Method>
inline nsPrintableCancelableRunnable*
NewNSVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method)
{
  return new NSVsyncRunnableMethod<T, Method, Tuple0, LogType, DataNum>(aTimestamp,
                                                                        aFrameNumber,
                                                                        aMsg,
                                                                        object,
                                                                        method,
                                                                        MakeTuple());
}

template<VsyncLogType LogType, int DataNum, class T, class Method, class A>
inline nsPrintableCancelableRunnable*
NewNSVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method, const A& a)
{
  return new NSVsyncRunnableMethod<T, Method, Tuple1<A>, LogType, DataNum>(aTimestamp,
                                                                           aFrameNumber,
                                                                           aMsg,
                                                                           object,
                                                                           method,
                                                                           MakeTuple(a));
}

template<VsyncLogType LogType, int DataNum, class T, class Method, class A, class B>
inline nsPrintableCancelableRunnable*
NewNSVsyncRunnableMethod(int64_t aTimestamp, int32_t aFrameNumber, const char* aMsg, T* object, Method method, const A& a, const B& b)
{
  return new NSVsyncRunnableMethod<T, Method, Tuple2<A, B>, LogType, DataNum>(aTimestamp,
                                                                              aFrameNumber,
                                                                              aMsg,
                                                                              object,
                                                                              method,
                                                                              MakeTuple(a, b));
}

} // namespace mozilla

#endif // mozilla_VsyncDispatcherUtils_h
