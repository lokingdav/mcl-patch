/**
	@file
	@brief multi scalar multiplication with AVX-512 IFMA
	@author MITSUNARI Shigeo(@herumi)
	@license modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#define MCL_DLL_EXPORT
#include <mcl/bls12_381.hpp>
#include <stdint.h>
#include "avx512.hpp"
#include "msm.hpp"
#define MCL_GLV_ONLY_FUNC
#include "glv.hpp"

#define XBYAK_NO_EXCEPTION
#include "xbyak/xbyak_util.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized" // false positive
#pragma GCC diagnostic ignored "-Wignored-attributes" // false positive
#endif
#endif

//#define MCL_MSM_BLS12_377
#define USE_ASM

extern "C" {

void mcl_c5_vaddPre(Vec *, const Vec *, const Vec *);
void mcl_c5_vaddPreA(VecA *, const VecA *, const VecA *);

void mcl_c5_vsubPre(Vec *, const Vec *, const Vec *);
void mcl_c5_vsubPreA(VecA *, const VecA *, const VecA *);

void mcl_c5_vadd(Vec *, const Vec *, const Vec *);
void mcl_c5_vsub(Vec *, const Vec *, const Vec *);
void mcl_c5_vmul(Vec *, const Vec *, const Vec *);
void mcl_c5_vsqr(Vec *, const Vec *);
void mcl_c5_vaddA(VecA *, const VecA *, const VecA *);
void mcl_c5_vsubA(VecA *, const VecA *, const VecA *);
void mcl_c5_vmulA(VecA *, const VecA *, const VecA *);

}

namespace {

using namespace mcl;

// only for BLS12-381
struct FrA {
	uint64_t v[4];
};

struct FpA {
	uint64_t v[6];
};

struct G1A {
	uint64_t v[6*3];
};

static const size_t S = sizeof(Unit)*8-1; // 63
static const size_t W = 52;
static const size_t N = 8; // = ceil(384/52)
static const size_t M = sizeof(Vec) / sizeof(Unit);
#include "msm_avx_bls12_381.h"

inline Unit getMask(int w)
{
	if (w == 64) return Unit(-1);
	return (Unit(1) << w) - 1;
}

inline uint8_t cvtToInt(const Vmask& v)
{
	uint8_t r;
	memcpy(&r, &v, sizeof(r));
	return r;
}

inline bool isEqual(const Vec& a, const Vec& b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

inline bool isEqual(const VecA& a, const VecA& b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

inline void dump(const Vmask& v, const char *msg = nullptr)
{
	if (msg) printf("%s ", msg);
	uint64_t x = cvtToInt(v);
	for (size_t i = 0; i < 8; i++) {
		putchar('0' + ((x>>(7-i))&1));
	}
	putchar('\n');
}

inline void dump(const Vec& v, const char *msg = nullptr)
{
	mcl::bint::dump((const uint64_t*)&v, sizeof(v)/sizeof(uint64_t), msg);
}

inline void dump(const VecA& v, const char *msg = nullptr)
{
	if (msg) printf("%s\n", msg);
	for (size_t i = 0; i < 2; i++) {
		printf("%zd ", i);
		dump(v.v[i]);
	}
}

// set x[j] to i-th SIMD element of v[j]
inline void set(Vec v[N], size_t i, const Unit x[N])
{
	assert(i < M);
	Unit *p = (Unit *)v;
	for (size_t j = 0; j < N; j++) {
		p[j*M+i] = x[j];
	}
}

inline void get(Unit x[N], const Vec v[N], size_t i)
{
	assert(i < M);
	const Unit *p = (const Unit *)v;
	for (size_t j = 0; j < N; j++) {
		x[j] = p[j*M+i];
	}
}

inline void cvt(Vec yN[N], const Unit x[N*M])
{
	for (size_t i = 0; i < M; i++) {
		set(yN, i, x+i*N);
	}
}

inline void cvt(Unit y[N*M], const Vec xN[N])
{
	for (size_t i = 0; i < M; i++) {
		get(y+i*N, xN, i);
	}
}

/*
	d[] = transpose(s[]), destroy s[]
	s = [a7, a6, a5, a4, a3, a2, a1, a0]
	    [b7, b6, b5, b4, b3, b2, b1, b0]
	    [c7, c6, c5, c4, c3, c2, c1, c0]
	    ....
*/
template<bool out8=true>
inline void trans8x8(Vec d[8], Vec s[8])
{
	d[0] = vpunpcklqdq(s[0], s[1]);
	d[1] = vpunpckhqdq(s[0], s[1]);
	d[2] = vpunpcklqdq(s[2], s[3]);
	d[3] = vpunpckhqdq(s[2], s[3]);
	d[4] = vpunpcklqdq(s[4], s[5]);
	d[5] = vpunpckhqdq(s[4], s[5]);
	d[6] = vpunpcklqdq(s[6], s[7]);
	d[7] = vpunpckhqdq(s[6], s[7]);

	s[0] = vshuffi64x2<0x44>(d[0], d[2]);
	s[1] = vshuffi64x2<0x44>(d[1], d[3]);
	s[2] = vshuffi64x2<0x44>(d[4], d[6]);
	s[3] = vshuffi64x2<0x44>(d[5], d[7]);
	s[4] = vshuffi64x2<0xee>(d[0], d[2]);
	s[5] = vshuffi64x2<0xee>(d[1], d[3]);
	s[6] = vshuffi64x2<0xee>(d[4], d[6]);
	s[7] = vshuffi64x2<0xee>(d[5], d[7]);

	d[0] = vshuffi64x2<0x88>(s[0], s[2]);
	d[1] = vshuffi64x2<0x88>(s[1], s[3]);
	d[2] = vshuffi64x2<0xdd>(s[0], s[2]);
	d[3] = vshuffi64x2<0xdd>(s[1], s[3]);
	d[4] = vshuffi64x2<0x88>(s[4], s[6]);
	d[5] = vshuffi64x2<0x88>(s[5], s[7]);
	if (out8) {
		d[6] = vshuffi64x2<0xdd>(s[4], s[6]);
		d[7] = vshuffi64x2<0xdd>(s[5], s[7]);
	}
}

template<class V, class U>
inline void vaddPre(V *z, const V *x, const U *y)
{
	V t = vpaddq(x[0], y[0]);
	V c = vpsrlq(t, W);
	z[0] = vpandq(t, G::mask());

	for (size_t i = 1; i < N; i++) {
		t = vpaddq(x[i], y[i]);
		t = vpaddq(t, c);
		if (i == N-1) {
			z[i] = t;
			return;
		}
		c = vpsrlq(t, W);
		z[i] = vpandq(t, G::mask());
	}
}

template<class VM=Vmask, class V, class U>
inline VM vsubPre(V *z, const V *x, const U *y)
{
	V t = vpsubq(x[0], y[0]);
	V c = vpsrlq(t, S);
	z[0] = vpandq(t, G::mask());
	for (size_t i = 1; i < N; i++) {
		t = vpsubq(x[i], y[i]);
		t = vpsubq(t, c);
		c = vpsrlq(t, S);
		z[i] = vpandq(t, G::mask());
	}
	return vpcmpneqq(c, vzero());
}

template<class VM=Vmask, class V>
inline void uvselect(V *z, const VM& c, const V *a, const V *b)
{
	for (size_t i = 0; i < N; i++) {
		z[i] = vselect(c, a[i], b[i]);
	}
}

template<class VM=Vmask, class V>
inline void vadd(V *z, const V *x, const V *y)
{
	V sN[N], tN[N];
	vaddPre(sN, x, y);
	VM c = vsubPre<VM>(tN, sN, G::ap());
	uvselect(z, c, sN, tN);
}
#ifdef USE_ASM
template<>
inline void vadd(Vec *z, const Vec *x, const Vec *y)
{
	mcl_c5_vadd(z, x, y);
}
#endif
#ifdef USE_ASM
template<>
inline void vadd<VmaskA, VecA>(VecA *z, const VecA *x, const VecA *y)
{
	mcl_c5_vaddA(z, x, y);
}
#endif

template<class VM=Vmask, class V>
inline void vsub(V *z, const V *x, const V *y)
{
	V sN[N], tN[N];
	VM c = vsubPre<VM>(sN, x, y);
	vaddPre(tN, sN, G::ap());
	tN[N-1] = vpandq(tN[N-1], G::mask());
	uvselect(z, c, tN, sN);
}
#ifdef USE_ASM
template<>
inline void vsub(Vec *z, const Vec *x, const Vec *y)
{
	mcl_c5_vsub(z, x, y);
}
#endif
#ifdef USE_ASM
template<>
inline void vsub<VmaskA, VecA>(VecA *z, const VecA *x, const VecA *y)
{
	mcl_c5_vsubA(z, x, y);
}
#endif

template<class V>
inline void vmulUnit(V *z, const V *x, const V& y)
{
	V H;
	z[0] = vmulL(x[0], y);
	H = vmulH(x[0], y);
	for (size_t i = 1; i < N; i++) {
		z[i] = vmulL(x[i], y, H);
		H = vmulH(x[i], y);
	}
	z[N] = H;
}

template<class V, class U>
inline V vmulUnitAdd(V *z, const U *x, const V& y)
{
	V H;
#if 1
	V v = x[0];
	z[0] = vmulL(v, y, z[0]);
	H = vmulH(v, y, z[1]);
	for (size_t i = 1; i < N-1; i++) {
		v = x[i];
		z[i] = vmulL(v, y, H);
		H = vmulH(v, y, z[i+1]);
	}
	v = x[N-1];
	z[N-1] = vmulL(v, y, H);
	H = vmulH(v, y);
	return H;
#else
	z[0] = vmulL(x[0], y, z[0]);
	H = vmulH(x[0], y);
	for (size_t i = 1; i < N; i++) {
		z[i] = vpaddq(vmulL(x[i], y, H), z[i]);
		H = vmulH(x[i], y);
	}
	return H;
#endif
}

template<class V>
inline void vmulPre(V z[N*2], const V x[N], const V y[N])
{
	vmulUnit(z, x, y[0]);
	for (size_t i = 1; i < N; i++) {
		z[N+i] = vmulUnitAdd(z+i, x, y[i]);
	}
}

// t[N] = c ? a[N] : zero
template<class VM, class V>
inline void vset(V *t, const VM& c, const V a[N])
{
	for (size_t i = 0; i < N; i++) {
		t[i] = vselect(c, a[i], vzero());
	}
}

template<class VM=Vmask, class V>
inline void vmont(V z[N], V xy[N*2])
{
	for (size_t i = 0; i < N; i++) {
		V q = vmulL(xy[i], G::rp());
		xy[N+i] = vpaddq(xy[N+i], vmulUnitAdd(xy+i, G::ap(), q));
		xy[i+1] = vpaddq(xy[i+1], vpsrlq(xy[i], W));
	}
	for (size_t i = N; i < N*2-1; i++) {
		xy[i+1] = vpaddq(xy[i+1], vpsrlq(xy[i], W));
		xy[i] = vpandq(xy[i], G::mask());
	}
	VM c = vsubPre(z, xy+N, G::ap());
	uvselect(z, c, xy+N, z);
}

template<class V, class U = V>
inline V broadcast(const U& v);

template<>
inline Vec broadcast(const Vec& v) { return v; }
template<>
inline VecA broadcast(const VecA& v) { return v; }
template<>
inline Vec broadcast(const uint64_t& v) { return vpbroadcastq(v); }
template<>
inline VecA broadcast(const uint64_t& v)
{
	VecA r;
	r.v[0] = broadcast<Vec>(v);
	r.v[1] = broadcast<Vec>(v);
	return r;
}

template<class VM=Vmask, class V, typename U>
inline void vmul(V *z, const V *x, const U *y)
{
#if 0
	V xy[N*2];
	vmulPre(xy, x, y);
	vmont(z, xy);
#else
	V t[N*2], q;
	vmulUnit(t, x, broadcast<V>(y[0]));
	q = vmulL(t[0], G::rp());
	t[N] = vpaddq(t[N], vmulUnitAdd(t, G::ap(), q));
	for (size_t i = 1; i < N; i++) {
		t[N+i] = vmulUnitAdd(t+i, x, broadcast<V>(y[i]));
		t[i] = vpaddq(t[i], vpsrlq(t[i-1], W));
		q = vmulL(t[i], G::rp());
		t[N+i] = vpaddq(t[N+i], vmulUnitAdd(t+i, G::ap(), q));
	}
	for (size_t i = N; i < N*2; i++) {
		t[i] = vpaddq(t[i], vpsrlq(t[i-1], W));
		t[i-1] = vpandq(t[i-1], G::mask());
	}
	VM c = vsubPre<VM>(z, t+N, G::ap());
	uvselect(z, c, t+N, z);
#endif
}
#ifdef USE_ASM
template<>
inline void vmul(Vec *z, const Vec *x, const Vec *y)
{
	mcl_c5_vmul(z, x, y);
}
#endif
#ifdef USE_ASM
template<>
inline void vmul<VmaskA, VecA>(VecA *z, const VecA *x, const VecA *y)
{
	mcl_c5_vmulA(z, x, y);
}
#endif

template<class VM=Vmask, class V>
inline void vsqr(V *z, const V *x)
{
#if 0
	V t[N*2];
	vmulUnit(t, x, x[0]);
	V q = vmulL(t[0], G::rp());
	t[N] = vpaddq(t[N], vmulUnitAdd(t, G::ap(), q));
	for (size_t i = 1; i < N; i++) {
		t[N+i] = vmulUnitAdd(t+i, x, x[i]);
		t[i] = vpaddq(t[i], vpsrlq(t[i-1], W));
		q = vmulL(t[i], G::rp());
		t[N+i] = vpaddq(t[N+i], vmulUnitAdd(t+i, G::ap(), q));
	}
#else
	V t[N*2];
	t[0] = vmulL(x[0], x[0]);
	for (size_t i = 1; i < N; i++) {
		t[i*2-1] = vmulL(x[i], x[i-1]);
		t[i*2  ] = vmulH(x[i], x[i-1]);
	}
	for (size_t i = 2; i < N; i++) {
		for (size_t j = i; j < N; j++) {
			t[j*2-i  ] = vmulL(x[j], x[j-i], t[j*2-i  ]);
			t[j*2-i+1] = vmulH(x[j], x[j-i], t[j*2-i+1]);
		}
	}
	for (size_t i = 1; i < N*2-1; i++) {
		t[i] = vpaddq(t[i], t[i]);
	}
	for (size_t i = 1; i < N; i++) {
		t[i*2-1] = vmulH(x[i-1], x[i-1], t[i*2-1]);
		t[i*2] = vmulL(x[i], x[i], t[i*2]);
	}
	t[N*2-1] = vmulH(x[N-1], x[N-1]);

	for (size_t i = 0; i < N; i++) {
		if (i > 0) t[i] = vpaddq(t[i], vpsrlq(t[i-1], W));
		V q = vmulL(t[i], G::rp());
		t[N+i] = vpaddq(t[N+i], vmulUnitAdd(t+i, G::ap(), q));
	}
#endif
	for (size_t i = N; i < N*2; i++) {
		t[i] = vpaddq(t[i], vpsrlq(t[i-1], W));
		t[i-1] = vpandq(t[i-1], G::mask());
	}
	VM c = vsubPre<VM>(z, t+N, G::ap());
	uvselect(z, c, t+N, z);
}

#if 1
template<>
inline void vsqr(Vec *z, const Vec *x)
{
	mcl_c5_vsqr(z, x);
//	mcl_c5_vmul(z, x, x);
}
#endif

#if 1
template<>
inline void vsqr<VmaskA, VecA>(VecA *z, const VecA *x)
{
	mcl_c5_vmulA(z, x, x);
}
#endif

template<class V>
inline V getUnitAt(const V *x, size_t xN, size_t bitPos)
{
	const size_t bitSize = 64;
	const size_t q = bitPos / bitSize;
	const size_t r = bitPos % bitSize;
	if (r == 0) return x[q];
	if (q == xN - 1) return vpsrlq(x[q], r);
	return vporq(vpsrlq(x[q], r), vpsllq(x[q+1], bitSize - r));
}

/*
	 |64   |64   |64   |64   |64    |64   |
	x|52:12|40:24|28:36|16:48|4:52:8|44:20|
    y|52|52   |52   |52   |52  |52|52  |20|
*/
template<class V>
inline void split52bit(V y[8], const V x[6])
{
	assert(&y != &x);
#if 1
	const Vec m = vpbroadcastq(getMask(52));
	// and(or(A, B), C) = andCorAB = 0xa8
	const uint8_t imm = 0xA8;
	y[0] = vpandq(x[0], m);
	y[1] = vpternlogq<imm>(vpsrlq(x[0], 52), vpsllq(x[1], 12), m);
	y[2] = vpternlogq<imm>(vpsrlq(x[1], 40), vpsllq(x[2], 24), m);
	y[3] = vpternlogq<imm>(vpsrlq(x[2], 28), vpsllq(x[3], 36), m);
	y[4] = vpternlogq<imm>(vpsrlq(x[3], 16), vpsllq(x[4], 48), m);
	y[5] = vpandq(vpsrlq(x[4], 4), m);
	y[6] = vpternlogq<imm>(vpsrlq(x[4], 56), vpsllq(x[5], 8), m);
	y[7] = vpsrlq(x[5], 44);
#else
	y[0] = vpandq(x[0], G::mask());
	y[1] = vpandq(vporq(vpsrlq(x[0], 52), vpsllq(x[1], 12)), G::mask());
	y[2] = vpandq(vporq(vpsrlq(x[1], 40), vpsllq(x[2], 24)), G::mask());
	y[3] = vpandq(vporq(vpsrlq(x[2], 28), vpsllq(x[3], 36)), G::mask());
	y[4] = vpandq(vporq(vpsrlq(x[3], 16), vpsllq(x[4], 48)), G::mask());
	y[5] = vpandq(vpsrlq(x[4], 4), G::mask());
	y[6] = vpandq(vporq(vpsrlq(x[4], 56), vpsllq(x[5], 8)), G::mask());
	y[7] = vpsrlq(x[5], 44);
#endif
}

/*
	 |52|52   |52   |52   |52  |52|52  |20|
	x|52|12:40|24:28|36:16|48:4|52|8:44|20|
    y|64   |64   |64   |64   |64    |64
*/
template<class V>
inline void concat52bit(V y[6], const V x[8])
{
	assert(&y != &x);
	y[0] = vporq(x[0], vpsllq(x[1], 52));
	y[1] = vporq(vpsrlq(x[1], 12), vpsllq(x[2], 40));
	y[2] = vporq(vpsrlq(x[2], 24), vpsllq(x[3], 28));
	y[3] = vporq(vpsrlq(x[3], 36), vpsllq(x[4], 16));
#if 1
	// or(A, B, C) = orABC = 0xFE
	y[4] = vpternlogq<0xFE>(vpsrlq(x[4], 48), vpsllq(x[5], 4), vpsllq(x[6], 56));
#else
	y[4] = vporq(vporq(vpsrlq(x[4], 48), vpsllq(x[5], 4)), vpsllq(x[6], 56));
#endif
	y[5] = vporq(vpsrlq(x[6], 8), vpsllq(x[7], 44));
}

/*
	384bit = 6U (U=64)
	G1(=6U x 3(x, y, z)) x 8 => 8Ux8x3
*/
static CYBOZU_ALIGN(64) uint64_t g_pickUpEc[] = {
	18*0, 18*1, 18*2, 18*3, 18*4, 18*5, 18*6, 18*7,
//	18*8, 18*9, 18*10, 18*11, 18*12, 18*13, 18*14, 18*15,
};
static const Vec& v_pickUpEc = *(const Vec*)g_pickUpEc;
//static const VecA& v_pickUpEcA = *(const VecA*)g_pickUpEc;

// convert G1.x (, y or z) to Vec
inline void cvtFromG1Ax(Vec *y, const Unit *x)
{
#if 1
	Vec t[6];
	for (int i = 0; i < 6; i++) {
		t[i] = vpgatherqq(v_pickUpEc, x+i);
	}
#else // faster
	Vec s[8], t[8]; // need 8 size work area
	Vmask v = getMask(6);
	for (int i = 0; i < 8; i++) {
		s[i] = vmovdqu64(v, x+i*6*3);
	}
	trans8x8<false>(t, s);
#endif
	split52bit(y, t);
}

// convert G1.x (, y or z) to VecA
inline void cvtFromG1Ax(VecA *y, const Unit *x)
{
	VecA t[6];
	for (int i = 0; i < 6; i++) {
#if 1
		assert(vN == 2);
		t[i].v[0] = vpgatherqq(v_pickUpEc, x+i);
		t[i].v[1] = vpgatherqq(v_pickUpEc, x+i+18*8);
#else
		t[i] = vpgatherqq(v_pickUpEcA, x+i);
#endif
	}
	split52bit(y, t);
}

// EcM(=8Ux8x3) => G1(=6U x 3) x 8
// convert Vec to G1.x
inline void cvtToG1Ax(Unit *y, const Vec *x)
{
	Vec t[6];
	concat52bit(t, x);
	for (size_t i = 0; i < 6; i++) {
		vpscatterqq(y+i, v_pickUpEc, t[i]);
	}
}

inline void cvtToG1Ax(Unit *y, const VecA *x)
{
	VecA t[6];
	concat52bit(t, x);
	for (size_t i = 0; i < 6; i++) {
		vpscatterqq(y+i, v_pickUpEc, t[i].v[0]);
		vpscatterqq(y+i+18*8, v_pickUpEc, t[i].v[1]);
	}
}

static const CYBOZU_ALIGN(64) uint64_t g_pickUpFp[8] = {
	6*0, 6*1, 6*2, 6*3, 6*4, 6*5, 6*6, 6*7,
};
static const Vec& v_pickUpFp = *(const Vec*)g_pickUpFp;
// FpM(8Ux8) => Fp(=6U) x 8
inline void cvt8Ux8to6Ux8(Unit y[6*8], const Vec x[8])
{
	Vec t[6];
	concat52bit(t, x);
	for (size_t i = 0; i < 6; i++) {
		vpscatterqq(y+i, v_pickUpFp, t[i]);
	}
}
// Fp(=6U)x8 => FpM(8Ux8)
inline void cvt6Ux8to8Ux8Fp(Vec y[8], const Unit x[6*8])
{
	Vec t[6];
	for (int i = 0; i < 6; i++) {
		t[i] = vpgatherqq(v_pickUpFp, x+i);
	}
	split52bit(y, t);
}

template<class T, class _VM=Vmask, class _V=Vec>
struct FpMT {
	typedef _VM VM;
	typedef _V V;
	V v[N];
	static void add(T& z, const T& x, const T& y)
	{
		vadd<VM>(z.v, x.v, y.v);
	}
	static void mul2(T& z, const T& x)
	{
		add(z, x, x);
	}
	static void sub(T& z, const T& x, const T& y)
	{
		vsub<VM>(z.v, x.v, y.v);
	}
	static void neg(T& z, const T& x)
	{
		T::sub(z, T::zero(), x);
	}
	T neg() const
	{
		T t;
		T::sub(t, T::zero(), static_cast<const T&>(*this));
		return t;
	}
	static void mul(T& z, const T& x, const T& y)
	{
		vmul<VM>(z.v, x.v, y.v);
	}
	static void mul(T& z, const T& x, const uint64_t y[N])
	{
		vmul<VM>(z.v, x.v, y);
	}
	static void sqr(T& z, const T& x)
	{
		vsqr<VM>(z.v, x.v);
//		mul(z, x, x);
	}
	void toMont(T& x) const
	{
		mul(x, *this, T::R2());
	}
	void fromMont(const T &x)
	{
		mul(*this, x, T::rawOne());
	}
	void clear()
	{
		memset(this, 0, sizeof(*this));
	}
	bool operator==(const T& rhs) const
	{
		return memcmp(this, &rhs, sizeof(rhs)) == 0;
	}
	bool operator!=(const T& rhs) const { return !operator==(rhs); }
	VM isEqualAll(const T& rhs) const
	{
		V t = vpxorq(v[0], rhs.v[0]);
		for (size_t i = 1; i < M; i++) {
			t = vporq(t, vpxorq(v[i], rhs.v[i]));
		}
		return vpcmpeqq(t, vzero());
	}
	VM isZero() const
	{
		V t = v[0];
		for (size_t i = 1; i < M; i++) {
			t = vporq(t, v[i]);
		}
		return vpcmpeqq(t, vzero());
	}
	static void pow(T& z, const T& x, const V *y, size_t yn)
	{
		const int w = 4;
		assert(w == 4);
		const int tblN = 1<<w;
		T tbl[tblN];
		tbl[0] = T::one();
		tbl[1] = x;
		for (size_t i = 2; i < tblN; i++) {
			mul(tbl[i], tbl[i-1], x);
		}
		const size_t bitLen = sizeof(Unit)*8;
		const size_t jn = bitLen / w;
		z = tbl[0];
		const Vec mask4 = vpbroadcastq(getMask(w));
		for (size_t i = 0; i < yn; i++) {
			const Vec& v = y[yn-1-i];
			for (size_t j = 0; j < jn; j++) {
				for (int k = 0; k < w; k++) T::sqr(z, z);
				Vec idx = vpandq(vpsrlq(v, bitLen-w-j*w), mask4);
				idx = vpsllq(idx, 6); // 512 B = 64 Unit = 2^6
				idx = vpaddq(idx, T::offset());
				T t;
				for (size_t k = 0; k < N; k++) {
					t.v[k] = vpgatherqq(idx, &tbl[0].v[k]);
				}
				mul(z, z, t);
			}
		}
	}
	// condition set (set x if c)
	void cset(const VM& c, const T& x)
	{
		for (size_t i = 0; i < N; i++) {
			v[i] = vselect(c, x.v[i], v[i]);
		}
	}
	// return c ? a : b;
	static T select(const VM& c, const T& a, const T& b)
	{
#if 0 // faster on gcc, same on clang
		const V mask = ::vselect(c, broadcast(uint64_t(-1)), broadcast(0));
		T d;
		for (size_t i = 0; i < N; i++) {
			d.v[i] = vporq(vpandq(mask, a.v[i]), vpandnq(mask, b.v[i]));
		}
		return d;
#else
		T d;
		for (size_t i = 0; i < N; i++) {
			d.v[i] = vselect(c, a.v[i], b.v[i]);
		}
		return d;
#endif
	}
};

struct FpM : FpMT<FpM, Vmask, Vec> {
	static const Vec& offset() { return *(const Vec*)g_offset_; }
	static const FpM& zero() { return *(const FpM*)g_zero_; }
	static const FpM& one() { return *(const FpM*)g_R_; }
	static const FpM& R2() { return *(const FpM*)g_R2_; }
	static const FpM& rawOne() { return *(const FpM*)g_rawOne_; }
	static const FpM& m64to52() { return *(const FpM*)g_m64to52_; }
	static const FpM& m52to64() { return *(const FpM*)g_m52to64_; }
	static const FpM& rw() { return *(const FpM*)g_rw_; }
	void setFpA(const FpA v[M])
	{
		cvt6Ux8to8Ux8Fp(this->v, v[0].v);
//		FpM::mul(*this, *this, FpM::m64to52());
		FpM::mul(*this, *this, g_m64to52u_);
	}
	void getFpA(FpA v[M]) const
	{
		FpM t;
//		FpM::mul(t, *this, FpM::m52to64());
		FpM::mul(t, *this, g_m52to64u_);
		cvt8Ux8to6Ux8((Unit*)v, t.v);
	}
	static void inv(FpM& z, const FpM& x)
	{
		CYBOZU_ALIGN(64) FpA v[M];
		mcl::Fp* vv = (mcl::Fp*)v;
		x.getFpA(v);
		mcl::invVec<mcl::Fp>(vv, vv, M, M);
		z.setFpA(v);
	}
#ifdef MCL_MSM_TEST
	void set(const mpz_class& x, size_t i);
	void set(const mpz_class& x);
	mpz_class getRaw(size_t i) const;
	mpz_class get(size_t i) const;
#endif
};

// set y = 1 if isProj
template<class E>
inline void normalizeJacobiVec(E *P, size_t n, bool isProj = false)
{
	typedef typename E::Fp F;
	bool alocated = false;
	F *tbl = 0;
#if 0
	if (sizeof(F) * n < 1024 * 1024) {
		tbl = (F*)CYBOZU_ALIGNED_ALLOCA(sizeof(F) * n, 64);
	} else
#endif
	{
		tbl = (F*)Xbyak::AlignedMalloc(sizeof(F) * n, 64);
		alocated = true;
	}
	tbl[0] = F::select(P[0].z.isZero(), F::one(), P[0].z);
	for (size_t i = 1; i < n; i++) {
		F t = F::select(P[i].z.isZero(), F::one(), P[i].z);
		F::mul(tbl[i], tbl[i-1], t);
	}
	CYBOZU_ALIGN(64) F r;
	F::inv(r, tbl[n-1]);
	for (size_t i = 0; i < n; i++) {
		size_t pos = n-1-i;
		F& z = P[pos].z;
		const typename E::VM zIsZero = z.isZero();
		F rz, rz2;
		if (pos == 0) {
			rz = r;
		} else {
			F::mul(rz, r, tbl[pos-1]);
			F::mul(r, r, F::select(zIsZero, F::one(), z));
		}
		F::sqr(rz2, rz);
		F::mul(P[pos].x, P[pos].x, rz2); // xz^-2
		F::mul(rz2, rz2, rz);
		F::mul(P[pos].y, P[pos].y, rz2); // yz^-3
		z = F::select(zIsZero, z, F::one());
		if (isProj) P[pos].y = F::select(zIsZero, F::one(), P[pos].y);
	}
	if (alocated) {
		Xbyak::AlignedFree(tbl);
	}
}

// 8M+3S+7A
// assume P.x != Q.x, P != Q
// asseume all Q are normalized
template<class E>
inline void addJacobiMixedNoCheck(E& R, const E& P, const E& Q)
{
	typedef typename E::Fp F;
	F r, U1, S1, H, H3;
	F::sqr(r, P.z);
	U1 = P.x;
	F::mul(H, Q.x, r);
	F::sub(H, H, U1);
	S1 = P.y;
	F::mul(r, r, P.z);
	F::mul(r, r, Q.y);
	F::sub(r, r, S1);
	F::mul(R.z, P.z, H);
	F::sqr(H3, H); // H^2
	F::sqr(R.y, r); // r^2
	F::mul(U1, U1, H3); // U1 H^2
	F::mul(H3, H3, H); // H^3
	F::sub(R.y, R.y, U1);
	F::sub(R.y, R.y, U1);
	F::sub(R.x, R.y, H3);
	F::sub(U1, U1, R.x);
	F::mul(U1, U1, r);
	F::mul(H3, H3, S1);
	F::sub(R.y, U1, H3);
}

// 12M+4S+7A
// P == Q or P == -Q then R = 0, so assume P != Q.
template<class E>
inline void addJacobiNoCheck(E& R, const E& P, const E& Q)
{
	typedef typename E::Fp F;
	F r, U1, S1, H, H3;
	F::sqr(r, P.z);
	F::sqr(S1, Q.z);
	F::mul(U1, P.x, S1);
	F::mul(H, Q.x, r);
	F::sub(H, H, U1);
	F::mul(S1, S1, Q.z);
	F::mul(S1, S1, P.y);
	F::mul(r, r, P.z);
	F::mul(r, r, Q.y);
	F::sub(r, r, S1);
	F::mul(R.z, P.z, Q.z);
	F::mul(R.z, R.z, H);
	F::sqr(H3, H); // H^2
	F::sqr(R.y, r); // r^2
	F::mul(U1, U1, H3); // U1 H^2
	F::mul(H3, H3, H); // H^3
	F::sub(R.y, R.y, U1);
	F::sub(R.y, R.y, U1);
	F::sub(R.x, R.y, H3);
	F::sub(U1, U1, R.x);
	F::mul(U1, U1, r);
	F::mul(H3, H3, S1);
	F::sub(R.y, U1, H3);
}

// assume a = 0
// 3M+4S+12A
template<class E>
inline void dblJacobiNoCheck(E& R, const E& P)
{
	typedef typename E::Fp F;
	F x2, y2, xy, t;
	F::sqr(x2, P.x);
	F::sqr(y2, P.y);
	F::mul(xy, P.x, y2);
	F::mul2(xy, xy);
	F::sqr(y2, y2);
	F::mul2(xy, xy); // 4xy^2
	F::mul2(t, x2);
	F::add(x2, x2, t);
	F::sqr(R.x, x2);
	F::sub(R.x, R.x, xy);
	F::sub(R.x, R.x, xy);
	F::mul(R.z, P.y, P.z);
	F::mul2(R.z, R.z);
	F::sub(R.y, xy, R.x);
	F::mul(R.y, R.y, x2);
	F::mul2(y2, y2);
	F::mul2(y2, y2);
	F::mul2(y2, y2);
	F::sub(R.y, R.y, y2);
}

template<class T, class F>
struct EcMT {
	typedef F Fp;
	typedef typename F::V V;
	typedef typename F::VM VM;
	F x, y, z;
	static const int a_ = 0;
#ifdef MCL_MSM_BLS12_377
	static const int b_ = 1;
	static const int specialB_ = mcl::ec::local::Plus1;
#else
	static const int b_ = 4;
	static const int specialB_ = mcl::ec::local::Plus4;
#endif
	static T select(const VM& c, const T& a, const T& b)
	{
		T d;
		d.x = F::select(c, a.x, b.x);
		d.y = F::select(c, a.y, b.y);
		d.z = F::select(c, a.z, b.z);
		return d;
	}
	template<bool isProj=true, bool mixed=false>
	static void add(T& z, const T& x, const T& y)
	{
		if (isProj) {
			if (mixed) {
				T t;
				mcl::ec::addCTProj(t, x, y, mixed);
				z = select(y.isZero(), x, t);
			} else {
				mcl::ec::addCTProj(z, x, y);
			}
		} else {
			T t;
			if (mixed) {
				addJacobiMixedNoCheck(t, x, y);
			} else {
				addJacobiNoCheck(t, x, y);
			}
			t = select(x.isZero(), y, t);
			z = select(y.isZero(), x, t);
		}
	}
	template<bool isProj=true>
	static void dbl(T& z, const T& x)
	{
		if (isProj) {
			mcl::ec::dblCTProj(z, x);
		} else {
			dblJacobiNoCheck(z, x);
		}
	}
	template<bool isProj=true>
	static const T& zero()
	{
		return isProj ? T::zeroProj_ : T::zeroJacobi_;
	}
	template<bool isProj=true>
	void clear()
	{
		*this = zero<isProj>();
	}
	void normalize()
	{
		// assume !isZero()
		F r;
		F::inv(r, z);
		F::mul(x, x, r);
		F::mul(y, y, r);
		z = F::one();
	}
	template<bool isProj=true, bool mixed=false>
	static void makeTable(T *tbl, size_t tblN, const T& P)
	{
		tbl[0].template clear<isProj>();
		tbl[1] = P;
		dbl<isProj>(tbl[2], P);
		for (size_t i = 3; i < tblN; i++) {
			if (i & 1) {
				add<isProj, mixed>(tbl[i], tbl[i-1], P);
			} else {
				dbl<isProj>(tbl[i], tbl[i/2]);
			}
		}
	}
	void gather(const T *tbl, V idx)
	{
		const Vec factor = vpbroadcastq(3 * sizeof(V));
		idx = vmulL(idx, factor, F::offset());
		for (size_t i = 0; i < N; i++) {
			x.v[i] = vpgatherqq(idx, &tbl[0].x.v[i]);
			y.v[i] = vpgatherqq(idx, &tbl[0].y.v[i]);
			z.v[i] = vpgatherqq(idx, &tbl[0].z.v[i]);
		}
	}
	void scatter(T *tbl, V idx) const
	{
		const Vec factor = vpbroadcastq(3 * sizeof(V));
		idx = vmulL(idx, factor, F::offset());
		for (size_t i = 0; i < N; i++) {
			vpscatterqq(&tbl[0].x.v[i], idx, x.v[i]);
			vpscatterqq(&tbl[0].y.v[i], idx, y.v[i]);
			vpscatterqq(&tbl[0].z.v[i], idx, z.v[i]);
		}
	}
	static void mulLambda(T& Q, const T& P)
	{
		F::mul(Q.x, P.x, F::rw());
		Q.y = P.y;
		Q.z = P.z;
	}
	static void neg(T& Q, const T& P)
	{
		Q.x = P.x;
		F::neg(Q.y, P.y);
		Q.z = P.z;
	}
	void cset(const VM& c, const T& v)
	{
		x.cset(c, v.x);
		y.cset(c, v.y);
		z.cset(c, v.z);
	}
	VM isZero() const
	{
		return z.isZero();
	}
	VM isEqualJacobiAll(const T& rhs) const
	{
		F s1, s2, t1, t2;
		VM v1, v2;
		F::sqr(s1, z);
		F::sqr(s2, rhs.z);
		F::mul(t1, x, s2);
		F::mul(t2, rhs.x, s1);
		v1 = t1.isEqualAll(t2);
		F::mul(t1, y, s2);
		F::mul(t2, rhs.y, s1);
		F::mul(t1, t1, rhs.z);
		F::mul(t2, t2, z);
		v2 = t1.isEqualAll(t2);
		return kandb(v1, v2);
	}
	template<size_t bitLen, size_t w>
	static void makeNAFtbl(V *idxTbl, VM *negTbl, const V a[2])
	{
		const Vec mask = vpbroadcastq((1<<w)-1);
		const Vec Fu = vpbroadcastq(1<<w);
		const Vec H = vpbroadcastq(1<<(w-1));
		const Vec one = vpbroadcastq(1);
		size_t pos = 0;
		V CF = vzero<V>();
		const size_t n = (bitLen+w-1)/w;
		for (size_t i = 0; i < n; i++) {
			V idx = getUnitAt(a, 2, pos);
			idx = vpandq(idx, mask);
			idx = vpaddq(idx, CF);
			V masked = vpandq(idx, mask);
			negTbl[i] = vpcmpgtq(masked, H);
			idxTbl[i] = vselect(negTbl[i], vpsubq(Fu, masked), masked); // idx >= H ? F - idx : idx;
			CF = vpsrlq(idx, w);
			CF = vpaddq(negTbl[i], CF, one);
			pos += w;
		}
	}
	template<bool isProj=true, bool mixed=false>
	static void mulGLV(T *Q, const T *P, const FrA *y, size_t n)
	{
		assert(n > 0);
		const size_t m = sizeof(V)/8;
		const size_t w = 5;
		const size_t tblN = (1<<(w-1))+1; // [0, 2^(w-1)]

#ifdef __GNUC__
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvla-cxx-extension"
#endif
		CYBOZU_ALIGN(64) T tbl1s[tblN*n];
#ifdef __clang__
#pragma GCC diagnostic pop
#endif
#else
		T *tbl1s = (T*)CYBOZU_ALIGNED_ALLOCA(sizeof(T)*tblN*n, 64);
#endif
		for (size_t k = 0; k < n; k++) {
			makeTable<isProj, mixed>(tbl1s + tblN*k, tblN, P[k]);
		}
		if (!isProj && mixed) normalizeJacobiVec<T>(tbl1s+1, tblN*n-1);

		const mcl::fp::Op& fr = mcl::Fr::getOp();
		for (size_t k = 0; k < n; k++) {
			const T *tbl1 = &tbl1s[tblN*k];
			T tbl2[tblN];
			for (size_t i = 0; i < tblN; i++) {
				mulLambda(tbl2[i], tbl1[i]);
			}
			V a[2], b[2];
			Unit *pa = (Unit*)a;
			Unit *pb = (Unit*)b;
			for (size_t i = 0; i < m; i++) {
				Unit buf[4];
				fr.fromMont(buf, y[k*m+i].v);
				Unit aa[2], bb[2];
#ifdef MCL_MSM_BLS12_377
				mcl::ec::optimizedSplitRawForBLS12_377(aa, bb, buf);
#else
				mcl::ec::optimizedSplitRawForBLS12_381(aa, bb, buf);
#endif
				pa[i+m*0] = aa[0]; pa[i+m*1] = aa[1];
				pb[i+m*0] = bb[0]; pb[i+m*1] = bb[1];
			}
			const size_t bitLen = 128;
			const size_t nw = (bitLen + w-1)/w;
			V aTbl[nw], bTbl[nw];
			VM aNegTbl[nw], bNegTbl[nw];
			makeNAFtbl<bitLen, w>(aTbl, aNegTbl, a);
			makeNAFtbl<bitLen, w>(bTbl, bNegTbl, b);

			for (size_t i = 0; i < nw; i++) {
				if (i > 0) for (size_t j = 0; j < w; j++) T::template dbl<isProj>(Q[k], Q[k]);
				const size_t pos = nw-1-i;

				T t;
				V idx = bTbl[pos];
				t.gather(tbl2, idx);
				t.y = F::select(bNegTbl[pos], t.y.neg(), t.y);
				if (i == 0) {
					Q[k] = t;
				} else {
					add<isProj, mixed>(Q[k], Q[k], t);
				}
				idx = aTbl[pos];
				t.gather(tbl1, idx);
				t.y = F::select(aNegTbl[pos], t.y.neg(), t.y);
				add<isProj, mixed>(Q[k], Q[k], t);
			}
		}
	}
};

struct EcM : EcMT<EcM, FpM> {
	static const FpM &b3_;
	static const EcM &zeroProj_;
	static const EcM &zeroJacobi_;
	template<bool isNormalized = false>
	void setG1A(const G1A v[M], bool JacobiToProj = true)
	{
		cvtFromG1Ax(x.v, v[0].v+0*6);
		cvtFromG1Ax(y.v, v[0].v+1*6);
		cvtFromG1Ax(z.v, v[0].v+2*6);

		FpM::mul(x, x, g_m64to52u_);
		FpM::mul(y, y, g_m64to52u_);
		FpM::mul(z, z, g_m64to52u_);

		if (JacobiToProj) {
			if (!isNormalized) mcl::ec::JacobiToProj(*this, *this);
			y = FpM::select(z.isZero(), FpM::one(), y);
		}
	}
	void getG1A(G1A v[M], bool ProjToJacobi = true) const
	{
		EcM T = *this;
		if (ProjToJacobi) mcl::ec::ProjToJacobi(T, T);

		FpM::mul(T.x, T.x, g_m52to64u_);
		FpM::mul(T.y, T.y, g_m52to64u_);
		FpM::mul(T.z, T.z, g_m52to64u_);

		cvtToG1Ax(v[0].v+0*6, T.x.v);
		cvtToG1Ax(v[0].v+1*6, T.y.v);
		cvtToG1Ax(v[0].v+2*6, T.z.v);
	}
#if 0
	// Treat idx as an unsigned integer
	// 33.6M clk
	template<bool isProj=true, bool mixed=false>
	static void mulGLV(EcM& Q, const EcM& P, const Vec y[4])
	{
		const size_t w = 4;
		const size_t tblN = 1<<w;
		// QQQ (n=1024) isProj=T : 36.8, isProj=F&&mixed=F : 36.0, isProj=F&&mixed=T : 34.6
		Vec a[2], b[2];
		EcM tbl1[tblN], tbl2[tblN];
		makeTable<isProj, mixed>(tbl1, tblN, P);
		if (!isProj && mixed) normalizeJacobiVec<EcM, tblN-1>(tbl1+1);
		for (size_t i = 0; i < tblN; i++) {
			mulLambda(tbl2[i], tbl1[i]);
		}
		const Unit *src = (const Unit*)y;
		Unit *pa = (Unit*)a;
		Unit *pb = (Unit*)b;
		for (size_t i = 0; i < M; i++) {
			Unit buf[4] = { src[i+M*0], src[i+M*1], src[i+M*2], src[i+M*3] };
			Unit aa[2], bb[2];
			mcl::ec::local::optimizedSplitRawForBLS12_381(aa, bb, buf);
			pa[i+M*0] = aa[0]; pa[i+M*1] = aa[1];
			pb[i+M*0] = bb[0]; pb[i+M*1] = bb[1];
		}
		const size_t bitLen = 128;
		Vec mask = vpbroadcastq((1<<w)-1);
		bool first = true;
		size_t pos = bitLen;
		for (size_t i = 0; i < (bitLen + w-1)/w; i++) {
			size_t dblN = w;
			if (pos < w) {
				mask = vpbroadcastq((1<<pos)-1);
				dblN = pos;
				pos = 0;
			} else {
				pos -= w;
			}
			if (!first) for (size_t k = 0; k < dblN; k++) EcM::dbl<isProj>(Q, Q);
			EcM T;
			Vec idx;
			idx = vpandq(getUnitAt(b, 2, pos), mask);
			if (first) {
				Q.gather(tbl2, idx);
				first = false;
			} else {
				T.gather(tbl2, idx);
				add<isProj, mixed>(Q, Q, T);
			}
			idx = vpandq(getUnitAt(a, 2, pos), mask);
			T.gather(tbl1, idx);
			add<isProj, mixed>(Q, Q, T);
		}
	}
#else
#endif
};

const FpM& EcM::b3_ = *(const FpM*)g_b3_;
const EcM& EcM::zeroProj_ = *(const EcM*)g_zeroProj_;
const EcM& EcM::zeroJacobi_ = *(const EcM*)g_zeroJacobi_;

template<class G>
inline void reduceSum(G1A& Q, const G& P)
{
	const size_t m = sizeof(P)/sizeof(EcM)*8;
	G1A z[m];
	P.getG1A(z);
	Q = z[0];
	for (size_t i = 1; i < m; i++) {
		mcl::G1::add((mcl::G1&)Q, (const mcl::G1&)Q, (const mcl::G1&)z[i]);
	}
}

template<class G, class V, bool mixed=false>
void mulVecUpdateTable(G& win, G *tbl, size_t tblN, const G *xVec, const V *yVec, size_t yn, size_t pos, size_t n, bool first)
{
	const bool isProj = true;
	const Vec m = vpbroadcastq(tblN-1);
	for (size_t i = 0; i < tblN; i++) {
		tbl[i].clear();
	}
	for (size_t i = 0; i < n; i++) {
		V v = getUnitAt(yVec+i*yn, yn, pos);
		v = vpandq(v, m);
		G T;
		T.gather(tbl, v);
		G::template add<isProj, mixed>(T, T, xVec[i]);
		T.scatter(tbl, v);
	}
	G sum = tbl[tblN - 1];
	if (first) {
		win = sum;
	} else {
		G::add(win, win, sum);
	}
	for (size_t i = 1; i < tblN - 1; i++) {
		G::add(sum, sum, tbl[tblN - 1- i]);
		G::add(win, win, sum);
	}
}

inline size_t glvGetBucketSizeAVX512(size_t n)
{
	size_t log2n = mcl::ec::ilog2(n);
	const size_t tblMin = 6;
	if (log2n < tblMin) return 2;
	// n >= 2^tblMin
	static const size_t tbl[] = {
	// elem num 2^a i          : a= 16  17  18  19  20  21
	// simd elem num 2^b=2^a/4 : b= 14  15  16  17  18  19
		3, 4, 5, 5, 6, 7, 8, 8, 10, 10, 10, 10, 10, 13, 15, 15, 16, 16, 16, 16, 16
		                                      //13 (almost same)
	};
	if (log2n >= CYBOZU_NUM_OF_ARRAY(tbl)) return 16;
	size_t ret = tbl[log2n - tblMin];
	return ret;
}
// xVec[n], yVec[n * maxBitSize/64]
template<class G=EcM, class V=Vec, bool mixed = false>
inline void mulVecAVX512_inner(G1A& P, const G *xVec, const V *yVec, size_t n, size_t maxBitSize, size_t b)
{
	if (b == 0) b = glvGetBucketSizeAVX512(n);
	size_t tblN = size_t(1) << b;
	G *tbl = (G*)Xbyak::AlignedMalloc(sizeof(G) * tblN, 64);
	const size_t yn = maxBitSize / 64;
	const size_t winN = (maxBitSize + b-1) / b;

	G T;
	mulVecUpdateTable<G, V, mixed>(T, tbl, tblN, xVec, yVec, yn, b*(winN-1), n, true);
	for (size_t w = 1; w < winN; w++) {
		for (size_t i = 0; i < b; i++) {
			G::dbl(T, T);
		}
		mulVecUpdateTable<G, V, mixed>(T, tbl, tblN, xVec, yVec, yn, b*(winN-1-w), n, false);
	}
	reduceSum(P, T);
	Xbyak::AlignedFree(tbl);
}

struct FpMA : FpMT<FpMA, VmaskA, VecA> {
	static const VecA& offset() { return *(const VecA*)g_offset_; }
	static const FpMA& zero() { return *(const FpMA*)g_zeroA_; }
	static const FpMA& one() { return *(const FpMA*)g_RA_; }
	static const FpMA& R2() { return *(const FpMA*)g_R2A_; }
	static const FpMA& rawOne() { return *(const FpMA*)g_rawOneA_; }
	static const FpMA& m64to52() { return *(const FpMA*)g_m64to52A_; }
	static const FpMA& m52to64() { return *(const FpMA*)g_m52to64A_; }
	static const FpMA& rw() { return *(const FpMA*)g_rwA_; }
	void setFpA(const FpA v[M*vN])
	{
		FpM t[vN];
		for (size_t i = 0; i < vN; i++) {
			t[i].setFpA(v+M*i);
		}
		setFpM(t);
	}
	void getFpA(FpA v[M*vN]) const
	{
		FpM t[vN];
		getFpM(t);
		for (size_t i = 0; i < vN; i++) {
			t[i].getFpA(v+M*i);
		}
	}
	static void inv(FpMA& z, const FpMA& x)
	{
		CYBOZU_ALIGN(64) FpA v[M*vN];
		mcl::Fp* vv = (mcl::Fp*)v;
		x.getFpA(v);
		mcl::invVec<mcl::Fp>(vv, vv, M*vN, M*vN);
		z.setFpA(v);
	}
	void setFpM(const FpM x[vN])
	{
		for (size_t i = 0; i < vN; i++) {
			for (size_t j = 0; j < N; j++) {
				v[j].v[i] = x[i].v[j];
			}
		}
	}
	void getFpM(FpM y[vN]) const
	{
		for (size_t i = 0; i < vN; i++) {
			for (size_t j = 0; j < N; j++) {
				y[i].v[j] = v[j].v[i];
			}
		}
	}
};

void cvtFpM2FpMA(FpMA& z, const FpM& x, const FpM& y)
{
	assert(vN == 2);
	for (size_t i = 0; i < N; i++) {
		z.v[i].v[0] = x.v[i];
		z.v[i].v[1] = y.v[i];
	}
}

void cvtFpMA2FpM(FpM& a, FpM& b, const FpMA& c)
{
	assert(vN == 2);
	for (size_t i = 0; i < N; i++) {
		a.v[i] = c.v[i].v[0];
		b.v[i] = c.v[i].v[1];
	}
}

struct EcMA : EcMT<EcMA, FpMA> {
	static const FpMA &b3_;
	static const EcMA &zeroProj_;
	static const EcMA &zeroJacobi_;
	void setEcM(const EcM P[vN])
	{
		cvtFpM2FpMA(x, P[0].x, P[1].x);
		cvtFpM2FpMA(y, P[0].y, P[1].y);
		cvtFpM2FpMA(z, P[0].z, P[1].z);
	}
	void getEcM(EcM P[vN]) const
	{
		cvtFpMA2FpM(P[0].x, P[1].x, x);
		cvtFpMA2FpM(P[0].y, P[1].y, y);
		cvtFpMA2FpM(P[0].z, P[1].z, z);
	}

	template<bool isNormalized = false>
	void setG1A(const G1A v[M*vN], bool JacobiToProj = true)
	{
#if 1
		assert(vN == 2);

		cvtFromG1Ax(x.v, v[0].v+0*6);
		cvtFromG1Ax(y.v, v[0].v+1*6);
		cvtFromG1Ax(z.v, v[0].v+2*6);

		FpMA::mul(x, x, g_m64to52u_);
		FpMA::mul(y, y, g_m64to52u_);
		FpMA::mul(z, z, g_m64to52u_);

		if (JacobiToProj) {
			// Jacobi = Proj if normalized
			if (!isNormalized) mcl::ec::JacobiToProj(*this, *this);
			y = FpMA::select(z.isZero(), FpMA::one(), y);
		}
#else
		EcM P[vN];
		for (size_t i = 0; i < vN; i++) {
			P[i].setG1A(v+i*M, JacobiToProj);
		}
		setEcM(P);
#endif
	}
	void getG1A(G1A v[M*vN], bool ProjToJacobi = true) const
	{
#if 1
		EcMA T = *this;
		if (ProjToJacobi) mcl::ec::ProjToJacobi(T, T);

		FpMA::mul(T.x, T.x, g_m52to64u_);
		FpMA::mul(T.y, T.y, g_m52to64u_);
		FpMA::mul(T.z, T.z, g_m52to64u_);

		cvtToG1Ax(v[0].v+0*6, T.x.v);
		cvtToG1Ax(v[0].v+1*6, T.y.v);
		cvtToG1Ax(v[0].v+2*6, T.z.v);
#else
		EcM P[vN];
		getEcM(P);
		for (size_t i = 0; i < vN; i++) {
			P[i].getG1A(v+i*M, ProjToJacobi);
		}
#endif
	}
};

const FpMA& EcMA::b3_ = *(const FpMA*)g_b3A_;
const EcMA& EcMA::zeroProj_ = *(const EcMA*)g_zeroProjA_;
const EcMA& EcMA::zeroJacobi_ = *(const EcMA*)g_zeroJacobiA_;

#define USE_GLV

template<class G=EcM, class V=Vec>
void mulVecAVX512T(G1& _P, G1 *_x, const Fr *_y, size_t n, size_t bucket = 0)
{
	G1A& P = *(G1A*)&_P;
	G1A *x = (G1A*)_x;
	const FrA *y = (const FrA*)_y;
	const size_t m = sizeof(V)/8;
	const size_t d = n/m;
#ifdef USE_GLV
	const size_t e = 2;
#else
	const size_t e = 1;
#endif
	const bool mixed = true;
	if (mixed) {
// 		ec::normalizeVec<G1>(x, x, n);
	}

	G *xVec = (G*)Xbyak::AlignedMalloc(sizeof(G) * d * e, 64);
	V *yVec = (V*)Xbyak::AlignedMalloc(sizeof(V) * d * 4, 64);

	for (size_t i = 0; i < d; i++) {
		xVec[i].template setG1A<mixed>(x+i*m);
	}
	normalizeJacobiVec(xVec, d, true);
#ifdef USE_GLV
	for (size_t i = 0; i < d; i++) {
		G::mulLambda(xVec[d+i], xVec[i]);
	}

	Unit *const py = (Unit*)yVec;
	Unit *const py2 = py + d*m*2;
	for (size_t i = 0; i < d; i++) {
		for (size_t j = 0; j < m; j++) {
			Unit ya[4];
			mcl::Fr::getOp().fromMont(ya, y[i*m+j].v);
			Unit a[2], b[2];
#ifdef MCL_MSM_BLS12_377
			mcl::ec::optimizedSplitRawForBLS12_377(a, b, ya);
#else
			mcl::ec::optimizedSplitRawForBLS12_381(a, b, ya);
#endif
			py[i*m*2+j+0] = a[0];
			py[i*m*2+j+m] = a[1];
			py2[i*m*2+j+0] = b[0];
			py2[i*m*2+j+m] = b[1];
		}
	}
#else
	const mcl::fp::Op& fr = mcl::Fr::getOp();
	Unit *const py = (Unit*)yVec;
	for (size_t i = 0; i < d; i++) {
		for (size_t j = 0; j < m; j++) {
			Unit ya[4];
			fr.fromMont(ya, y[i*m+j].v);
			for (size_t k = 0; k < 4; k++) {
				py[(i*4+k)*m+j] = ya[k];
			}
		}
	}
#endif
	mulVecAVX512_inner<G, V, mixed>(P, xVec, yVec, d * e, 256 / e, bucket);

	Xbyak::AlignedFree(yVec);
	Xbyak::AlignedFree(xVec);

	for (size_t i = d*m; i < n; i++) {
		G1A Q;
		mcl::G1::mul((mcl::G1&)Q, (const mcl::G1&)x[i], (const mcl::Fr&)y[i]);
		mcl::G1::add((mcl::G1&)P, (const mcl::G1&)P, (const mcl::G1&)Q);
	}
}

} // namespace

namespace mcl { namespace msm {

void mulVecAVX512(G1& P, G1 *x, const Fr *y, size_t n, size_t b = 0)
{
	mulVecAVX512T<EcM, Vec>(P, x, y, n, b);
//	mulVecAVX512T<EcMA, VecA>(P, x, y, n, b); // slower
}

void mulEachAVX512(G1 *_x, const Fr *_y, size_t n)
{
#if 1
	typedef EcMA V;
#else
	typedef EcM V;
#endif
	const size_t m = sizeof(V)/(sizeof(FpM)*3)*8;
	assert(n % m == 0);
	const size_t d = n / m;
	const bool isProj = false;
	const bool mixed = true;
	G1A *x = (G1A*)_x;
	const FrA *y = (const FrA*)_y;
	if (!isProj && mixed) ec::normalizeVec((G1*)x, (G1*)x, n);
	const size_t u = 4;
	const size_t q = d / u;
	const size_t remain = d % u;
	V P[u];
	for (size_t i = 0; i < q; i++) {
		for (size_t k = 0; k < u; k++) {
			P[k].setG1A(x+k*m, isProj);
		}
		V::mulGLV<isProj, mixed>(P, P, y, u);
		for (size_t k = 0; k < u; k++) {
			P[k].getG1A(x+k*m, isProj);
		}
		x += m*u;
		y += m*u;
	}
	if (remain == 0) return;
	for (size_t k = 0; k < remain; k++) {
		P[k].setG1A(x+k*m, isProj);
	}
	V::mulGLV<isProj, mixed>(P, P, y, remain);
	for (size_t k = 0; k < remain; k++) {
		P[k].getG1A(x+k*m, isProj);
	}
}

bool initMsm(const mcl::CurveParam& cp)
{
	assert(EcM::a_ == 0);
#ifdef MCL_MSM_BLS12_377
	assert(EcM::b_ == 1);
#else
	assert(EcM::b_ == 4);
#endif
	(void)EcM::a_; // disable unused warning
	(void)EcM::b_;
	(void)EcM::zeroProj_;
	(void)EcM::zeroJacobi_;
	(void)EcMA::a_;
	(void)EcMA::b_;
	(void)EcMA::b3_;
	(void)EcMA::zeroProj_;
	(void)EcMA::zeroJacobi_;

#ifdef MCL_MSM_BLS12_377
	if (cp != mcl::BLS12_377) return false;
#else
	if (cp != mcl::BLS12_381) return false;
#endif
	if ((mcl::bint::g_cpuType & mcl::bint::tAVX512_IFMA) == 0) return false;
	return true;
}

} } // mcl::msm

#ifdef MCL_MSM_TEST
#include <cybozu/xorshift.hpp>
#include <cybozu/benchmark.hpp>

#define CYBOZU_TEST_DISABLE_AUTO_RUN
#include <cybozu/test.hpp>
#include <cybozu/option.hpp>
#define MCL_GLV_ONLY_FUNC
#include "glv.hpp"

using namespace mcl::bn;
using namespace mcl::msm;

template<size_t N, int w = W>
inline void toArray(Unit x[N], const mpz_class& mx)
{
	const Unit mask = getMask(w);
	Unit tmp[N];
	bool b;
	mcl::gmp::getArray(&b, tmp, N, mx);
	assert(b); (void)b;
	for (size_t i = 0; i < N; i++) {
		x[i] = mcl::fp::getUnitAt(tmp, N, i*w) & mask;
	}
}

template<size_t N>
inline mpz_class fromArray(const Unit x[N])
{
	mpz_class mx;
	mcl::gmp::setUnit(mx, x[N-1]);
	for (size_t i = 1; i < N; i++) {
		mx <<= W;
		mcl::gmp::addUnit(mx, x[N-1-i]);
	}
	return mx;
}

class Montgomery {
	Unit v_[N];
public:
	mpz_class mp;
	mpz_class mR; // (1 << (N * 64)) % p
	mpz_class mR2; // (R * R) % p
	Unit rp; // rp * p = -1 mod M = 1 << 64
	const Unit *p;
	bool isFullBit;
	Montgomery() {}
	static Unit getLow(const mpz_class& x)
	{
		if (x == 0) return 0;
		return mcl::gmp::getUnit(x, 0) & g_mask;
	}
	void init(const mpz_class& _p)
	{
		mp = _p;
		mR = 1;
		mR = (mR << (W * N)) % mp;
		mR2 = (mR * mR) % mp;
		toArray<N>(v_, _p);
		rp = mcl::bint::getMontgomeryCoeff(v_[0], W);
		p = v_;
		isFullBit = p[N-1] >> (W-1);
	}

	mpz_class toMont(const mpz_class& x) const
	{
		mpz_class y;
		mul(y, x, mR2);
		return y;
	}
	mpz_class fromMont(const mpz_class& x) const
	{
		mpz_class y;
		mul(y, x, 1);
		return y;
	}

	void mul(mpz_class& z, const mpz_class& x, const mpz_class& y) const
	{
		mod(z, x * y);
	}
	void mod(mpz_class& z, const mpz_class& xy) const
	{
		z = xy;
		mpz_class t;
		for (size_t i = 0; i < N; i++) {
			Unit q = (getLow(z) * rp) & g_mask;
			mcl::gmp::setUnit(t, q);
			z += mp * t;
			z >>= W;
		}
		if (z >= mp) {
			z -= mp;
		}
	}
};

static Montgomery g_mont;

void dump(const FpM& x, const char *msg = nullptr, size_t pos = size_t(-1))
{
	CYBOZU_ALIGN(64) Fp T[8];
	x.getFpA((FpA*)T);
	if (msg) printf("%s\n", msg);
	for (size_t i = 0; i < 8; i++) {
		if (i == pos || pos == size_t(-1)) {
			printf("  [%zd]=%s\n", i, T[i].getStr(16).c_str());
		}
	}
}

void FpM::set(const mpz_class& x, size_t i)
{
	mpz_class r = g_mont.toMont(x);
	Unit rv[N];
	toArray<N>(rv, r);
	::set(v, i, rv);
}
void FpM::set(const mpz_class& x)
{
	mpz_class r = g_mont.toMont(x);
	Unit rv[N];
	toArray<N>(rv, r);
	for (size_t i = 0; i < M; i++) {
		::set(v, i, rv);
	}
}
mpz_class FpM::getRaw(size_t i) const
{
	Unit x[N];
	::get(x, v, i);
	return fromArray<N>(x);
}
mpz_class FpM::get(size_t i) const
{
	mpz_class r = getRaw(i);
	return g_mont.fromMont(r);
}

void dump(const EcM& x, bool isProj, const char *msg = nullptr, size_t pos = size_t(-1))
{
	G1 T[8];
	x.getG1A((G1A*)T, isProj);
	if (msg) printf("%s\n", msg);
	for (size_t i = 0; i < 8; i++) {
		if (i == pos || pos == size_t(-1)) {
			printf("  [%zd]=%s\n", i, T[i].getStr(16|mcl::IoEcProj).c_str());
		}
	}
}

void putA(const uint64_t a[64], const char *msg)
{
	printf("%s\n", msg);
	for (size_t i = 0; i < 8; i++) {
		for (size_t j = 0; j < 8; j++) {
			printf("%02d ", int(a[i * 8 + j]));
		}
		printf("\n");
	}
}

CYBOZU_TEST_AUTO(trans8x8)
{
	MIE_ALIGN(64) uint64_t sa[64], da[64];
	for (size_t i = 0; i < 8; i++) {
		for (size_t j = 0; j < 8; j++) {
			sa[i*8+j] = uint64_t(j + i * 10);
		}
	}
	Vec s[8], d[8];
	memcpy(s, sa, sizeof(s));
	trans8x8(d, s);
	memcpy(da, d, sizeof(d));
	for (size_t i = 0; i < 8; i++) {
		for (size_t j = 0; j < 8; j++) {
			CYBOZU_TEST_EQUAL(sa[i*8+j], da[i+j*8]);
		}
	}
}

CYBOZU_TEST_AUTO(glvParam)
{
	const char *item[] = {"d", "exact b", "cost", "MiB", "heuristic b", "cost", "MiB" };
	const size_t itemN = CYBOZU_NUM_OF_ARRAY(item);
	for (size_t i = 0; i < itemN; i++) {
		if (i > 0) putchar('|');
		printf("%s", item[i]);
	}
	printf("\n");
	for (size_t i = 0; i < itemN; i++) {
		if (i > 0) putchar('|');
		putchar('-');
	}
	printf("\n");
	for (size_t d = 9; d < 28; d++) {
		size_t n = (size_t(1) << d)/8*2; // /(#SIMD)*(GLV)
		size_t b1 = mcl::ec::glvGetTheoreticBucketSize(n);
		size_t cost1 = mcl::ec::glvCost(n, b1);
		double mem1 = (8*8*8*3) * (size_t(1) << b1) / 1024.0 / 1024;
		size_t b2 = glvGetBucketSizeAVX512(n);
		size_t cost2 = mcl::ec::glvCost(n, b2);
		double mem2 = (8*8*8*3) * (size_t(1) << b2) / 1024.0 / 1024;
		printf("%zd|%zd|%zd|%.2f|%zd|%zd(%.2fx)|%.2f(%.2fx)\n", d, b1, cost1, mem1, b2, cost2, cost2/double(cost1), mem2, mem1/mem2);
	}
}

#if 0
CYBOZU_TEST_AUTO(sqr)
{
	const size_t n = 8;
	cybozu::XorShift rg;
	Vec v[n];
	Vec z1[n], z2[n];
	for (size_t i = 0; i < n*8; i++) ((Unit*)v)[i] = rg.get32();
	for (size_t i = 0; i < n; i++) dump(v[i], "v");

	vsqr(z1, v);
	mcl_c5_vsqr(z2, v);
	for (size_t i = 0; i < n*2; i++) {
		printf("i=%zd eq=%s\n", i, memcmp(&z1[i], &z2[i], sizeof(Vec)) == 0 ? "o" : "x");
		dump(z1[i], "z1");
		dump(z2[i], "z2");
	}
}
#endif

void setParam(G1 *P, Fr *x, size_t n, cybozu::XorShift& rg)
{
	for (size_t i = 0; i < n; i++) {
		uint32_t v = rg.get32();
		hashAndMapToG1(P[i], &v, sizeof(v));
		if (x) x[i].setByCSPRNG(rg);
	}
}

CYBOZU_TEST_AUTO(cmp)
{
	const size_t n = 8;
	Vmask v;
	FpM x, y;
	x.clear();
	v = x.isEqualAll(x);
	CYBOZU_TEST_EQUAL(cvtToInt(v), 0xff);
	for (size_t i = 0; i < n; i++) {
		y.clear();
		y.set(1, i);
		v = x.isEqualAll(y);
		CYBOZU_TEST_EQUAL(cvtToInt(v), 0xff ^ (1<<i));
	}
	G1 P[n];
	G1A *PA = (G1A*)P;

	EcM PM, QM;
	cybozu::XorShift rg;
	setParam(P, 0, n, rg);
	PM.setG1A(PA);
	QM.setG1A(PA);
	v = PM.isEqualJacobiAll(QM);
	CYBOZU_TEST_EQUAL(cvtToInt(v), 0xff);
	for (size_t i = 0; i < n; i++) {
		QM = PM;
		QM.x.set(1, i);
		v = PM.isEqualJacobiAll(QM);
		CYBOZU_TEST_EQUAL(cvtToInt(v), 0xff ^ (1<<i));
	}
#ifdef NDEBUG
	CYBOZU_BENCH_C("setG1A", 1000, PM.setG1A, PA);
#endif
}

void setRand(FpM& x, cybozu::XorShift& rg, mcl::Fp *t = 0)
{
	if (t == 0) t = (mcl::Fp*)CYBOZU_ALLOCA(sizeof(mcl::Fp)*N);
	for (size_t i = 0; i < N; i++) {
		t[i].setByCSPRNG(rg);
	}
	x.setFpA((const FpA*)t);
}

CYBOZU_TEST_AUTO(conv)
{
	const int C = 16;
	FpM x1;
	CYBOZU_ALIGN(64) mcl::Fp x2[8], x3[8];
	cybozu::XorShift rg;
	for (int i = 0; i < C; i++) {
		setRand(x1, rg, x2);
		x1.getFpA((FpA*)x3);
		CYBOZU_TEST_EQUAL_ARRAY(x2, x3, 8);
	}
	{
		FpM x[vN], y[vN];
		FpMA z;
		for (size_t i = 0; i < vN; i++) {
			setRand(x[i], rg);
		}
		z.setFpM(x);
		z.getFpM(y);
		for (size_t i = 0; i < vN; i++) {
			CYBOZU_TEST_ASSERT(x[i] == y[i]);
		}
	}
}

template<class T>
void forcedRead(const T& x)
{
	const uint8_t *p = (const uint8_t*)&x;
	uint8_t r = 0;
	for (size_t i = 0; i < sizeof(T); i++) {
		r += p[i];
	}
	volatile uint8_t dummy = r;
	(void)dummy;
}

void asmTest(const mcl::Fp x[8], const mcl::Fp y[8])
{
	CYBOZU_ALIGN(64) mcl::Fp z[8];
	FpM xm, ym, zm, wm;
	xm.setFpA((const FpA*)x);
	ym.setFpA((const FpA*)y);
	// add
	for (size_t i = 0; i < 8; i++) {
		mcl::Fp::add(z[i], x[i], y[i]);
	}
	mcl_c5_vadd(zm.v, xm.v, ym.v);
	wm.setFpA((const FpA*)z);
	CYBOZU_TEST_ASSERT(zm == wm);
	// sub
	for (size_t i = 0; i < 8; i++) {
		mcl::Fp::sub(z[i], x[i], y[i]);
	}
	mcl_c5_vsub(zm.v, xm.v, ym.v);
	wm.setFpA((const FpA*)z);
	CYBOZU_TEST_ASSERT(zm == wm);
	// mul
	for (size_t i = 0; i < 8; i++) {
		mcl::Fp::mul(z[i], x[i], y[i]);
	}
	mcl_c5_vmul(zm.v, xm.v, ym.v);
	wm.setFpA((const FpA*)z);
	CYBOZU_TEST_ASSERT(zm == wm);
	if (zm != wm) {
		for (size_t i = 0; i < 8; i++) {
			printf("i=%zd\n", i);
			dump(zm.v[i], "ok");
			dump(wm.v[i], "ng");
		}
	}
	// sqr
	for (size_t i = 0; i < 8; i++) {
		mcl::Fp::sqr(z[i], x[i]);
	}
	mcl_c5_vsqr(zm.v, xm.v);
	wm.setFpA((const FpA*)z);
	CYBOZU_TEST_ASSERT(zm == wm);
	if (zm != wm) {
		for (size_t i = 0; i < 8; i++) {
			printf("i=%zd\n", i);
			dump(zm.v[i], "ok");
			dump(wm.v[i], "ng");
		}
	}
}

#ifdef USE_ASM
CYBOZU_TEST_AUTO(asm)
{
	cybozu::XorShift rg;
	mcl::Fp x[8], y[8];
	for (int i = 0; i < 30; i++) {
		for (int k = 0; k < 8; k++) {
			x[k] = i*8+k;
		}
		for (int j = 0; j < 30; j++) {
			for (int k = 0; k < 8; k++) {
				y[k] = j*8+k;
			}
		}
		asmTest(x, y);
	}
}
#endif

CYBOZU_TEST_AUTO(vaddPre)
{
	FpM x[vN], y[vN], z[vN], w[vN];
	FpMA xa, ya, za, wa;
	cybozu::XorShift rg;
	for (int i = 0; i < 10; i++) {
		// vaddPre, vsubPre
		for (size_t j = 0; j < vN; j++) {
			setRand(x[j], rg);
			setRand(y[j], rg);
			vaddPre(z[j].v, x[j].v, y[j].v);
			Vec t[N];
			mcl_c5_vaddPre(t, x[j].v, y[j].v);
			for (size_t k = 0; k < N; k++) {
				CYBOZU_TEST_ASSERT(isEqual(t[k], z[j].v[k]));
			}
			vsubPre(w[j].v, z[j].v, x[j].v);
			for (size_t k = 0; k < N; k++) {
				CYBOZU_TEST_ASSERT(isEqual(w[j].v[k], y[j].v[k]));
			}
			mcl_c5_vsubPre(w[j].v, z[j].v, y[j].v);
			for (size_t k = 0; k < N; k++) {
				CYBOZU_TEST_ASSERT(isEqual(w[j].v[k], x[j].v[k]));
			}
		}
		xa.setFpM(x);
		ya.setFpM(y);
		vaddPre(za.v, xa.v, ya.v);
		za.getFpM(w);
		for (size_t j = 0; j < vN; j++) {
			CYBOZU_TEST_ASSERT(z[j] == w[j]);
		}
		mcl_c5_vaddPreA(wa.v, xa.v, ya.v);
		CYBOZU_TEST_ASSERT(za == wa);
		vsubPre<VmaskA>(za.v, za.v, ya.v);
		za.getFpM(w);
		for (size_t j = 0; j < vN; j++) {
			CYBOZU_TEST_ASSERT(x[j] == w[j]);
		}
		// vadd, vsub
		for (size_t j = 0; j < vN; j++) {
			vadd(z[j].v, x[j].v, y[j].v);
			Vec u[8];
			mcl_c5_vadd(u, x[j].v, y[j].v);
			for (size_t k = 0; k < N; k++) {
				CYBOZU_TEST_ASSERT(isEqual(z[j].v[k], u[k]));
			}
			mcl_c5_vsub(u, u, y[j].v);
			for (size_t k = 0; k < N; k++) {
				CYBOZU_TEST_ASSERT(isEqual(x[j].v[k], u[k]));
			}
		}
		w[0].clear();
		w[1].clear();

		xa.setFpM(x);
		ya.setFpM(y);
		FpMA::add(za, xa, ya);
		za.getFpM(w);
		for (size_t j = 0; j < vN; j++) {
			CYBOZU_TEST_ASSERT(z[j] == w[j]);
		}
		FpMA::sub(za, za, ya);
		za.getFpM(w);
		for (size_t j = 0; j < vN; j++) {
			CYBOZU_TEST_ASSERT(x[j] == w[j]);
		}
		{ // vsubA
			VecA u[8];
			vsub<VmaskA>(u, xa.v, ya.v);
			VecA w[8];
			mcl_c5_vsubA(w, xa.v, ya.v);
			for (size_t i = 0; i < 8; i++) {
				CYBOZU_TEST_ASSERT(isEqual(u[i], w[i]));
			}
		}
		// vmul
		for (size_t j = 0; j < vN; j++) {
			vmul(z[j].v, x[j].v, y[j].v);
			Vec w[8];
			mcl_c5_vmul(w, x[j].v, y[j].v);
			for (size_t k = 0; k < N; k++) {
				CYBOZU_TEST_ASSERT(isEqual(z[j].v[k], w[k]));
			}
		}
		xa.setFpM(x);
		ya.setFpM(y);
		FpMA::mul(za, xa, ya);
		za.getFpM(w);
		for (size_t j = 0; j < vN; j++) {
			CYBOZU_TEST_ASSERT(z[j] == w[j]);
		}
		{ // vmulA
			VecA u[8];
			vmul<VmaskA>(u, xa.v, ya.v);
			VecA w[8];
			mcl_c5_vmulA(w, xa.v, ya.v);
			for (size_t i = 0; i < 8; i++) {
				CYBOZU_TEST_ASSERT(isEqual(u[i], w[i]));
			}
		}
		// inv
		for (size_t j = 0; j < vN; j++) {
			FpM::inv(z[j], x[j]);
		}
		xa.setFpM(x);
		FpMA::inv(za, xa);
		za.getFpM(w);
		for (size_t j = 0; j < vN; j++) {
			CYBOZU_TEST_ASSERT(z[j] == w[j]);
		}
	}
#ifdef NDEBUG
	const int C = 10000000;
	CYBOZU_BENCH_C("vaddPre::Vec", C, vaddPre, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("vsubPre::Vec", C, vsubPre, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("vaddPre::VecA", C, vaddPre, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("vsubPre::VecA", C, vsubPre<VmaskA>, za.v, za.v, xa.v);
#if 1
	CYBOZU_BENCH_C("asm vaddPre", C, mcl_c5_vaddPre, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("asm vsubPre", C, mcl_c5_vsubPre, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("asm vaddPreA", C, mcl_c5_vaddPreA, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("asm vsubPreA", C, mcl_c5_vsubPreA, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("asm vadd", C, mcl_c5_vadd, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("asm vsub", C, mcl_c5_vsub, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("asm vmul", C, mcl_c5_vmul, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("asm vaddA", C, mcl_c5_vaddA, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("asm vsubA", C, mcl_c5_vsubA, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("asm vmulA", C, mcl_c5_vmulA, za.v, za.v, xa.v);
#endif
	CYBOZU_BENCH_C("vadd::Vec", C, vadd, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("vsub::Vec", C, vsub, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("vadd::VecA", C, vadd<VmaskA>, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("vsub::VecA", C, vsub<VmaskA>, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("vmul::Vec", C/10, vmul, z[0].v, z[0].v, x[0].v);
	CYBOZU_BENCH_C("vmul::VecA", C/10, vmul<VmaskA>, za.v, za.v, xa.v);
	CYBOZU_BENCH_C("vsqr::Vec", C/10, vsqr, z[0].v, z[0].v);
	CYBOZU_BENCH_C("vsqr::VecA", C/10, vsqr<VmaskA>, za.v, za.v);
	CYBOZU_BENCH_C("FpM::inv", C/100, FpM::inv, z[0], z[0]);
	CYBOZU_BENCH_C("FpMA::inv", C/100, FpMA::inv, za, za);
	{
		CYBOZU_ALIGN(64) mcl::Fp vv[8*vN];
		za.getFpA((FpA*)vv);
		CYBOZU_BENCH_C("Fp::inv(8)", C/100, mcl::invVec<mcl::Fp>, vv, vv, 8*vN);
	}
	forcedRead(z);
	forcedRead(za);
#endif
}

template<class T>
void lpN(void f(T&, const T&, const T&), T *z, const T *x, const T *y, size_t n)
{
	for (size_t i = 0; i < n; i++) f(z[i], x[i], y[i]);
}

CYBOZU_TEST_AUTO(op)
{
	const size_t n = 8; // fixed
	G1 P[n];
	G1 Q[n];
	G1 R[n];
	G1 T[n];
	Fr x[n];
	G1A *PA = (G1A*)P;
	G1A *QA = (G1A*)Q;
	G1A *TA = (G1A*)T;

	EcM PM, QM, TM;
	cybozu::XorShift rg;
	setParam(P, x, n, rg);
	setParam(Q, x, n, rg);
	P[3].clear();
	Q[4].clear();
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_ASSERT(!P[i].z.isOne());
	}
	ec::normalizeVec(R, P, n);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_ASSERT(R[i].z.isOne() || R[i].z.isZero());
	}
	CYBOZU_TEST_EQUAL_ARRAY(P, R, n);

	// test dbl
	// R = 2P
	for (size_t i = 0; i < n; i++) {
		G1::dbl(R[i], P[i]);
	}
	// as Proj
	PM.setG1A(PA);
	EcM::dbl<true>(TM, PM);
	TM.getG1A(TA);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// as Jacobi
	PM.setG1A(PA, false);
	EcM::dbl<false>(TM, PM);
	TM.getG1A(TA, false);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// test add
	// R = P + Q
	for (size_t i = 0; i < n; i++) {
		G1::add(R[i], P[i], Q[i]);
	}

	// as Proj
	PM.setG1A(PA);
	QM.setG1A(QA);
	EcM::add<true>(TM, PM, QM);
	TM.getG1A(TA);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// as Jacobi
	PM.setG1A(PA, false);
	QM.setG1A(QA, false);
	EcM::add<false>(TM, PM, QM);
	TM.getG1A(TA, false);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// as Jacobi (mixed)
	for (size_t i = 0; i < n; i++) {
		Q[i].normalize();
	}
	QM.setG1A(QA, false);
	EcM::add<false, true>(TM, PM, QM);
	TM.getG1A(TA, false);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}
#ifdef NDEBUG
	{
		const int C = 10000;
		const size_t n = 128;
		FpM a[n], b[n];
		for (size_t i = 0; i < n; i++) {
			a[i] = PM.x;
			b[i] = PM.y;
		}
		CYBOZU_BENCH_C("FpM::add", C, FpM::add, a[0], a[0], b[0]);
		CYBOZU_BENCH_C("FpM::mul", C, FpM::mul, a[0], a[0], b[0]);
		CYBOZU_BENCH_C("FpM::sqr", C, FpM::sqr, a[0], a[0]);
		CYBOZU_BENCH_C("mulu", C, FpM::mul, a[0], a[0], g_m64to52u_);
		CYBOZU_BENCH_C("addn", C, lpN, FpM::add, a, a, b, n);
		CYBOZU_BENCH_C("muln", C, lpN, FpM::mul, a, a, b, n);
	}
#endif
}

CYBOZU_TEST_AUTO(opA)
{
	const size_t n = N*vN;
	G1 P[n];
	G1 Q[n];
	G1 R[n];
	G1 T[n];
	Fr x[n];
	G1A *PA = (G1A*)P;
	G1A *QA = (G1A*)Q;
	G1A *TA = (G1A*)T;

	EcMA PM, QM, TM;
	cybozu::XorShift rg;
	setParam(P, x, n, rg);
	setParam(Q, x, n, rg);
	P[3].clear();
	Q[4].clear();
	// test dbl
	// R = 2P
	for (size_t i = 0; i < n; i++) {
		G1::dbl(R[i], P[i]);
	}
	// as Proj
	PM.setG1A(PA);
	EcMA::dbl<true>(TM, PM);
	TM.getG1A(TA);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// as Jacobi
	PM.setG1A(PA, false);
	EcMA::dbl<false>(TM, PM);
	TM.getG1A(TA, false);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// test add
	// R = P + Q
	for (size_t i = 0; i < n; i++) {
		G1::add(R[i], P[i], Q[i]);
	}

	// as Proj
	PM.setG1A(PA);
	QM.setG1A(QA);
	EcMA::add<true>(TM, PM, QM);
	TM.getG1A(TA);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// as Jacobi
	PM.setG1A(PA, false);
	QM.setG1A(QA, false);
	EcMA::add<false>(TM, PM, QM);
	TM.getG1A(TA, false);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}

	// as Jacobi (mixed)
	for (size_t i = 0; i < n; i++) {
		Q[i].normalize();
	}
	QM.setG1A(QA, false);
	EcMA::add<false, true>(TM, PM, QM);
	TM.getG1A(TA, false);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], T[i]);
	}
	{
		EcM PM2, QM2;
		PM2.setG1A(PA);
		QM2.setG1A(QA);
		const int C = 10000;
		CYBOZU_BENCH_C("EcM::dbl", C, EcM::dbl, QM2, QM2);
		CYBOZU_BENCH_C("EcM::add", C, EcM::add, PM2, PM2, QM2);
		CYBOZU_BENCH_C("EcMA::dbl", C, EcMA::dbl, QM, QM);
		CYBOZU_BENCH_C("EcMA::add", C, EcMA::add, TM, TM, QM);
		CYBOZU_BENCH_C("EcMA::setG1A:proj", C, PM.setG1A, PA, true);
		CYBOZU_BENCH_C("EcMA::setG1A:jacobi", C, PM.setG1A, PA, false);
		CYBOZU_BENCH_C("EcMA::getG1A:proj", C, PM.getG1A, PA, true);
		CYBOZU_BENCH_C("EcMA::getG1A:jacobi", C, PM.getG1A, PA, false);
	}
}

CYBOZU_TEST_AUTO(normalizeJacobiVec)
{
	const bool isProj = false;
	const size_t N = 64;
	CYBOZU_ALIGN(64) G1 P[N], Q[N], R[N];
	EcM PP[N/8];
	for (size_t n = 8; n < N; n += 8) {
		cybozu::XorShift rg;
		setParam(P, 0, n, rg);
		P[n/2].clear();
		P[n/3].clear();
		mcl::ec::normalizeVec(Q, P, n);
		for (size_t i = 0; i < n/8; i++) {
			PP[i].setG1A((G1A*)&P[i*8], isProj);
		}
		normalizeJacobiVec<EcM>(PP, n/8);
		for (size_t i = 0; i < n/8; i++) {
			PP[i].getG1A((G1A*)&R[i*8], isProj);
		}
		CYBOZU_TEST_EQUAL_ARRAY(P, R, n);
	}
	const int C = 10000;
	CYBOZU_BENCH_C("EcM::setG1A:proj", C, PP[0].setG1A, (G1A*)P, true);
	CYBOZU_BENCH_C("EcM::setG1A:jacobi", C, PP[0].setG1A, (G1A*)P, false);
	CYBOZU_BENCH_C("EcM::getG1A:proj", C, PP[0].getG1A, (G1A*)P, true);
	CYBOZU_BENCH_C("EcM::getG1A:jacobi", C, PP[0].getG1A, (G1A*)P, false);
}

CYBOZU_TEST_AUTO(mulEach_special)
{
	const size_t n = 8;
	CYBOZU_ALIGN(64) G1 P[n], Q[n], R[n];
	Fr x[n];
	mpz_class L;
	for (size_t i = 0; i < n; i++) P[i].clear();
	mcl::hashAndMapToG1(P[0], "abc");
	x[0].setHashOf("abc", 3);
#ifdef MCL_MSM_BLS12_377
	mcl::gmp::setStr(L, "0x452217cc900000010a11800000000000");
#else
	mcl::gmp::setStr(L, "0xac45a4010001a40200000000ffffffff");
#endif
	for (size_t i = 0; i < n; i++) Q[i] = P[i];
	G1::mul(R[0], P[0], x[0]);
	G1::mulEach(Q, x, 8);
	CYBOZU_TEST_EQUAL(R[0], Q[0]);
	mpz_class tbl[] = {
		0,
		1,
		L,
	};
	cybozu::XorShift rg;
	for (size_t i = 0; i < CYBOZU_NUM_OF_ARRAY(tbl); i++) {
		const mpz_class& a = tbl[i];
		for (size_t j = 0; j < CYBOZU_NUM_OF_ARRAY(tbl); j++) {
			const mpz_class& b = tbl[j];
			setParam(P, x, n, rg);
			x[0].setMpz(a * L + b);
			for (size_t k = 0; k < 8; k++) {
				Q[k] = P[k];
				G1::mul(R[k], P[k], x[k]);
			}
			G1::mulEach(Q, x, n);
			CYBOZU_TEST_EQUAL_ARRAY(R, Q, n);
		}
	}
}

void mulEachOrg(G1 *P, const Fr *x, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		G1::mul(P[i], P[i], x[i]);
	}
}

void mulVecOrg(G1& Q, G1 *P, const Fr *x, size_t n)
{
	mulEachOrg(P, x, n);
	Q = P[0];
	for (size_t i = 1; i < n; i++) {
		Q += P[i];
	}
}

CYBOZU_TEST_AUTO(mulEach)
{
	const size_t n = 1024;
	CYBOZU_ALIGN(64) G1 P[n], Q[n], R[n];
	Fr x[n];
	cybozu::XorShift rg;
	setParam(P, x, n, rg);
	if (n > 32) P[32].clear();
	P[n/2].clear();
	for (size_t i = 0; i < n; i++) {
		Q[i] = P[i];
		G1::mul(R[i], P[i], x[i]);
	}
	G1::mulEach(Q, x, n);
	for (size_t i = 0; i < n; i++) {
		CYBOZU_TEST_EQUAL(R[i], Q[i]);
		if (R[i] != Q[i]) {
			printf("P[%zd]=%s\n", i, P[i].getStr(16).c_str());
			printf("x[%zd]=%s\n", i, x[i].getStr(16).c_str());
			printf("R[%zd]=%s\n", i, R[i].getStr(16|mcl::IoEcProj).c_str());
			printf("Q[%zd]=%s\n", i, Q[i].getStr(16|mcl::IoEcProj).c_str());
		}
	}
#ifdef NDEBUG
	CYBOZU_BENCH_C("mulEachOrg", 100, mulEachOrg, Q, x, n);
	CYBOZU_BENCH_C("mulEach", 100, G1::mulEach, Q, x, n);
#endif
}

void copyMulVec(G1& R, const G1 *_P, const Fr *x, size_t n)
{
	G1 *P = (G1*)CYBOZU_ALLOCA(sizeof(G1) * n);
	mcl::bint::copyN(P, _P, n);
	mulVecAVX512(R, P, x, n);
}

CYBOZU_TEST_AUTO(mulVec)
{
	const size_t n = 8203;
	CYBOZU_ALIGN(64) G1 P[n], Q, R;
	Fr x[n];
	cybozu::XorShift rg;
	setParam(P, x, n, rg);
	if (n > 32) P[32].clear();
	P[n/2].clear();
	Q.clear();
	for (size_t i = 0; i < n; i++) {
		G1 T;
		G1::mul(T, P[i], x[i]);
		Q += T;
	}
	G1 P2[n];
	mcl::bint::copyN(P2, P, n);
	mulVecAVX512(R, P, x, n);
//	G1::mulVec(R, P, x, n);
	CYBOZU_TEST_EQUAL(Q, R);
#ifdef NDEBUG
	G1 R2;
	CYBOZU_BENCH_C("mulVec(copy)", 30, copyMulVec, R2, P2, x, n);
	CYBOZU_BENCH_C("mulVec", 30, mulVecAVX512, R, P, x, n);
	CYBOZU_TEST_EQUAL(R, R2);
	CYBOZU_BENCH_C("mulVecOrg", 3, mulVecOrg, R2, P2, x, n);
#endif
}

void msmBench(int C, size_t db, size_t de, size_t b)
{
	printf("d = [%zd, %zd], b = %zd\n", db, de, b);
	const size_t maxN = size_t(1) << de;
	cybozu::XorShift rg;
	std::vector<G1> Pvec(maxN);
	std::vector<Fr> xVec(maxN);
	hashAndMapToG1(Pvec[0], "abc", 3);
	for (size_t i = 1; i < maxN; i++) {
		G1::add(Pvec[i], Pvec[i-1], Pvec[0]);
		xVec[i].setByCSPRNG(rg);
	}
	G1 P1;
	for (size_t d = db; d <= de; d++) {
		size_t n = size_t(1) << d;
		size_t b1 = glvGetBucketSizeAVX512(n/4);
		printf("% 8zd % 2zd", n, b1);
		CYBOZU_BENCH_C(" ", C, mulVecAVX512, P1, Pvec.data(), xVec.data(), n, b1);
		size_t b2 = b ? b : mcl::ec::glvGetTheoreticBucketSize(n/4);
		printf("% 8zd % 2zd", n, size_t(b2));
		CYBOZU_BENCH_C(" ", C, mulVecAVX512, P1, Pvec.data(), xVec.data(), n, b2);
	}
}

void showParams()
{
	puts("d|b|cost_b|theoretic|cost_t|cost_b/cost_t");
	for (size_t d = 7; d <= 27; d++) {
		size_t n = size_t(1) << d;
		size_t nn = n/8*2; // /#SIMD*GLV
		size_t b1 = glvGetBucketSizeAVX512(nn);
		size_t c1 = mcl::ec::glvCost(nn, b1);
		size_t b2 = mcl::ec::glvGetTheoreticBucketSize(nn);
		size_t c2 = mcl::ec::glvCost(nn, b2);
		printf("%zd|%zd|%zd|%zd|%zd|%.2f\n", d, b1, c1, b2, c2, c1/double(c2));
	}
}

int main(int argc, char *argv[])
{
	cybozu::Option opt;
	size_t db, de, d;
	size_t b;
	bool msm, show;
	int C;
	opt.appendOpt(&b, 0, "b", ": set bucket size");
	opt.appendOpt(&d, 9, "d", ": set n to 1<<d");
	opt.appendOpt(&db, 0, "db", ": set begin of d");
	opt.appendOpt(&de, 0, "de", ": set end of d");
	opt.appendOpt(&C, 10, "c", ": count of loop");
	opt.appendBoolOpt(&msm, "msm", ": msm bench");
	opt.appendBoolOpt(&show, "show", ": show params");
	opt.appendHelp("h", ": show this message");
	if (!opt.parse(argc, argv)) {
		opt.usage();
		return 1;
	}
	if (show) {
		showParams();
		return 0;
	}
	if (db == 0) {
		db = d;
	}
	if (de == 0) {
		de = d;
	}
#ifdef MCL_MSM_BLS12_377
	puts("BLS12_377");
	initPairing(mcl::BLS12_377);
#else
	puts("BLS12_381");
	initPairing(mcl::BLS12_381);
#endif
	g_mont.init(mcl::Fp::getOp().mp);
	if (msm) {
		msmBench(C, db, de, b);
		return 0;
	}
	return cybozu::test::autoRun.run(argc, argv);
}
#endif
