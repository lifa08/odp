/* Copyright (c) 2017-2021, ARM Limited
 * Copyright (c) 2017-2018, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PLATFORM_LINUXGENERIC_ARCH_ARM_ODP_ATOMIC_H
#define PLATFORM_LINUXGENERIC_ARCH_ARM_ODP_ATOMIC_H

#ifndef PLATFORM_LINUXGENERIC_ARCH_ARM_ODP_CPU_H
#error This file should not be included directly, please include odp_cpu.h
#endif

#ifdef CONFIG_DMBSTR

#define atomic_store_release(loc, val, ro)		\
do {							\
	_odp_release_barrier(ro);			\
	__atomic_store_n(loc, val, __ATOMIC_RELAXED);   \
} while (0)

#else

#define atomic_store_release(loc, val, ro) \
	__atomic_store_n(loc, val, __ATOMIC_RELEASE)

#endif  /* CONFIG_DMBSTR */

#define HAS_ACQ(mo) ((mo) != __ATOMIC_RELAXED && (mo) != __ATOMIC_RELEASE)
#define HAS_RLS(mo) ((mo) == __ATOMIC_RELEASE || (mo) == __ATOMIC_ACQ_REL || \
		     (mo) == __ATOMIC_SEQ_CST)

#define LL_MO(mo) (HAS_ACQ((mo)) ? __ATOMIC_ACQUIRE : __ATOMIC_RELAXED)
#define SC_MO(mo) (HAS_RLS((mo)) ? __ATOMIC_RELEASE : __ATOMIC_RELAXED)

#ifndef __ARM_FEATURE_QRDMX /* Feature only available in v8.1a and beyond */
static inline bool
__lockfree_compare_exchange_16(register __int128 *var, __int128 *exp,
			       register __int128 neu, bool weak, int mo_success,
			       int mo_failure)
{
	(void)weak; /* Always do strong CAS or we can't perform atomic read */
	/* Ignore memory ordering for failure, memory order for
	 * success must be stronger or equal. */
	(void)mo_failure;
	register __int128 old;
	register __int128 expected;
	int ll_mo = LL_MO(mo_success);
	int sc_mo = SC_MO(mo_success);

	expected = *exp;
	__asm__ volatile("" ::: "memory");
	do {
		/* Atomicity of LLD is not guaranteed */
		old = lld(var, ll_mo);
		/* Must write back neu or old to verify atomicity of LLD */
	} while (odp_unlikely(scd(var, old == expected ? neu : old, sc_mo)));
	*exp = old; /* Always update, atomically read value */
	return old == expected;
}

static inline __int128 __lockfree_exchange_16(__int128 *var, __int128 neu,
					      int mo)
{
	register __int128 old;
	int ll_mo = LL_MO(mo);
	int sc_mo = SC_MO(mo);

	do {
		/* Atomicity of LLD is not guaranteed */
		old = lld(var, ll_mo);
		/* Must successfully write back to verify atomicity of LLD */
	} while (odp_unlikely(scd(var, neu, sc_mo)));
	return old;
}

static inline __int128 __lockfree_fetch_and_16(__int128 *var, __int128 mask,
					       int mo)
{
	register __int128 old;
	int ll_mo = LL_MO(mo);
	int sc_mo = SC_MO(mo);

	do {
		/* Atomicity of LLD is not guaranteed */
		old = lld(var, ll_mo);
		/* Must successfully write back to verify atomicity of LLD */
	} while (odp_unlikely(scd(var, old & mask, sc_mo)));
	return old;
}

static inline __int128 __lockfree_fetch_or_16(__int128 *var, __int128 mask,
					      int mo)
{
	register __int128 old;
	int ll_mo = LL_MO(mo);
	int sc_mo = SC_MO(mo);

	do {
		/* Atomicity of LLD is not guaranteed */
		old = lld(var, ll_mo);
		/* Must successfully write back to verify atomicity of LLD */
	} while (odp_unlikely(scd(var, old | mask, sc_mo)));
	return old;
}

#else

static inline __int128_t cas_u128(__int128_t *ptr, __int128_t old_val,
				  __int128_t new_val, int mo)
{
	/* CASP instructions require that the first register number is paired */
	register uint64_t old0 __asm__ ("x0");
	register uint64_t old1 __asm__ ("x1");
	register uint64_t new0 __asm__ ("x2");
	register uint64_t new1 __asm__ ("x3");

	old0 = (uint64_t)old_val;
	old1 = (uint64_t)(old_val >> 64);
	new0 = (uint64_t)new_val;
	new1 = (uint64_t)(new_val >> 64);

	if (mo == __ATOMIC_RELAXED) {
		__asm__ volatile("casp %[old0], %[old1], %[new0], %[new1], [%[ptr]]"
				 : [old0] "+r" (old0), [old1] "+r" (old1)
				 : [new0] "r"  (new0), [new1] "r"  (new1), [ptr] "r" (ptr)
				 : "memory");
	} else if (mo == __ATOMIC_ACQUIRE) {
		__asm__ volatile("caspa %[old0], %[old1], %[new0], %[new1], [%[ptr]]"
				 : [old0] "+r" (old0), [old1] "+r" (old1)
				 : [new0] "r"  (new0), [new1] "r"  (new1), [ptr] "r" (ptr)
				 : "memory");
	} else if (mo == __ATOMIC_ACQ_REL) {
		__asm__ volatile("caspal %[old0], %[old1], %[new0], %[new1], [%[ptr]]"
				 : [old0] "+r" (old0), [old1] "+r" (old1)
				 : [new0] "r"  (new0), [new1] "r"  (new1), [ptr] "r" (ptr)
				 : "memory");
	} else if (mo == __ATOMIC_RELEASE) {
		__asm__ volatile("caspl %[old0], %[old1], %[new0], %[new1], [%[ptr]]"
				 : [old0] "+r" (old0), [old1] "+r" (old1)
				 : [new0] "r"  (new0), [new1] "r"  (new1), [ptr] "r" (ptr)
				 : "memory");
	} else {
		abort();
	}

	return ((__int128)old0) | (((__int128)old1) << 64);
}

static inline bool
__lockfree_compare_exchange_16(register __int128 *var, __int128 *exp,
			       register __int128 neu, bool weak, int mo_success,
			       int mo_failure)
{
	(void)weak;
	(void)mo_failure;
	__int128 old;
	__int128 expected;

	expected = *exp;
	old = cas_u128(var, expected, neu, mo_success);
	*exp = old; /* Always update, atomically read value */
	return old == expected;
}

static inline __int128 __lockfree_exchange_16(__int128 *var, __int128 neu,
					      int mo)
{
	__int128 old;
	__int128 expected;

	do {
		expected = *var;
		old = cas_u128(var, expected, neu, mo);
	} while (old != expected);
	return old;
}

static inline __int128 __lockfree_fetch_and_16(__int128 *var, __int128 mask,
					       int mo)
{
	__int128 old;
	__int128 expected;

	do {
		expected = *var;
		old = cas_u128(var, expected, expected & mask, mo);
	} while (old != expected);
	return old;
}

static inline __int128 __lockfree_fetch_or_16(__int128 *var, __int128 mask,
					      int mo)
{
	__int128 old;
	__int128 expected;

	do {
		expected = *var;
		old = cas_u128(var, expected, expected | mask, mo);
	} while (old != expected);
	return old;
}

#endif  /* __ARM_FEATURE_QRDMX */

static inline __int128 __lockfree_load_16(__int128 *var, int mo)
{
	__int128 old = *var; /* Possibly torn read */

	/* Do CAS to ensure atomicity
	 * Either CAS succeeds (writing back the same value)
	 * Or CAS fails and returns the old value (atomic read)
	 */
	(void)__lockfree_compare_exchange_16(var, &old, old, false, mo, mo);
	return old;
}

#ifdef _ODP_LOCK_FREE_128BIT_ATOMICS

/**
 * @internal
 * Helper macro for lockless atomic CAS operations on 128-bit integers
 * @param[in,out] atom Pointer to the 128-bit atomic variable
 * @param oper CAS operation
 * @param old_val Old value
 * @param new_val New value to be swapped
 * @return 1 for success and 0 for fail
 */
#define ATOMIC_CAS_OP_128(atom, oper, old_val, new_val, val) \
({ \
	odp_u128_t _val; \
	odp_atomic_u128_t *_atom = atom; \
	odp_u128_t *_old_val = old_val; \
	odp_u128_t _new_val = new_val; \
	odp_u128_t *ptr = (odp_u128_t *)(_atom); \
	register uint64_t old0 __asm__ ("x0"); \
	register uint64_t old1 __asm__ ("x1"); \
	register uint64_t new0 __asm__ ("x2"); \
	register uint64_t new1 __asm__ ("x3"); \
	old0 = (uint64_t)(_old_val)->u64[0]; \
	old1 = (uint64_t)(_old_val)->u64[1]; \
	new0 = (uint64_t)(_new_val).u64[0]; \
	new1 = (uint64_t)(_new_val).u64[1]; \
	__asm__ volatile(oper " %[old0], %[old1], %[new0], %[new1], [%[ptr]]" \
			: [old0] "+r" (old0), [old1] "+r" (old1) \
			: [new0] "r"  (new0), [new1] "r"  (new1), \
			[ptr] "r" (ptr) \
			: "memory"); \
	_val.u64[0] = old0; \
	_val.u64[1] = old1; \
	val = _val; \
})

#define ATOMIC_CAS_OP_128_NO_ORDER(atom, old_value, new_value, val) \
	ATOMIC_CAS_OP_128(atom, "casp", old_value, new_value, val)

#define ATOMIC_CAS_OP_128_ACQ(atom, old_value, new_value, val) \
	ATOMIC_CAS_OP_128(atom, "caspa", old_value, new_value, val)

#define ATOMIC_CAS_OP_128_REL(atom, old_value, new_value, val) \
	ATOMIC_CAS_OP_128(atom, "caspl", old_value, new_value, val)

#define ATOMIC_CAS_OP_128_ACQ_REL(atom, old_value, new_value, val) \
	ATOMIC_CAS_OP_128(atom, "caspal", old_value, new_value, val)

#endif

#endif  /* PLATFORM_LINUXGENERIC_ARCH_ARM_ODP_ATOMIC_H */
