//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once

namespace Memory
{
class Recycler;
class RecyclerNonLeafAllocator;

// Dummy tag classes to mark yes/no write barrier policy
//
struct _write_barrier_policy {};
struct _no_write_barrier_policy {};

// Type write barrier policy
//
// By default following potentially contains GC pointers and use write barrier policy:
//      pointer, WriteBarrierPtr, _write_barrier_policy
//
template <class T>
struct TypeWriteBarrierPolicy { typedef _no_write_barrier_policy Policy; };
template <class T>
struct TypeWriteBarrierPolicy<T*> { typedef _write_barrier_policy Policy; };
template <class T>
struct TypeWriteBarrierPolicy<WriteBarrierPtr<T>> { typedef _write_barrier_policy Policy; };
template <>
struct TypeWriteBarrierPolicy<_write_barrier_policy> { typedef _write_barrier_policy Policy; };

// AllocatorType write barrier policy
//
// Recycler allocator type => _write_barrier_policy
// Note that Recycler allocator type consists of multiple allocators:
//      Recycler, RecyclerNonLeafAllocator, RecyclerLeafAllocator
//
template <class AllocatorType>
struct _AllocatorTypeWriteBarrierPolicy { typedef _no_write_barrier_policy Policy; };
template <>
struct _AllocatorTypeWriteBarrierPolicy<Recycler> { typedef _write_barrier_policy Policy; };

template <class Policy1, class Policy2>
struct _AndWriteBarrierPolicy { typedef _no_write_barrier_policy Policy; };
template <>
struct _AndWriteBarrierPolicy<_write_barrier_policy, _write_barrier_policy>
{
    typedef _write_barrier_policy Policy;
};

// Combine Allocator + Data => write barrier policy
// Specialize RecyclerNonLeafAllocator
//
template <class Allocator, class T>
struct AllocatorWriteBarrierPolicy
{
    typedef typename AllocatorInfo<Allocator, void>::AllocatorType AllocatorType;
    typedef typename _AndWriteBarrierPolicy<
        typename _AllocatorTypeWriteBarrierPolicy<AllocatorType>::Policy,
        typename TypeWriteBarrierPolicy<T>::Policy>::Policy Policy;
};
template <class T>
struct AllocatorWriteBarrierPolicy<RecyclerNonLeafAllocator, T> { typedef _write_barrier_policy Policy; };
template <>
struct AllocatorWriteBarrierPolicy<RecyclerNonLeafAllocator, int> { typedef _no_write_barrier_policy Policy; };

// Choose WriteBarrierPtr or NoWriteBarrierPtr based on Policy
//
template <class T, class Policy>
struct _WriteBarrierPtrPolicy { typedef NoWriteBarrierPtr<T> Ptr; };
template <class T>
struct _WriteBarrierPtrPolicy<T, _write_barrier_policy> { typedef WriteBarrierPtr<T> Ptr; };

// Choose WriteBarrierPtr or NoWriteBarrierPtr based on Allocator and T* type
//
template <class T,
          class Allocator = Recycler,
          class Policy = typename AllocatorWriteBarrierPolicy<Allocator, T*>::Policy>
struct WriteBarrierPtrTraits { typedef typename _WriteBarrierPtrPolicy<T, Policy>::Ptr Ptr; };

// Choose WriteBarrierPtr type if Allocator is recycler type and element type T
// is a pointer type, otherwise use type T unchanged.
//
// Used to wrap array item type when write barrier is needed (wraps pointer
// item type with WriteBarrierPtr).
//
template <class T, class Policy>
struct _ArrayItemTypeTraits
{
    typedef T Type;
};
template <class T>
struct _ArrayItemTypeTraits<T*, _write_barrier_policy>
{
    typedef WriteBarrierPtr<T> Type;
};
template <class T,
          class Allocator,
          class Policy = typename AllocatorWriteBarrierPolicy<Allocator, T>::Policy>
struct WriteBarrierArrayItemTraits { typedef typename _ArrayItemTypeTraits<T, Policy>::Type Type; };

// ArrayWriteBarrier behavior
//
template <class Policy>
struct _ArrayWriteBarrier
{
    template <class T>
    static void WriteBarrier(T * address, size_t count) {}
};

#ifdef RECYCLER_WRITE_BARRIER
template <>
struct _ArrayWriteBarrier<_write_barrier_policy>
{
    template <class T>
    static void WriteBarrier(T * address, size_t count)
    {
        RecyclerWriteBarrierManager::WriteBarrier(address, sizeof(T) * count);
    }
};
#endif

// Trigger write barrier on changing array content if Allocator and element type
// determines write barrier is needed. Ignore otherwise.
//
template <class T, class Allocator = Recycler, class PolicyType = T>
void WriteBarrier(T * address, size_t count)
{
    typedef typename AllocatorWriteBarrierPolicy<Allocator, PolicyType>::Policy Policy;
    return _ArrayWriteBarrier<Policy>::WriteBarrier(address, count);
}

// Copy array content. Triggers write barrier on the dst array content if if
// Allocator and element type determines write barrier is needed.
//
template <class Allocator, class T, class PolicyType = T>
void CopyArray(T* dst, size_t dstCount, const T* src, size_t srcCount)
{
    js_memcpy_s(dst, sizeof(T) * dstCount, src, sizeof(T) * srcCount);
    WriteBarrier<T, Allocator, PolicyType>(dst, dstCount);
}
template <class Allocator, class T, class PolicyType = T>
void CopyArray(NoWriteBarrierPtr<T>& dst, size_t dstCount, const NoWriteBarrierPtr<T>& src, size_t srcCount)
{
    return CopyArray<Allocator, T, PolicyType>((T*)dst, dstCount, (const T*)src, srcCount);
}
template <class Allocator, class T, class PolicyType = T>
void CopyArray(WriteBarrierPtr<T>& dst, size_t dstCount, const WriteBarrierPtr<T>& src, size_t srcCount)
{
    return CopyArray<Allocator, T, PolicyType>((T*)dst, dstCount, (const T*)src, srcCount);
}


template <typename T>
class NoWriteBarrierField
{
public:
    NoWriteBarrierField() {}
    explicit NoWriteBarrierField(T const& value) : value(value) {}

    // Getters
    operator T const&() const { return value; }
    operator T&() { return value; }

    T const* operator&() const { return &value; }
    T* operator&() { return &value; }

    // Setters
    NoWriteBarrierField& operator=(T const& value)
    {
        this->value = value;
        return *this;
    }

private:
    T value;
};

template <typename T>
class NoWriteBarrierPtr
{
public:
    NoWriteBarrierPtr() {}
    NoWriteBarrierPtr(T * value) : value(value) {}

    // Getters
    T * operator->() const { return this->value; }
    operator T* const & () const { return this->value; }

    T* const * operator&() const { return &value; }
    T** operator&() { return &value; }

    // Setters
    NoWriteBarrierPtr& operator=(T * value)
    {
        this->value = value;
        return *this;
    }
private:
    T * value;
};

template <typename T>
class WriteBarrierObjectConstructorTrigger
{
public:
    WriteBarrierObjectConstructorTrigger(T* object, Recycler* recycler):
        object((char*) object),
        recycler(recycler)
    {
    }

    ~WriteBarrierObjectConstructorTrigger()
    {
        // WriteBarrier-TODO: trigger write barrier if the GC is in concurrent mark state
    }

    operator T*()
    {
        return object;
    }

private:
    T* object;
    Recycler* recycler;
};

template <typename T>
class WriteBarrierPtr
{
public:
    WriteBarrierPtr() {}
    WriteBarrierPtr(T * ptr)
    {
        // WriteBarrier
        NoWriteBarrierSet(ptr);
    }

    // Getters
    T * operator->() const { return ptr; }
    operator T* const & () const { return ptr; }

    T* const * AddressOf() const { return &ptr; }
    T** AddressOf() { return &ptr; }

    // Taking immutable address is ok
    //
    T* const * operator&() const
    {
        return &ptr;
    }
    // Taking mutable address is not allowed
    //
    // T** operator&()
    // {
    //     static_assert(false, "Might need to set barrier for this operation, and use AddressOf instead.");
    //     return &ptr;
    // }

    // Setters
    WriteBarrierPtr& operator=(T * ptr)
    {
        WriteBarrierSet(ptr);
        return *this;
    }
    void NoWriteBarrierSet(T * ptr)
    {
        this->ptr = ptr;
    }
    void WriteBarrierSet(T * ptr)
    {
        NoWriteBarrierSet(ptr);
#ifdef RECYCLER_WRITE_BARRIER
        RecyclerWriteBarrierManager::WriteBarrier(this);
#endif
    }

    WriteBarrierPtr& operator=(WriteBarrierPtr const& other)
    {
        WriteBarrierSet(other.ptr);
        return *this;
    }

    static void MoveArray(WriteBarrierPtr * dst, WriteBarrierPtr * src, size_t count)
    {
        memmove((void *)dst, src, sizeof(WriteBarrierPtr) * count);
        WriteBarrier(dst, count);
    }
    static void CopyArray(WriteBarrierPtr * dst, size_t dstCount, T const* src, size_t srcCount)
    {
        js_memcpy_s((void *)dst, sizeof(WriteBarrierPtr) * dstCount, src, sizeof(T *) * srcCount);
        WriteBarrier(dst, dstCount);
    }
    static void CopyArray(WriteBarrierPtr * dst, size_t dstCount, WriteBarrierPtr const* src, size_t srcCount)
    {
        js_memcpy_s((void *)dst, sizeof(WriteBarrierPtr) * dstCount, src, sizeof(WriteBarrierPtr) * srcCount);
        WriteBarrier(dst, dstCount);
    }
    static void ClearArray(WriteBarrierPtr * dst, size_t count)
    {
        // assigning NULL don't need write barrier, just cast it and null it out
        memset((void *)dst, 0, sizeof(WriteBarrierPtr<T>) * count);
    }
private:
    T * ptr;
};
}  // namespace Memory


template<class T> inline
const T& min(const T& a, const NoWriteBarrierField<T>& b) { return a < b ? a : b; }

template<class T> inline
const T& min(const NoWriteBarrierField<T>& a, const T& b) { return a < b ? a : b; }

template<class T> inline
const T& min(const NoWriteBarrierField<T>& a, const NoWriteBarrierField<T>& b) { return a < b ? a : b; }

template<class T> inline
const T& max(const NoWriteBarrierField<T>& a, const T& b) { return a > b ? a : b; }

// TODO: Add this method back once we figure out why OACR is tripping on it
template<class T> inline
const T& max(const T& a, const NoWriteBarrierField<T>& b) { return a > b ? a : b; }

template<class T> inline
const T& max(const NoWriteBarrierField<T>& a, const NoWriteBarrierField<T>& b) { return a > b ? a : b; }


// Disallow memcpy, memmove of WriteBarrierPtr

template <typename T>
void *  __cdecl memmove(_Out_writes_bytes_all_opt_(_Size) WriteBarrierPtr<T> * _Dst, _In_reads_bytes_opt_(_Size) const void * _Src, _In_ size_t _Size)
{
    CompileAssert(false);
}

template <typename T>
void __stdcall js_memcpy_s(__bcount(sizeInBytes) WriteBarrierPtr<T> *dst, size_t sizeInBytes, __bcount(count) const void *src, size_t count)
{
    CompileAssert(false);
}

template <typename T>
void *  __cdecl memset(_Out_writes_bytes_all_(_Size) WriteBarrierPtr<T> * _Dst, _In_ int _Val, _In_ size_t _Size)
{
    CompileAssert(false);
}

#include <Memory/WriteBarrierMacros.h>