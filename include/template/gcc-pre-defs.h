/*
 * (C) Copyright 2002-2026 Diomidis Spinellis
 *
 * This file is part of CScout.
 *
 * CScout is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CScout is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CScout.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Generic gcc workarounds.
 * Note that CScout has builtin support for __atribute__.
 */
#ifdef __cplusplus
#define __EXCEPTIONS
#endif

/* GCC __builtin_* funtions and misc */
#pragma keyword "asm" "__asm__"
#define __alignof__(x) (sizeof(x) & 0xf)
#define _Alignof(x) (sizeof(x) & 0xf)

#define _Atomic

/*
 * Atomic builtins
 * https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
 * via https://chatgpt.com/share/6801030d-b528-8011-9eab-811ae81fca54
 */
// Load
#define __atomic_load_n(ptr, memorder) (*(ptr))
#define __atomic_load(ptr, ret, memorder) (*(ret) = *(ptr))

// Store
#define __atomic_store_n(ptr, val, memorder) (*(ptr) = (val))
#define __atomic_store(ptr, val, memorder) (*(ptr) = *(val))

// Exchange
#define __atomic_exchange_n(ptr, val, memorder) ({ \
    __typeof__(*(ptr)) __old = *(ptr);            \
    *(ptr) = (val);                                \
    __old;                                         \
})
#define __atomic_exchange(ptr, val, ret, memorder) (*(ret) = *(ptr), *(ptr) = *(val))

// Compare-and-exchange
#define __atomic_compare_exchange_n(ptr, expected, desired, weak, succ, fail) ({ \
    bool __res = (*(ptr) == *(expected));                                       \
    if (__res) *(ptr) = (desired);                                              \
    else *(expected) = *(ptr);                                                  \
    __res;                                                                      \
})
#define __atomic_compare_exchange(ptr, expected, desired, weak, succ, fail) ({ \
    bool __res = (*(ptr) == *(expected));                                      \
    if (__res) *(ptr) = *(desired);                                            \
    else *(expected) = *(ptr);                                                 \
    __res;                                                                     \
})

// Arithmetic fetch-and-update (return old value)
#define __atomic_fetch_add(ptr, val, memorder) ({ __typeof__(*(ptr)) __old = *(ptr); *(ptr) += (val); __old; })
#define __atomic_fetch_sub(ptr, val, memorder) ({ __typeof__(*(ptr)) __old = *(ptr); *(ptr) -= (val); __old; })
#define __atomic_fetch_and(ptr, val, memorder) ({ __typeof__(*(ptr)) __old = *(ptr); *(ptr) &= (val); __old; })
#define __atomic_fetch_or(ptr, val, memorder)  ({ __typeof__(*(ptr)) __old = *(ptr); *(ptr) |= (val); __old; })
#define __atomic_fetch_xor(ptr, val, memorder) ({ __typeof__(*(ptr)) __old = *(ptr); *(ptr) ^= (val); __old; })
#define __atomic_fetch_nand(ptr, val, memorder) ({ __typeof__(*(ptr)) __old = *(ptr); *(ptr) = ~(*(ptr) & (val)); __old; })

// Arithmetic update-and-fetch (return new value)
#define __atomic_add_fetch(ptr, val, memorder) (*(ptr) += (val))
#define __atomic_sub_fetch(ptr, val, memorder) (*(ptr) -= (val))
#define __atomic_and_fetch(ptr, val, memorder) (*(ptr) &= (val))
#define __atomic_or_fetch(ptr, val, memorder)  (*(ptr) |= (val))
#define __atomic_xor_fetch(ptr, val, memorder) (*(ptr) ^= (val))
#define __atomic_nand_fetch(ptr, val, memorder) (*(ptr) = ~(*(ptr) & (val)))

// Test-and-set / Clear
#define __atomic_test_and_set(ptr, memorder) ({ \
    bool __old = (*(bool*)(ptr));              \
    *(bool*)(ptr) = 1;                          \
    __old;                                      \
})
#define __atomic_clear(ptr, memorder) (*(bool*)(ptr) = 0)

// Fences (no-op)
#define __atomic_thread_fence(memorder) ((void)0)
#define __atomic_signal_fence(memorder) ((void)0)

// Lock-free checks (naive assumptions)
#define __atomic_always_lock_free(size, ptr) (1)
#define __atomic_is_lock_free(size, ptr) (1)


extern void *__builtin_alloca(unsigned int);
extern void *__builtin_alloca_with_align(unsigned int, unsigned int);
extern void *__builtin_alloca_with_align_and_max(unsigned int, unsigned int, unsigned int);
extern void *__builtin_assume_aligned(const void *, unsigned int, ...);
#define __builtin_assoc_barrier(x) (x)
extern unsigned short __builtin_bswap16(unsigned short);
extern unsigned int __builtin_bswap32(unsigned int);
extern unsigned long long __builtin_bswap64(unsigned long long);
#define __builtin_add_overflow(x,y,z) ((x), (y), (z), 1)
#define __builtin_add_overflow_p(x,y,z) ((x), (y), 1)
#define __builtin_call_with_static_chain(x,y) ((x), (y), 0)
extern void __builtin___clear_cache(void *, void *);
extern int __builtin_clrsb(int);
extern int __builtin_clrsbl(long);
extern int __builtin_clrsbll(long long);
extern int __builtin_clz(unsigned int);
extern int __builtin_clzl(unsigned long);
extern int __builtin_clzll(unsigned long long);
#define __builtin_complex(x,y) ((x), (y))
#define __builtin_classify_type(x) ((x), 0)
extern void __builtin_clear_padding(void *);
#define __builtin_counted_by_ref(x) ((x), (void *)0)
#define __builtin_constant_p(_x) ((_x), 0)
#define __builtin_constant(x) ((x), 0)
extern int __builtin_ctz(unsigned int);
extern int __builtin_ctzl(unsigned long);
extern int __builtin_ctzll(unsigned long long);
extern long __builtin_expect(long, long);
extern long __builtin_expect_with_probability(long, long, double);
extern unsigned long __builtin_extend_pointer(void *);
extern double __builtin_fabs(double);
extern float __builtin_fabsf(float);
extern long double __builtin_fabsl(long double);
extern int __builtin_ffs(int);
extern int __builtin_ffsl(long);
extern int __builtin_ffsll(long long);
extern const char *__builtin_FILE(void);
extern int __builtin_fpclassify(int, int, int, int, int, ...);
extern int __builtin___fprintf_chk(void *, int, const char *, ...);
extern void *__builtin_frame_address(unsigned int);
extern const char *__builtin_FUNCTION(void);
extern int __builtin_goacc_parlevel_id(int);
extern int __builtin_goacc_parlevel_size(int);
#define __builtin_has_attribute(x, a) (1)
extern double __builtin_huge_val(void);
extern float __builtin_huge_valf(void);
#define __builtin_huge_valfn() 1
#define __builtin_huge_valfnx() 1
extern long double __builtin_huge_vall(void);
extern _Decimal32 __builtin_infd32(void);
extern _Decimal64 __builtin_infd64(void);
extern double __builtin_inf(void);
extern float __builtin_inff(void);
#define __builtin_inffnx() 1
extern long double __builtin_infl(void);
extern int __builtin_isinf_sign(...);
extern int __builtin_LINE(void);
extern void *__builtin_memchr(const void *, int, unsigned int);
extern int __builtin_memcmp(const void *, const void *, unsigned int);
extern void *__builtin___memcpy_chk(void *, const void *, unsigned int, unsigned int);
extern void *__builtin_memcpy(void *, const void *, unsigned int);
extern void *__builtin_memmove(void *, const void *, unsigned int);
extern void *__builtin___memmove_chk(void *, const void *, unsigned int, unsigned int);
extern void *__builtin___mempcpy_chk(void *, const void *, unsigned int, unsigned int);
extern void *__builtin_memset(void *, int, unsigned int);
extern void *__builtin___memset_chk(void *, int, unsigned int, unsigned int);
#define __builtin_mul_overflow(x,y,z) ((x), (y), (z), 1)
#define __builtin_mul_overflow_p(x,y,z) ((x), (y), 1)
extern _Decimal128 __builtin_nand128(const char *);
extern _Decimal32 __builtin_nand32(const char *);
extern _Decimal64 __builtin_nand64(const char *);
#define __builtin_nanfn(x) ((x), 1.)
#define __builtin_nanfnx(x) ((x), 1.)
extern double __builtin_nan(const char *);
extern float __builtin_nanf(const char *);
extern long double __builtin_nanl(const char *);
extern double __builtin_nans(const char *);
extern float __builtin_nansf(const char *);
#define __builtin_nansfnx(x) ((x), 1)
extern long double __builtin_nansl(const char *);
#define __builtin_next_arg(_x) (_x, 0)
extern unsigned int __builtin_object_size(const void *, int);
#define __builtin_offsetof(_t, _m) (((size_t) &((_t *)0)->_m))
extern int __builtin_parity(unsigned int);
extern int __builtin_parityl(unsigned long);
extern int __builtin_parityll(unsigned long long);
extern int __builtin_popcount(unsigned int);
extern int __builtin_popcountl(unsigned long);
extern int __builtin_popcountll(unsigned long long);
extern double __builtin_powi(double, int);
extern long double __builtin_powl(long double, long double);
extern void __builtin_prefetch(const void *, ...);
extern int __builtin___printf_chk(int, const char *, ...);
extern void *__builtin_return_address(unsigned int);
extern int __builtin___snprintf_chk(char *, unsigned int, int, unsigned int, const char *, ...);
#define __builtin_speculation_safe_value(x,y) ((x), (y), 1)
extern int __builtin___sprintf_chk(char *, int, unsigned int, const char *, ...);
#define __builtin_stdarg_start
extern char *__builtin___stpcpy_chk(char *, const char *, unsigned int);
extern char *__builtin___strcat_chk(char *, const char *, unsigned int);
extern char *__builtin_strchr(const char *, int);
extern int __builtin_strcmp(const char *, const char *);
extern char *__builtin_strcpy(char *, const char *);
extern char *__builtin___strcpy_chk(char *, const char *, unsigned int);
extern unsigned int __builtin_strcspn(const char *, const char *);
extern unsigned int __builtin_strlen(const char *);
extern char *__builtin_strncat(char *, const char *, unsigned int);
extern char *__builtin_strncpy(char *, const char *, unsigned int);
extern char *__builtin___strncat_chk(char *, const char *, unsigned int, unsigned int);
extern char *__builtin___strncpy_chk(char *, const char *, unsigned int, unsigned int);
#define __builtin_sub_overflow(x,y,z) ((x), (y), (z), 1)
#define __builtin_sub_overflow_p(x,y,z) ((x), (y), 1)
#define __builtin_tgmath(x, y) ((x), (y), 0)
extern void __builtin_trap(void);
#define __builtin_types_compatible_p(x,y) (1)
extern void __builtin_unreachable(void);
#define __builtin_va_arg(_ap, _type) (*(_type *)(_ap))
#define __builtin_va_arg_pack() 1
#define __builtin_va_arg_pack_len() 1
#define __builtin_va_copy(_a, _b) ((_a), (_b), 0)
#define __builtin_va_end(_v) ((void)(_v))
#define __builtin_va_list void *
#define __builtin_va_start(_ap, _arg) ((void)(_ap),(void)(_arg))
extern int __builtin___vprintf_chk(int, const char *, void *);
extern int __builtin___vsnprintf_chk(char *, unsigned int, int, unsigned int, const char *, void *);
extern int __builtin___vsprintf_chk(char *, int, unsigned int, const char *, void *);
// Handled internally, but allow defined() to return true.
#define __COUNTER__ 0
#define __extension
#pragma define_immutable __extension__
#define __has_attribute(x) 1
#define __int128 long
#define __int128_t long
#define __uint128_t long
#define _Float32 float
#define _Float32x float
#define _Float64 double
#define _Float64x double
#define __float128 double
#define _Float128 double
#define __seg_fs
#define __seg_gs
#define __PRETTY_FUNCTION__ "UNKNOWN"
