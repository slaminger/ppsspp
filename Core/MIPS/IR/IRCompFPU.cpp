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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"
#include "Common/CPUDetect.h"

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)


// FPCR interesting bits:
// 24: FZ (flush-to-zero)
// 23:22: RMode (0 = nearest, 1 = +inf, 2 = -inf, 3 = zero)
// not much else is interesting for us, but should be preserved.
// To access: MRS Xt, FPCR ;  MSR FPCR, Xt


// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp {

void IRJit::Comp_FPU3op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int ft = _FT;
	int fs = _FS;
	int fd = _FD;

	switch (op & 0x3f) {
	case 0: ir.Write(IROp::FAdd, fd, fs, ft); break; //F(fd) = F(fs) + F(ft); //add
	case 1: ir.Write(IROp::FSub, fd, fs, ft); break; //F(fd) = F(fs) - F(ft); //sub
	case 2: ir.Write(IROp::FMul, fd, fs, ft); break; //F(fd) = F(fs) * F(ft); //mul
	case 3: ir.Write(IROp::FDiv, fd, fs, ft); break; //F(fd) = F(fs) / F(ft); //div
	default:
		DISABLE;
		return;
	}
}

void IRJit::Comp_FPULS(MIPSOpcode op) {
	DISABLE;
}

void IRJit::Comp_FPUComp(MIPSOpcode op) {
	DISABLE;

	int opc = op & 0xF;
	if (opc >= 8) opc -= 8; // alias
	if (opc == 0) {  // f, sf (signalling false)
		ir.Write(IROp::ZeroFpCond);
		return;
	}

	int fs = _FS;
	int ft = _FT;

	IROp irOp;
	switch (opc) {
	case 1:      // un,  ngle (unordered)
		irOp = IROp::FCmpUnordered;
		break;
	case 2:      // eq,  seq (equal, ordered)
		irOp = IROp::FCmpEqual;
		break;
	case 3:      // ueq, ngl (equal, unordered)
		irOp = IROp::FCmpEqualUnordered;
		return;
	case 4:      // olt, lt (less than, ordered)
		irOp = IROp::FCmpLessOrdered;
		break;
	case 5:      // ult, nge (less than, unordered)
		irOp = IROp::FCmpLessUnordered;
		break;
	case 6:      // ole, le (less equal, ordered)
		irOp = IROp::FCmpLessEqualOrdered;
		break;
	case 7:      // ule, ngt (less equal, unordered)
		irOp = IROp::FCmpLessEqualUnordered;
		break;
	default:
		Comp_Generic(op);
		return;
	}
	ir.Write(irOp, fs, ft);
}

void IRJit::Comp_FPU2op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	int fs = _FS;
	int fd = _FD;

	switch (op & 0x3f) {
	case 4:	//F(fd)	   = sqrtf(F(fs));            break; //sqrt
		ir.Write(IROp::FSqrt, fd, fs);
		break;
	case 5:	//F(fd)    = fabsf(F(fs));            break; //abs
		ir.Write(IROp::FAbs, fd, fs);
		break;
	case 6:	//F(fd)	   = F(fs);                   break; //mov
		ir.Write(IROp::FMov, fd, fs);
		break;
	case 7:	//F(fd)	   = -F(fs);                  break; //neg
		ir.Write(IROp::FNeg, fd, fs);
		break;

	case 12: //FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
	{
		ir.Write(IROp::FRound, fd, fs);
		break;
	}

	case 13: //FsI(fd) = Rto0(F(fs)));            break; //trunc.w.s
	{
		ir.Write(IROp::FTrunc, fd, fs);
		break;
	}

	case 14://FsI(fd) = (int)ceilf (F(fs));      break; //ceil.w.s
	{
		ir.Write(IROp::FCeil, fd, fs);
		break;
	}
	case 15: //FsI(fd) = (int)floorf(F(fs));      break; //floor.w.s
	{
		ir.Write(IROp::FFloor, fd, fs);
		break;
	}

	case 32: //F(fd)   = (float)FsI(fs);          break; //cvt.s.w
		ir.Write(IROp::FCvtSW, fd, fs);
		break;

	case 36: //FsI(fd) = (int)  F(fs);            break; //cvt.w.s
		ir.Write(IROp::FCvtWS, fd, fs);
		break;

	default:
		DISABLE;
	}
}

void IRJit::Comp_mxc1(MIPSOpcode op)
{
	CONDITIONAL_DISABLE;

	int fs = _FS;
	MIPSGPReg rt = _RT;

	switch ((op >> 21) & 0x1f) {
	case 0: // R(rt) = FI(fs); break; //mfc1
		if (rt == MIPS_REG_ZERO) {
			return;
		}
		ir.Write(IROp::FMovToGPR, rt, fs);
		return;

	case 2: //cfc1
		if (rt == MIPS_REG_ZERO) {
			return;
		}
		if (fs == 31) {
			DISABLE;
		}
		else if (fs == 0) {
			ir.Write(IROp::SetConst, rt, ir.AddConstant(MIPSState::FCR0_VALUE));
		} else {
			// Unsupported regs are always 0.
			ir.Write(IROp::SetConst, rt, ir.AddConstant(0));
		}
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		ir.Write(IROp::FMovFromGPR, fs, rt);
		return;

	case 6: //ctc1
		if (fs == 31) {
			// Set rounding mode
			DISABLE;
		} else {
			Comp_Generic(op);
		}
		return;
	default:
		DISABLE;
		break;
	}
}

}	// namespace MIPSComp
