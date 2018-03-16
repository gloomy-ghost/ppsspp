// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <cstring>
#include "ppsspp_config.h"
#include "base/basictypes.h"
#include "Common/Hashmaps.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/GPUCommon.h"

#if PPSSPP_ARCH(ARM)
#include "Common/ArmEmitter.h"
#elif PPSSPP_ARCH(ARM64)
#include "Common/Arm64Emitter.h"
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include "Common/x64Emitter.h"
#elif PPSSPP_ARCH(MIPS)
#include "Common/MipsEmitter.h"
#else
#include "Common/FakeEmitter.h"
#endif

// DecVtxFormat - vertex formats for PC
// Kind of like a D3D VertexDeclaration.
// Can write code to easily bind these using OpenGL, or read these manually.
// No morph support, that is taken care of by the VertexDecoder.

// Keep this in 4 bits.
enum {
	DEC_NONE,
	DEC_FLOAT_1,
	DEC_FLOAT_2,
	DEC_FLOAT_3,
	DEC_FLOAT_4,
	DEC_S8_3,
	DEC_S16_3,
	DEC_U8_1,
	DEC_U8_2,
	DEC_U8_3,
	DEC_U8_4,
	DEC_U16_1,
	DEC_U16_2,
	DEC_U16_3,
	DEC_U16_4,
};

int DecFmtSize(u8 fmt);

struct DecVtxFormat {
	u8 uvfmt; u8 uvoff;
	u8 c0fmt; u8 c0off;  // First color
	u8 c1fmt; u8 c1off;
	u8 nrmfmt; u8 nrmoff;
	u8 posfmt; u8 posoff;
	short stride;

	uint32_t id;
	void ComputeID();
};

void GetIndexBounds(const void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound);

inline int RoundUp4(int x) {
	return (x + 3) & ~3;
}

class IndexConverter {
private:
	union {
		const void *indices;
		const u8 *indices8;
		const u16 *indices16;
		const u32 *indices32;
	};
	u32 indexType;

public:
	IndexConverter(u32 vertType, const void *indices)
		: indices(indices), indexType(vertType & GE_VTYPE_IDX_MASK) {
	}

	inline u32 convert(u32 index) const {
		switch (indexType) {
		case GE_VTYPE_IDX_8BIT:
			return indices8[index];
		case GE_VTYPE_IDX_16BIT:
			return indices16[index];
		case GE_VTYPE_IDX_32BIT:
			return indices32[index];
		default:
			return index;
		}
	}
};

// Reads decoded vertex formats in a convenient way. For software transform and debugging.
class VertexReader {
public:
	VertexReader(u8 *base, const DecVtxFormat &decFmt, int vtype) : base_(base), data_(base), decFmt_(decFmt), vtype_(vtype) {}

	void ReadPos(float pos[3]) const {
		switch (decFmt_.posfmt) {
		case DEC_FLOAT_3:
			{
				const float *f = (const float *)(data_ + decFmt_.posoff);
				memcpy(pos, f, 12);
				if (isThrough()) {
					// Integer value passed in a float. Clamped to 0, 65535.
					const float z = (int)pos[2] * (1.0f / 65535.0f);
					pos[2] = z > 1.0f ? 1.0f : (z < 0.0f ? 0.0f : z);
				}
			}
			break;
		case DEC_S16_3:
			{
				// X and Y are signed 16 bit, Z is unsigned 16 bit
				const s16 *s = (const s16 *)(data_ + decFmt_.posoff);
				const u16 *u = (const u16 *)(data_ + decFmt_.posoff);
				if (isThrough()) {
					for (int i = 0; i < 2; i++)
						pos[i] = s[i];
					pos[2] = u[2] * (1.0f / 65535.0f);
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = s[i] * (1.0f / 32768.0f);
				}
			}
			break;
		case DEC_S8_3:
			{
				// X and Y are signed 8 bit, Z is unsigned 8 bit
				const s8 *b = (const s8 *)(data_ + decFmt_.posoff);
				const u8 *u = (const u8 *)(data_ + decFmt_.posoff);
				if (isThrough()) {
					for (int i = 0; i < 2; i++)
						pos[i] = b[i];
					pos[2] = u[2] * (1.0f / 255.0f);
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = b[i] * (1.0f / 128.0f);
				}
			}
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtpos, G3D, "Reader: Unsupported Pos Format %d", decFmt_.posfmt);
			memset(pos, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadPosThroughZ16(float pos[3]) const {
		switch (decFmt_.posfmt) {
		case DEC_FLOAT_3:
			{
				const float *f = (const float *)(data_ + decFmt_.posoff);
				memcpy(pos, f, 12);
				if (isThrough()) {
					// Integer value passed in a float. Clamped to 0, 65535.
					const float z = (int)pos[2];
					pos[2] = z > 65535.0f ? 65535.0f : (z < 0.0f ? 0.0f : z);
				}
			}
			break;
		case DEC_S16_3:
			{
				// X and Y are signed 16 bit, Z is unsigned 16 bit
				const s16 *s = (const s16 *)(data_ + decFmt_.posoff);
				const u16 *u = (const u16 *)(data_ + decFmt_.posoff);
				if (isThrough()) {
					for (int i = 0; i < 2; i++)
						pos[i] = s[i];
					pos[2] = u[2];
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = s[i] * (1.0f / 32768.0f);
				}
			}
			break;
		case DEC_S8_3:
			{
				// X and Y are signed 8 bit, Z is unsigned 8 bit
				const s8 *b = (const s8 *)(data_ + decFmt_.posoff);
				const u8 *u = (const u8 *)(data_ + decFmt_.posoff);
				if (isThrough()) {
					for (int i = 0; i < 2; i++)
						pos[i] = b[i];
					pos[2] = u[2];
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = b[i] * (1.0f / 128.0f);
				}
			}
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtz16, G3D, "Reader: Unsupported Pos Format %d", decFmt_.posfmt);
			memset(pos, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadNrm(float nrm[3]) const {
		switch (decFmt_.nrmfmt) {
		case DEC_FLOAT_3:
			//memcpy(nrm, data_ + decFmt_.nrmoff, 12);
			{
				const float *f = (const float *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = f[i];
			}
			break;
		case DEC_S16_3:
			{
				const s16 *s = (const s16 *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = s[i] * (1.f / 32767.f);
			}
			break;
		case DEC_S8_3:
			{
				const s8 *b = (const s8 *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = b[i] * (1.f / 127.f);
			}
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtnrm, G3D, "Reader: Unsupported Nrm Format %d", decFmt_.nrmfmt);
			memset(nrm, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadUV(float uv[2]) const {
		switch (decFmt_.uvfmt) {
		case DEC_U8_2:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.uvoff);
				uv[0] = b[0] * (1.f / 128.f);
				uv[1] = b[1] * (1.f / 128.f);
			}
			break;

		case DEC_U16_2:
			{
				const u16 *s = (const u16 *)(data_ + decFmt_.uvoff);
				uv[0] = s[0] * (1.f / 32768.f);
				uv[1] = s[1] * (1.f / 32768.f);
			}
			break;

		case DEC_FLOAT_2:
			{
				const float *f = (const float *)(data_ + decFmt_.uvoff);
				uv[0] = f[0];
				uv[1] = f[1];
			}
			break;

		default:
			ERROR_LOG_REPORT_ONCE(fmtuv, G3D, "Reader: Unsupported UV Format %d", decFmt_.uvfmt);
			memset(uv, 0, sizeof(float) * 2);
			break;
		}
	}

	void ReadColor0(float color[4]) const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c0off);
				for (int i = 0; i < 4; i++)
					color[i] = b[i] * (1.f / 255.f);
			}
			break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c0off, 16); 
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtc0, G3D, "Reader: Unsupported C0 Format %d", decFmt_.c0fmt);
			memset(color, 0, sizeof(float) * 4);
			break;
		}
	}

	void ReadColor0_8888(u8 color[4]) const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c0off);
				for (int i = 0; i < 4; i++)
					color[i] = b[i];
			}
			break;
		case DEC_FLOAT_4:
			{
				const float *f = (const float *)(data_ + decFmt_.c0off);
				for (int i = 0; i < 4; i++)
					color[i] = f[i] * 255.0f;
			}
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtc0_8888, G3D, "Reader: Unsupported C0 Format %d", decFmt_.c0fmt);
			memset(color, 0, sizeof(u8) * 4);
			break;
		}
	}


	void ReadColor1(float color[3]) const {
		switch (decFmt_.c1fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c1off);
				for (int i = 0; i < 3; i++)
					color[i] = b[i] * (1.f / 255.f);
			}
			break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c1off, 12); 
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtc1, G3D, "Reader: Unsupported C1 Format %d", decFmt_.c1fmt);
			memset(color, 0, sizeof(float) * 3);
			break;
		}
	}

	bool hasColor0() const { return decFmt_.c0fmt != 0; }
	bool hasColor1() const { return decFmt_.c1fmt != 0; }
	bool hasNormal() const { return decFmt_.nrmfmt != 0; }
	bool hasUV() const { return decFmt_.uvfmt != 0; }
	bool isThrough() const { return (vtype_ & GE_VTYPE_THROUGH) != 0; }
	void Goto(int index) {
		data_ = base_ + index * decFmt_.stride;
	}

private:
	u8 *base_;
	u8 *data_;
	DecVtxFormat decFmt_;
	int vtype_;
};
// Debugging utilities
void PrintDecodedVertex(VertexReader &vtx);


class VertexDecoder;
class VertexDecoderJitCache;

typedef void (VertexDecoder::*StepFunction)() const;
typedef void (VertexDecoderJitCache::*JitStepFunction)();

struct JitLookup {
	StepFunction func;
	JitStepFunction jitFunc;
};

typedef void(*JittedVertexDecoder)(const u8 *src, u8 *dst, int count);

struct VertexDecoderOptions {
	bool expandAllWeightsToFloat;
	bool expand8BitNormalsToFloat;
};

class VertexDecoder {
public:
	VertexDecoder();

	// A jit cache is not mandatory.
	void SetVertexType(u32 vtype, const VertexDecoderOptions &options, VertexDecoderJitCache *jitCache = nullptr);

	u32 VertexType() const { return fmt_; }

	const DecVtxFormat &GetDecVtxFmt() { return decFmt; }

	void DecodeVerts(u8 *decoded, const void *verts, int indexLowerBound, int indexUpperBound) const;

	bool hasColor() const { return col != 0; }
	bool hasTexcoord() const { return tc != 0; }
	int VertexSize() const { return size; }  // PSP format size

	std::string GetString(DebugShaderStringType stringType);

	void ComputeSkinMatrix(const float weights[8]) const;

	void Step_WeightsU8Skin() const;
	void Step_WeightsU16Skin() const;
	void Step_WeightsFloatSkin() const;

	void Step_TcU8ToFloat() const;
	void Step_TcU16ToFloat() const;
	void Step_TcFloat() const;

	void Step_TcU8Prescale() const;
	void Step_TcU16Prescale() const;
	void Step_TcU16DoublePrescale() const;
	void Step_TcFloatPrescale() const;

	void Step_TcU16DoubleToFloat() const;
	void Step_TcU16ThroughToFloat() const;
	void Step_TcU16ThroughDoubleToFloat() const;
	void Step_TcFloatThrough() const;

	void Step_TcU8MorphToFloat() const;
	void Step_TcU16MorphToFloat() const;
	void Step_TcU16DoubleMorphToFloat() const;
	void Step_TcFloatMorph() const;
	void Step_TcU8PrescaleMorph() const;
	void Step_TcU16PrescaleMorph() const;
	void Step_TcU16DoublePrescaleMorph() const;
	void Step_TcFloatPrescaleMorph() const;

	void Step_ColorInvalid() const;
	void Step_Color4444() const;
	void Step_Color565() const;
	void Step_Color5551() const;
	void Step_Color8888() const;

	void Step_Color4444Morph() const;
	void Step_Color565Morph() const;
	void Step_Color5551Morph() const;
	void Step_Color8888Morph() const;

	void Step_NormalS8() const;
	void Step_NormalS8ToFloat() const;
	void Step_NormalS16() const;
	void Step_NormalFloat() const;

	void Step_NormalS8Skin() const;
	void Step_NormalS16Skin() const;
	void Step_NormalFloatSkin() const;

	void Step_NormalS8Morph() const;
	void Step_NormalS16Morph() const;
	void Step_NormalFloatMorph() const;

	void Step_NormalS8MorphSkin() const;
	void Step_NormalS16MorphSkin() const;
	void Step_NormalFloatMorphSkin() const;

	void Step_PosS8() const;
	void Step_PosS16() const;
	void Step_PosFloat() const;

	void Step_PosS8Skin() const;
	void Step_PosS16Skin() const;
	void Step_PosFloatSkin() const;

	void Step_PosS8Morph() const;
	void Step_PosS16Morph() const;
	void Step_PosFloatMorph() const;

	void Step_PosS8MorphSkin() const;
	void Step_PosS16MorphSkin() const;
	void Step_PosFloatMorphSkin() const;

	void Step_PosS8Through() const;
	void Step_PosS16Through() const;
	void Step_PosFloatThrough() const;

	// output must be big for safety.
	// Returns number of chars written.
	// Ugly for speed.
	int ToString(char *output) const;

	// Mutable decoder state
	mutable u8 *decoded_;
	mutable const u8 *ptr_;

	JittedVertexDecoder jitted_;
	int32_t jittedSize_;

	// "Immutable" state, set at startup

	// The decoding steps. Never more than 5.
	StepFunction steps_[5];
	int numSteps_;

	u32 fmt_;
	DecVtxFormat decFmt;

	bool throughmode;
	u8 size;
	u8 onesize_;

	u8 weightoff;
	u8 tcoff;
	u8 coloff;
	u8 nrmoff;
	u8 posoff;

	u8 tc;
	u8 col;
	u8 nrm;
	u8 pos;
	u8 weighttype;
	u8 idx;
	u8 morphcount;
	u8 nweights;

	u8 biggest;  // in practice, alignment.

	friend class VertexDecoderJitCache;
};


// A compiled vertex decoder takes the following arguments (C calling convention):
// u8 *src, u8 *dst, int count
//
// x86:
//   src is placed in esi and dst in edi
//   for every vertex, we step esi and edi forwards by the two vertex sizes
//   all movs are done relative to esi and edi
//
// that's it!


#if PPSSPP_ARCH(ARM)
class VertexDecoderJitCache : public ArmGen::ARMXCodeBlock {
#elif PPSSPP_ARCH(ARM64)
class VertexDecoderJitCache : public Arm64Gen::ARM64CodeBlock {
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
class VertexDecoderJitCache : public Gen::XCodeBlock {
#elif PPSSPP_ARCH(MIPS)
class VertexDecoderJitCache : public MIPSGen::MIPSCodeBlock {
#else
class VertexDecoderJitCache : public FakeGen::FakeXCodeBlock {
#endif
public:
	VertexDecoderJitCache();

	// Returns a pointer to the code to run.
	JittedVertexDecoder Compile(const VertexDecoder &dec, int32_t *jittedSize);
	void Clear();

	void Jit_WeightsU8Skin();
	void Jit_WeightsU16Skin();
	void Jit_WeightsFloatSkin();

	void Jit_TcU8ToFloat();
	void Jit_TcU16ToFloat();
	void Jit_TcFloat();

	void Jit_TcU8Prescale();
	void Jit_TcU16Prescale();
	void Jit_TcFloatPrescale();

	void Jit_TcAnyMorph(int bits);
	void Jit_TcU8MorphToFloat();
	void Jit_TcU16MorphToFloat();
	void Jit_TcFloatMorph();
	void Jit_TcU8PrescaleMorph();
	void Jit_TcU16PrescaleMorph();
	void Jit_TcFloatPrescaleMorph();

	void Jit_TcU16ThroughToFloat();
	void Jit_TcFloatThrough();

	void Jit_Color8888();
	void Jit_Color4444();
	void Jit_Color565();
	void Jit_Color5551();

	void Jit_NormalS8();
	void Jit_NormalS8ToFloat();
	void Jit_NormalS16();
	void Jit_NormalFloat();

	void Jit_NormalS8Skin();
	void Jit_NormalS16Skin();
	void Jit_NormalFloatSkin();

	void Jit_PosS8();
	void Jit_PosS8ToFloat();
	void Jit_PosS16();
	void Jit_PosFloat();
	void Jit_PosS8Through();
	void Jit_PosS16Through();

	void Jit_PosS8Skin();
	void Jit_PosS16Skin();
	void Jit_PosFloatSkin();

	void Jit_NormalS8Morph();
	void Jit_NormalS16Morph();
	void Jit_NormalFloatMorph();

	void Jit_PosS8Morph();
	void Jit_PosS16Morph();
	void Jit_PosFloatMorph();

	void Jit_Color8888Morph();
	void Jit_Color4444Morph();
	void Jit_Color565Morph();
	void Jit_Color5551Morph();

private:
	bool CompileStep(const VertexDecoder &dec, int i);
	void Jit_ApplyWeights();
	void Jit_WriteMatrixMul(int outOff, bool pos);
	void Jit_WriteMorphColor(int outOff, bool checkAlpha = true);
	void Jit_AnyS8ToFloat(int srcoff);
	void Jit_AnyS16ToFloat(int srcoff);
	void Jit_AnyU8ToFloat(int srcoff, u32 bits = 32);
	void Jit_AnyU16ToFloat(int srcoff, u32 bits = 64);
	void Jit_AnyS8Morph(int srcoff, int dstoff);
	void Jit_AnyS16Morph(int srcoff, int dstoff);
	void Jit_AnyFloatMorph(int srcoff, int dstoff);

	const VertexDecoder *dec_;
#if PPSSPP_ARCH(ARM64)
	Arm64Gen::ARM64FloatEmitter fp;
#endif
};
