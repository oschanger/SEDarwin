/*
 * Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *		This module only exists because I don't know how to get the silly C compiler
 *		and/or linker to generate data areas that are aligned on a particular boundary.
 *		And, this stuff is in the V=R mapped area.
 *
 *		Do the following for each:
 *
 *				.size	name,size-in-bytes
 *				.type	area-name,@object
 *				.globl	area-name
 *				.align 	power-of-two
 *		area-name:
 *				.set	.,.+size-in-bytes
 *
 *		So long as I'm being pedantic, always make sure that the most aligned,
 *		i.e., the largest power-of-twos, are first and then descend to the smallest.
 *		If you don't, and you are not careful and hand calculate, you'll end up
 *		with holes and waste storage.  I hate C.
 *
 *		Define the sizes in genassym.c
 */
 
		
#include <debug.h>
#include <cpus.h>
#include <ppc/asm.h>
#include <ppc/proc_reg.h>
#include <ppc/spec_reg.h>
#include <mach/ppc/vm_param.h>
#include <assym.s>

;
;		NOTE: We need this only if PREEMPTSTACK is set to non-zero in hw_lock.
;		Make sure they are set to the same thing
;
#define PREEMPTSTACK 0

			.data

/*		4096-byte aligned areas */

		.globl	EXT(per_proc_info)
		.align	12
EXT(per_proc_info):									; Per processor data area
		.space	(ppSize*NCPUS),0				; (filled with 0s)

/*		512-byte aligned areas */

		.globl	EXT(kernel_pmap_store)				; This is the kernel_pmap
		.align	8
EXT(kernel_pmap_store):
		.set	.,.+pmapSize


/*		256-byte aligned areas */

		.globl	EXT(GratefulDebWork)
		.align	8
EXT(GratefulDebWork):								; Enough for 2 rows of 8 chars of 16-pixel wide 32-bit pixels and a 256 byte work area
		.set	.,.+2560

		.globl	debstash
		.align	8
debstash:
		.set	.,.+256

#if PREEMPTSTACK

;
;		NOTE: We need this only if PREEMPTSTACK is set to non-zero in hw_lock.
;

		.globl	EXT(DBGpreempt)						; preemption debug stack
		.align	8
EXT(DBGpreempt):
		.set	.,.+(NCPUS*PREEMPTSTACK*16)
#endif


/*		128-byte aligned areas */

		.globl	EXT(mapCtl)
		.align	7
EXT(mapCtl):
		.set	.,.+mapcsize

		.globl	fwdisplock
		.align	7
fwdisplock:
		.set	.,.+128

		.globl	EXT(free_mappings)
		.align	7
	
EXT(free_mappings):
		.long	0

		.globl	EXT(syncClkSpot)
		.align	7
EXT(syncClkSpot):
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
	
		.globl	EXT(NMIss)
		.align	7
EXT(NMIss):
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0
		.long	0

/*		32-byte aligned areas */

		.globl	EXT(dbvecs)
		.align	5
EXT(dbvecs):
		.set	.,.+(33*16)

		.globl	hexfont
		.align	5
#include <ppc/hexfont.h>

    	.globl  EXT(QNaNbarbarian)
		.align	5

EXT(QNaNbarbarian):
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
	
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */
		.long	0x7FFFDEAD							/* This is a quiet not-a-number which is a "known" debug value */

/*		8-byte aligned areas */

    	.globl  EXT(FloatInit)
		.align	3

EXT(FloatInit):
		.long	0xC24BC195							/* Initial value */
		.long	0x87859393							/* of floating point registers */
		.long	0xE681A2C8							/* and others */
		.long	0x8599855A

		.globl  EXT(DebugWork)
		.align	3

EXT(DebugWork):
		.long	0
		.long	0
		.long	0
		.long	0

    	.globl  EXT(dbfloats)
		.align	3
EXT(dbfloats):
		.set	.,.+(33*8)

		.globl  EXT(dbspecrs)
		.align	3
EXT(dbspecrs):
		.set	.,.+(80*4)

/*
 *		Interrupt and debug stacks go here
 */
 	
		.align  PPC_PGSHIFT
     	.globl  EXT(FixedStackStart)
EXT(FixedStackStart):
     
	 	.globl  EXT(intstack)
EXT(intstack):
		.set	.,.+INTSTACK_SIZE*NCPUS
	
/* Debugger stack - used by the debugger if present */
/* NOTE!!! Keep the debugger stack right after the interrupt stack */

    	.globl  EXT(debstack)
EXT(debstack):
		.set	., .+KERNEL_STACK_SIZE*NCPUS
     
		 .globl  EXT(FixedStackEnd)
EXT(FixedStackEnd):

		.align	ALIGN
   		.globl  EXT(intstack_top_ss)
EXT(intstack_top_ss):
		.long	EXT(intstack)+INTSTACK_SIZE-FM_SIZE			/* intstack_top_ss points to the top of interrupt stack */

		.align	ALIGN
   	 	.globl  EXT(debstack_top_ss)	
EXT(debstack_top_ss):

		.long	EXT(debstack)+KERNEL_STACK_SIZE-FM_SIZE		/* debstack_top_ss points to the top of debug stack */

    	.globl  EXT(debstackptr)
EXT(debstackptr):	
		.long	EXT(debstack)+KERNEL_STACK_SIZE-FM_SIZE

