/*
    enoki/cuda.h -- CUDA-backed Enoki dynamic array with JIT compilation

    Enoki is a C++ template library that enables transparent vectorization
    of numerical kernels using SIMD instruction sets available on current
    processor architectures.

    Copyrighe (c) 2018 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a BSD-style
    license that can be found in the LICENSE file.
*/

#pragma once

#define ENOKI_CUDA 1

#include <enoki/array.h>

NAMESPACE_BEGIN(enoki)

// -----------------------------------------------------------------------
//! @{ \name Imports from libenoki-cuda.so
// -----------------------------------------------------------------------

/// Initialize the tracing JIT
extern ENOKI_IMPORT void cuda_init();

/// Delete the trace, requires a subsequent call by cuda_init()
extern ENOKI_IMPORT void cuda_shutdown();

/// Compile and evaluate the trace up to the current instruction
extern ENOKI_IMPORT void cuda_eval(bool log_assembly /* = false */);

/// Invokes 'cuda_eval' if the given variable has not been evaluated yet
extern ENOKI_IMPORT void cuda_eval_var(uint32_t index, bool log_assembly = false);

/// Increase the reference count of a variable
extern ENOKI_IMPORT void cuda_inc_ref_ext(uint32_t);

/// Decrease the reference count of a variable
extern ENOKI_IMPORT void cuda_dec_ref_ext(uint32_t);

/// Return the size of a variable
extern ENOKI_IMPORT size_t cuda_var_size(uint32_t);

/// Return the pointer address of a variable (in device memory)
extern ENOKI_IMPORT void*  cuda_var_ptr(uint32_t);

/// Retroactively adjust the recorded size of a variable
extern ENOKI_IMPORT void   cuda_var_set_size(uint32_t, size_t);

/// Mark a variable as dirty (e.g. due to scatter)
extern ENOKI_IMPORT void   cuda_var_mark_dirty(uint32_t);

/// Attach a label to a variable (written to PTX assembly)
extern ENOKI_IMPORT void   cuda_var_set_label(uint32_t, const char *);

/// Needed to mark certain instructions with side effects (e.g. scatter)
extern ENOKI_IMPORT void   cuda_var_mark_side_effect(uint32_t);

/// Set the current scatter/source operand array
extern ENOKI_IMPORT void   cuda_set_scatter_gather_operand(uint32_t);

/// Append an operation to the trace (0 arguments)
extern ENOKI_IMPORT uint32_t cuda_trace_append(EnokiType type,
                                               const char *op);

/// Append an operation to the trace (1 argument)
extern ENOKI_IMPORT uint32_t cuda_trace_append(EnokiType type,
                                               const char *op,
                                               uint32_t arg1);

/// Append an operation to the trace (2 arguments)
extern ENOKI_IMPORT uint32_t cuda_trace_append(EnokiType type,
                                               const char *op,
                                               uint32_t arg1,
                                               uint32_t arg2);

/// Append an operation to the trace (3 arguments)
extern ENOKI_IMPORT uint32_t cuda_trace_append(EnokiType type,
                                               const char *op,
                                               uint32_t arg1,
                                               uint32_t arg2,
                                               uint32_t arg3);

/// Insert a "printf" instruction for the given instruction
extern ENOKI_IMPORT void cuda_trace_printf(const char *fmt, uint32_t narg,
                                           uint32_t *arg);

/// Computes the horizontal sum of a given memory region
template <typename T> extern ENOKI_IMPORT T cuda_hsum(size_t, const T *);

/// Computes the horizontal product of a given memory region
template <typename T> extern ENOKI_IMPORT T cuda_hprod(size_t, const T *);

/// Computes the horizontal maximum of a given memory region
template <typename T> extern ENOKI_IMPORT T cuda_hmax(size_t, const T *);

/// Computes the horizontal minimum of a given memory region
template <typename T> extern ENOKI_IMPORT T cuda_hmin(size_t, const T *);

/// Computes a horizontal reduction of a mask array via AND
extern ENOKI_IMPORT bool cuda_all(size_t, const bool *);

/// Computes a horizontal reduction of a mask array via OR
extern ENOKI_IMPORT bool cuda_any(size_t, const bool *);

extern ENOKI_IMPORT std::pair<std::vector<std::pair<void *, size_t>>, size_t *>
cuda_partition(size_t, const void **);

extern ENOKI_IMPORT uint32_t cuda_var_copy_to_device(EnokiType type,
                                                     size_t size, const void *value);

/// Register a memory region (in device memory) as a variable
extern ENOKI_IMPORT uint32_t cuda_var_register(EnokiType type, size_t size,
                                               void *ptr, uint32_t parent,
                                               bool dealloc);

/// Fetch a scalar value from a CUDA array (in device memory)
extern ENOKI_IMPORT void cuda_fetch_element(void *, uint32_t, size_t, size_t);

/// Copy a memory region to the device
extern ENOKI_IMPORT void cuda_memcpy_to_device(void *dst, const void *src, size_t);

/// Copy a memory region from  the device
extern ENOKI_IMPORT void cuda_memcpy_from_device(void *dst, const void *src, size_t);

/// Allocate unified memory (wrapper around cudaMalloc)
extern ENOKI_IMPORT void* cuda_malloc(size_t);

/// Allocate unified memory (wrapper around cudaMalloc & cudaMemset)
extern ENOKI_IMPORT void* cuda_malloc_zero(size_t);

/// Allocate unified memory (wrapper around cudaMalloc)
extern ENOKI_IMPORT void* cuda_managed_malloc(size_t);

/// Release unified memory (wrapper around cudaFree)
extern ENOKI_IMPORT void cuda_free(void *);

//! @}
// -----------------------------------------------------------------------

template <typename Value>
struct CUDAArray : ArrayBase<value_t<Value>, CUDAArray<Value>> {
    template <typename T> friend struct CUDAArray;
    using Index = uint32_t;

    static constexpr EnokiType Type = enoki_type_v<Value>;
    static constexpr bool IsCUDA = true;
    static constexpr bool Approx = true;
    template <typename T> using ReplaceValue = CUDAArray<T>;
    using MaskType = CUDAArray<bool>;
    using ArrayType = CUDAArray;

    CUDAArray() = default;

    ~CUDAArray() {
        cuda_dec_ref_ext(m_index);
    }

    CUDAArray(const CUDAArray &a) : m_index(a.m_index) {
        cuda_inc_ref_ext(m_index);
    }

    CUDAArray(CUDAArray &&a) : m_index(a.m_index) {
        a.m_index = 0;
    }

    template <typename T> CUDAArray(const CUDAArray<T> &v) {
        const char *op;

        if (std::is_floating_point_v<T> && std::is_integral_v<Value>)
            op = "cvt.rzi.$t1.$t2 $r1, $r2";
        else if (std::is_integral_v<T> && std::is_floating_point_v<Value>)
            op = "cvt.rn.$t1.$t2 $r1, $r2";
        else
            op = "cvt.$t1.$t2 $r1, $r2";

        m_index = cuda_trace_append(Type, op, v.index_());
    }

    template <typename T>
    CUDAArray(const CUDAArray<T> &v, detail::reinterpret_flag) {
        static_assert(sizeof(T) == sizeof(Value));
        if (std::is_integral_v<T> == std::is_integral_v<Value>) {
            m_index = cuda_trace_append(Type, "mov.$b1 $r1, $r2", v.index_());
        } else {
            m_index = v.index_();
            cuda_inc_ref_ext(m_index);
        }
    }

    template <typename T, enable_if_t<std::is_scalar_v<T>> = 0>
    CUDAArray(T value) : CUDAArray((Value) value) { }

    CUDAArray(Value value) {
        const char *fmt = nullptr;

        switch (Type) {
            case EnokiType::Float16:
                fmt = "mov.$t1 $r1, %04x";
                break;

            case EnokiType::Float32:
                fmt = "mov.$t1 $r1, 0f%08x";
                break;

            case EnokiType::Float64:
                fmt = "mov.$t1 $r1, 0d%016llx";
                break;

            case EnokiType::Bool:
                fmt = "mov.$t1 $r1, %i";
                break;

            case EnokiType::Int8:
            case EnokiType::UInt8:
                fmt = "mov.$t1 $r1, 0x%02x";
                break;

            case EnokiType::Int16:
            case EnokiType::UInt16:
                fmt = "mov.$t1 $r1, 0x%04x";
                break;

            case EnokiType::Int32:
            case EnokiType::UInt32:
                fmt = "mov.$t1 $r1, 0x%08x";
                break;

            case EnokiType::Pointer:
            case EnokiType::Int64:
            case EnokiType::UInt64:
                fmt = "mov.$t1 $r1, 0x%016llx";
                break;

            default:
                fmt = "<<invalid format during cast>>";
                break;
        }

        char tmp[32];
        snprintf(tmp, 32, fmt, memcpy_cast<uint_array_t<Value>>(value));

        m_index = cuda_trace_append(Type, tmp);
    }

    template <typename... Args, enable_if_t<(sizeof...(Args) > 1)> = 0>
    CUDAArray(Args&&... args) {
        Value data[] = { (Value) args... };
        m_index = cuda_var_copy_to_device(Type, sizeof...(Args), data);
    }

    CUDAArray &operator=(const CUDAArray &a) {
        cuda_dec_ref_ext(m_index);
        m_index = a.m_index;
        cuda_inc_ref_ext(m_index);
        return *this;
    }

    CUDAArray &operator=(CUDAArray &&a) {
        std::swap(m_index, a.m_index);
        return *this;
    }

    CUDAArray add_(const CUDAArray &v) const {
        const char *op = std::is_floating_point_v<Value>
            ? "add.rn.ftz.$t1 $r1, $r2, $r3"
            : "add.$t1 $r1, $r2, $r3";

        return CUDAArray::from_index(
            cuda_trace_append(Type, op, index_(), v.index_()));
    }

    CUDAArray sub_(const CUDAArray &v) const {
        const char *op = std::is_floating_point_v<Value>
            ? "sub.rn.ftz.$t1 $r1, $r2, $r3"
            : "sub.$t1 $r1, $r2, $r3";

        return CUDAArray::from_index(
            cuda_trace_append(Type, op, index_(), v.index_()));
    }

    CUDAArray mul_(const CUDAArray &v) const {
        const char *op = std::is_floating_point_v<Value>
            ? "mul.rn.ftz.$t1 $r1, $r2, $r3"
            : "mul.lo.$t1 $r1, $r2, $r3";

        return CUDAArray::from_index(
            cuda_trace_append(Type, op, index_(), v.index_()));
    }

    CUDAArray div_(const CUDAArray &v) const {
        const char *op = std::is_floating_point_v<Value>
            ? "div.rn.ftz.$t1 $r1, $r2, $r3"
            : "div.$t1 $r1, $r2, $r3";

        return CUDAArray::from_index(
            cuda_trace_append(Type, op, index_(), v.index_()));
    }

    CUDAArray mod_(const CUDAArray &v) const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            std::string("rem.$t1 $r1, $r2, $r3"), index_(), v.index_()));
    }

    CUDAArray fmadd_(const CUDAArray &a, const CUDAArray &b) const {
        const char *op = std::is_floating_point_v<Value>
            ? "fma.rn.ftz.$t1 $r1, $r2, $r3, $r4"
            : "mad.lo.$t1 $r1, $r2, $r3, $r4";

        return CUDAArray::from_index(
            cuda_trace_append(Type, op, index_(), a.index_(), b.index_()));
    }

    CUDAArray fmsub_(const CUDAArray &a, const CUDAArray &b) const {
        return fmadd_(a, -b);
    }

    CUDAArray fnmadd_(const CUDAArray &a, const CUDAArray &b) const {
        return fmadd_(-a, b);
    }

    CUDAArray fnmsub_(const CUDAArray &a, const CUDAArray &b) const {
        return -fmadd_(a, b);
    }

    CUDAArray max_(const CUDAArray &v) const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "max.$t1 $r1, $r2, $r3", index_(), v.index_()));
    }

    CUDAArray min_(const CUDAArray &v) const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "min.$t1 $r1, $r2, $r3", index_(), v.index_()));
    }

    CUDAArray abs_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "abs.$t1 $r1, $r2", index_()));
    }

    CUDAArray neg_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "neg.$t1 $r1, $r2", index_()));
    }

    CUDAArray sqrt_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "sqrt.rn.ftz.$t1 $r1, $r2", index_()));
    }

    CUDAArray exp_() const {
        CUDAArray scaled = Value(1.4426950408889634074) * *this;
        return CUDAArray::from_index(cuda_trace_append(Type,
            "ex2.approx.ftz.$t1 $r1, $r2", scaled.index_()));
    }

    CUDAArray log_() const {
        return CUDAArray::from_index(cuda_trace_append(
            Type, "lg2.approx.ftz.$t1 $r1, $r2",
            index_())) * Value(0.69314718055994530942);
    }

    CUDAArray sin_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "sin.approx.ftz.$t1 $r1, $r2", index_()));
    }

    CUDAArray cos_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "cos.approx.ftz.$t1 $r1, $r2", index_()));
    }

    std::pair<CUDAArray, CUDAArray> sincos_() const {
        return { sin_(), cos_() };
    }

    CUDAArray rcp_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "rcp.approx.ftz.$t1 $r1, $r2", index_()));
    }

    CUDAArray rsqrt_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "rsqrt.approx.ftz.$t1 $r1, $r2", index_()));
    }

    CUDAArray floor_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "cvt.rmi.$t1.$t1 $r1, $r2", index_()));
    }

    CUDAArray ceil_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "cvt.rpi.$t1.$t1 $r1, $r2", index_()));
    }

    CUDAArray round_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "cvt.rni.$t1.$t1 $r1, $r2", index_()));
    }

    CUDAArray trunc_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "cvt.rzi.$t1.$t1 $r1, $r2", index_()));
    }

    template <typename T> T floor2int_() const {
        return CUDAArray::from_index(cuda_trace_append(T::Type,
            "cvt.rmi.$t1.$t2 $r1, $r2", index_()));
    }

    template <typename T> T ceil2int_() const {
        return CUDAArray::from_index(cuda_trace_append(T::Type,
            "cvt.rpi.$t1.$t2 $r1, $r2", index_()));
    }

    CUDAArray sl_(const CUDAArray &v) const {
        if constexpr (sizeof(Value) == 4)
            return CUDAArray::from_index(cuda_trace_append(Type,
                "shl.$b1 $r1, $r2, $r3", index_(), v.index_()));
        else
            return CUDAArray::from_index(cuda_trace_append(Type,
                "shl.$b1 $r1, $r2, $r3", index_(), CUDAArray<int32_t>(v).index_()));
    }

    CUDAArray sr_(const CUDAArray &v) const {
        if constexpr (sizeof(Value) == 4)
            return CUDAArray::from_index(cuda_trace_append(Type,
                "shr.$b1 $r1, $r2, $r3", index_(), v.index_()));
        else
            return CUDAArray::from_index(cuda_trace_append(Type,
                "shr.$b1 $r1, $r2, $r3", index_(), CUDAArray<int32_t>(v).index_()));
    }

    template <size_t Imm> CUDAArray sl_() const { return sl_(Value(Imm)); }
    template <size_t Imm> CUDAArray sr_() const { return sr_(Value(Imm)); }

    CUDAArray not_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "not.$b1 $r1, $r2", index_()));
    }

    CUDAArray popcnt_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "popc.$b1 $r1, $r2", index_()));
    }

    CUDAArray lzcnt_() const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "clz.$b1 $r1, $r2", index_()));
    }

    template <typename T>
    CUDAArray or_(const CUDAArray<T> &v) const {
        Value all_ones = memcpy_cast<Value>(int_array_t<Value>(-1));
        ENOKI_MARK_USED(all_ones);

        if constexpr (std::is_same_v<T, Value>)
            return CUDAArray::from_index(cuda_trace_append(Type,
                "or.$b1 $r1, $r2, $r3", index_(), v.index_()));
        else
            return CUDAArray::from_index(cuda_trace_append(Type,
                "selp.$t1 $r1, $r2, $r3, $r4", CUDAArray(all_ones).index_(),
                index_(), v.index_()));
    }

    template <typename T>
    CUDAArray and_(const CUDAArray<T> &v) const {
        Value all_zeros = memcpy_cast<Value>(int_array_t<Value>(0));
        ENOKI_MARK_USED(all_zeros);

        if constexpr (std::is_same_v<T, Value>)
            return CUDAArray::from_index(cuda_trace_append(Type,
                "and.$b1 $r1, $r2, $r3", index_(), v.index_()));
        else
            return CUDAArray::from_index(cuda_trace_append(Type,
                "selp.$t1 $r1, $r2, $r3, $r4", index_(),
                CUDAArray(all_zeros).index_(), v.index_()));
    }

    template <typename T> CUDAArray andnot_(const CUDAArray<T> &v) const {
        return and_(!v);
    }

    CUDAArray xor_(const CUDAArray &v) const {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "xor.$b1 $r1, $r2, $r3", index_(), v.index_()));
    }

    MaskType gt_(const CUDAArray &v) const {
        return MaskType::from_index(cuda_trace_append(EnokiType::Bool,
            "setp.gt.$t2 $r1, $r2, $r3", index_(), v.index_()));
    }

    MaskType ge_(const CUDAArray &v) const {
        return MaskType::from_index(cuda_trace_append(EnokiType::Bool,
            "setp.ge.$t2 $r1, $r2, $r3", index_(), v.index_()));
    }

    MaskType lt_(const CUDAArray &v) const {
        return MaskType::from_index(cuda_trace_append(EnokiType::Bool,
            "setp.lt.$t2 $r1, $r2, $r3", index_(), v.index_()));
    }

    MaskType le_(const CUDAArray &v) const {
        return MaskType::from_index(cuda_trace_append(EnokiType::Bool,
            "setp.le.$t2 $r1, $r2, $r3", index_(), v.index_()));
    }

    MaskType eq_(const CUDAArray &v) const {
        return MaskType::from_index(cuda_trace_append(EnokiType::Bool,
            "setp.eq.$t2 $r1, $r2, $r3", index_(), v.index_()));
    }

    MaskType neq_(const CUDAArray &v) const {
        return MaskType::from_index(cuda_trace_append(EnokiType::Bool,
            "setp.ne.$t2 $r1, $r2, $r3", index_(), v.index_()));
    }

    static CUDAArray select_(const MaskType &m, const CUDAArray &t, const CUDAArray &f) {
        return CUDAArray::from_index(cuda_trace_append(Type,
            "selp.$t1 $r1, $r2, $r3, $r4", t.index_(), f.index_(), m.index_()));
    }

    static CUDAArray arange_(ssize_t start, ssize_t stop, ssize_t step) {
        size_t size = size_t((stop - start + step - (step > 0 ? 1 : -1)) / step);

        using UInt32 = CUDAArray<uint32_t>;
        UInt32 index = UInt32::from_index(
            cuda_trace_append(EnokiType::UInt32, "mov.u32 $r1, $r2", 2));
        cuda_var_set_size(index.index_(), size);

        if (start == 0 && step == 1)
            return index;
        else
            return fmadd(index, CUDAArray((Value) step), CUDAArray((Value) start));
    }

    static CUDAArray linspace_(Value min, Value max, size_t size) {
        using UInt32 = CUDAArray<uint32_t>;
        UInt32 index = UInt32::from_index(
            cuda_trace_append(EnokiType::UInt32, "mov.u32 $r1, $r2", 2));
        cuda_var_set_size(index.index_(), size);

        Value step = (max - min) / Value(size - 1);
        return fmadd(index, CUDAArray(step), CUDAArray(min));
    }

    static CUDAArray zero_(size_t size) {
        if (size <= 1) {
            CUDAArray result(Value(0));
            cuda_var_set_size(result.index_(), size);
            return result;
        } else {
            return CUDAArray::from_index(cuda_var_register(
                Type, size, cuda_malloc_zero(size * sizeof(Value)), 0, true));
        }
    }

    static CUDAArray empty_(size_t size) {
        return zero_(size);
    }

    static CUDAArray full_(const Value &value, size_t size) {
        CUDAArray result(value);
        cuda_var_set_size(result.index_(), size);
        return result;
    }

    Value hsum_() const {
        cuda_eval_var(m_index);
        return cuda_hsum(cuda_var_size(m_index), (const Value *) cuda_var_ptr(m_index));
    }

    Value hprod_() const {
        cuda_eval_var(m_index);
        return cuda_hprod(cuda_var_size(m_index), (const Value *) cuda_var_ptr(m_index));
    }

    Value hmax_() const {
        cuda_eval_var(m_index);
        return cuda_hmax(cuda_var_size(m_index), (const Value *) cuda_var_ptr(m_index));
    }

    Value hmin_() const {
        cuda_eval_var(m_index);
        return cuda_hmin(cuda_var_size(m_index), (const Value *) cuda_var_ptr(m_index));
    }

    bool all_() const {
        cuda_eval_var(m_index);
        return cuda_all(cuda_var_size(m_index), (const Value *) cuda_var_ptr(m_index));
    }

    bool any_() const {
        cuda_eval_var(m_index);
        return cuda_any(cuda_var_size(m_index), (const Value *) cuda_var_ptr(m_index));
    }

    static CUDAArray map(void *ptr, size_t size) {
        return CUDAArray::from_index(
            cuda_var_register(Type, size, ptr, 0, false));
    }

    template <typename T = Value, enable_if_t<std::is_pointer_v<T>> = 0>
    std::vector<std::pair<Value, CUDAArray<uint64_t>>> partition_() const {
        cuda_eval_var(m_index);
        auto [rle, perm] = cuda_partition(size(), (const void **) data());
        uint32_t parent =
            cuda_var_register(EnokiType::UInt64, 1, perm, 0, true);

        std::vector<std::pair<Value, CUDAArray<uint64_t>>> result;
        result.reserve(rle.size());
        for (auto [ptr_r, size_r] : rle) {
            result.emplace_back(
                (Value) ptr_r,
                CUDAArray<uint64_t>::from_index(cuda_var_register(
                    EnokiType::UInt64, size_r, perm, parent, false)));
            perm += size_r;
        }

        cuda_dec_ref_ext(parent);
        return result;
    }

    template <size_t Stride, typename Index, typename Mask>
    static CUDAArray gather_(const void *ptr_, const Index &index,
                             const Mask &mask) {
        if (ptr_ == nullptr)
            throw std::runtime_error(
                "CUDAArray::gather_() invoked with base pointer NULL.");

        using UInt64 = CUDAArray<uint64_t>;

        UInt64 ptr    = UInt64::alloc_const(memcpy_cast<uintptr_t>(ptr_)),
               ptr_gl = UInt64::from_index(cuda_trace_append(
                          UInt64::Type, "cvta.to.global.$t1 $r1, $r2", ptr.index_())),
               addr   = fmadd(index, (uint64_t) Stride, ptr_gl);

        return CUDAArray::from_index(cuda_trace_append(Type,
            "@$r3 ld.global.$t1 $r1, [$r2];\n    @!$r3 mov.$b1 $r1, 0",
            addr.index_(), mask.index_()));
    }

    template <size_t Stride, typename Index, typename Mask>
    ENOKI_INLINE void scatter_(void *ptr_, const Index &index, const Mask &mask) const {
        if (ptr_ == nullptr)
            throw std::runtime_error(
                "CUDAArray::scatter_() invoked with base pointer NULL.");

        using UInt64 = CUDAArray<uint64_t>;

        UInt64 ptr    = UInt64::alloc_const(memcpy_cast<uintptr_t>(ptr_)),
               ptr_gl = UInt64::from_index(cuda_trace_append(
                          UInt64::Type, "cvta.to.global.$t1 $r1, $r2", ptr.index_())),
               addr   = fmadd(index, (uint64_t) Stride, ptr_gl);

        CUDAArray::Index var = cuda_trace_append(EnokiType::UInt64,
            "@$r4 st.global.$t3 [$r2], $r3",
            addr.index_(), m_index, mask.index_()
        );

        cuda_var_mark_side_effect(var);
    }

    template <size_t Stride, typename Index, typename Mask>
    void scatter_add_(void *ptr_, const Index &index, const Mask &mask) const {
        if (ptr_ == nullptr)
            throw std::runtime_error(
                "CUDAArray::scatter_add_() invoked with base pointer NULL.");

        using UInt64  = CUDAArray<uint64_t>;
        UInt64 ptr    = UInt64::alloc_const(memcpy_cast<uintptr_t>(ptr_)),
               ptr_gl = UInt64::from_index(cuda_trace_append(
                          UInt64::Type, "cvta.to.global.$t1 $r1, $r2", ptr.index_())),
               addr   = fmadd(index, (uint64_t) Stride, ptr_gl);

        CUDAArray::Index var = cuda_trace_append(Type,
            "@$r4 atom.global.add.$t1 $r1, [$r2], $r3",
            addr.index_(), m_index, mask.index_()
        );

        cuda_var_mark_side_effect(var);
    }

    auto operator->() const {
        using BaseType = std::decay_t<std::remove_pointer_t<Value>>;
        return call_support<BaseType, CUDAArray>(*this);
    }

    Index index_() const { return m_index; }
    size_t size() const { return cuda_var_size(m_index); }
    const void *data() const { return cuda_var_ptr(m_index); }
    void *data() { return cuda_var_ptr(m_index); }
    void resize(size_t size) {
        cuda_var_set_size(m_index, size);
    }

    Value coeff(size_t i) const {
        Value result = (Value) 0;
        cuda_fetch_element(&result, m_index, i, sizeof(Value));
        return result;
    }

protected:
    static CUDAArray from_index(Index index) {
        CUDAArray a;
        a.m_index = index;
        return a;
    }

    static CUDAArray alloc_const(Value value) {
        CUDAArray a;
        a.m_index = cuda_var_copy_to_device(Type, 1, &value);
        return a;
    }

protected:
    Index m_index = 0;
};

template <typename T, enable_if_t<!is_diff_array_v<T> && is_cuda_array_v<T>> = 0>
ENOKI_INLINE void set_label(const T& a, const char *label) {
    if constexpr (array_depth_v<T> >= 2) {
        for (size_t i = 0; i < T::Size; ++i)
            set_label(a.coeff(i), (std::string(label) + "." + std::to_string(i)).c_str());
    } else {
        cuda_var_set_label(a.index_(), label);
    }
}

template <typename T> class cuda_managed_allocator {
public:
    using value_type = T;
    using reference = T &;
    using const_reference = const T &;

    cuda_managed_allocator() {}

    template <typename T2>
    cuda_managed_allocator(const cuda_managed_allocator<T2> &) { }

    value_type *allocate(size_t n) {
        return (value_type *) cuda_managed_malloc(n * sizeof(T));
    }

    void deallocate(value_type *ptr, size_t) {
        cuda_free(ptr);
    }
};

#if defined(ENOKI_AUTODIFF) && !defined(ENOKI_BUILD)
    extern ENOKI_IMPORT template struct Tape<CUDAArray<float>>;
    extern ENOKI_IMPORT template struct DiffArray<CUDAArray<float>>;

    extern ENOKI_IMPORT template struct Tape<CUDAArray<double>>;
    extern ENOKI_IMPORT template struct DiffArray<CUDAArray<double>>;
#endif

NAMESPACE_END(enoki)
