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
 * @OSF_COPYRIGHT@
 */

#include <mach_kdb.h>
#include <mach_kdp.h>
#include <debug.h>
#include <cpus.h>
#include <kern/thread.h>
#include <kern/exception.h>
#include <kern/syscall_sw.h>
#include <kern/cpu_data.h>
#include <kern/debug.h>
#include <mach/thread_status.h>
#include <vm/vm_fault.h>
#include <vm/vm_kern.h> 	/* For kernel_map */
#include <ppc/misc_protos.h>
#include <ppc/trap.h>
#include <ppc/exception.h>
#include <ppc/proc_reg.h>	/* for SR_xxx definitions */
#include <ppc/pmap.h>
#include <ppc/mem.h>
#include <ppc/mappings.h>
#include <ppc/Firmware.h>
#include <ppc/low_trace.h>
#include <ppc/Diagnostics.h>
#include <ppc/hw_perfmon.h>

#include <sys/kdebug.h>

perfTrap perfTrapHook = 0;							/* Pointer to performance trap hook routine */

#if	MACH_KDB
#include <ddb/db_watch.h>
#include <ddb/db_run.h>
#include <ddb/db_break.h>
#include <ddb/db_trap.h>

boolean_t let_ddb_vm_fault = FALSE;
boolean_t	debug_all_traps_with_kdb = FALSE;
extern struct db_watchpoint *db_watchpoint_list;
extern boolean_t db_watchpoints_inserted;
extern boolean_t db_breakpoints_inserted;



#endif	/* MACH_KDB */

extern int debugger_active[NCPUS];
extern task_t bsd_init_task;
extern char init_task_failure_data[];
extern int not_in_kdp;

#define	PROT_EXEC	(VM_PROT_EXECUTE)
#define PROT_RO		(VM_PROT_READ)
#define PROT_RW		(VM_PROT_READ|VM_PROT_WRITE)

/* A useful macro to update the ppc_exception_state in the PCB
 * before calling doexception
 */
#define UPDATE_PPC_EXCEPTION_STATE {							\
	thread_act_t thr_act = current_act();						\
	thr_act->mact.pcb->save_dar = (uint64_t)dar;				\
	thr_act->mact.pcb->save_dsisr = dsisr;						\
	thr_act->mact.pcb->save_exception = trapno / T_VECTOR_SIZE;	/* back to powerpc */ \
}

static void unresolved_kernel_trap(int trapno,
				   struct savearea *ssp,
				   unsigned int dsisr,
				   addr64_t dar,
				   char *message);

static void handleMck(struct savearea *ssp);		/* Common machine check handler */


struct savearea *trap(int trapno,
			     struct savearea *ssp,
			     unsigned int dsisr,
			     addr64_t dar)
{
	int exception;
	int code;
	int subcode;
	vm_map_t map;
    unsigned int sp;
	unsigned int space, space2;
	unsigned int offset;
	thread_act_t thr_act;
	boolean_t intr;
	
#ifdef MACH_BSD
	time_value_t tv;
#endif /* MACH_BSD */

	if(perfTrapHook) {							/* Is there a hook? */
		if(perfTrapHook(trapno, ssp, dsisr, (unsigned int)dar) == KERN_SUCCESS) return ssp;	/* If it succeeds, we are done... */
	}

#if 0
	{
		extern void fctx_text(void);
		fctx_test();
	}
#endif

	thr_act = current_act();					/* Get current activation */
	exception = 0;								/* Clear exception for now */

/*
 *	Remember that we are disabled for interruptions when we come in here.  Because
 *	of latency concerns, we need to enable interruptions in the interrupted process
 *	was enabled itself as soon as we can.
 */

	intr = (ssp->save_srr1 & MASK(MSR_EE)) != 0;	/* Remember if we were enabled */

	/* Handle kernel traps first */

	if (!USER_MODE(ssp->save_srr1)) {
		/*
		 * Trap came from kernel
		 */
	      	switch (trapno) {

		case T_PREEMPT:			/* Handle a preempt trap */
			ast_taken(AST_PREEMPTION, FALSE);
			break;	

		case T_PERF_MON:
			perfmon_handle_pmi(ssp);
			break;

		case T_RESET:					/* Reset interruption */
			if (!Call_Debugger(trapno, ssp))
				unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			break;						/* We just ignore these */
		
		/*
		 * These trap types should never be seen by trap()
		 * in kernel mode, anyway.
		 * Some are interrupts that should be seen by
		 * interrupt() others just don't happen because they
		 * are handled elsewhere. Some could happen but are
		 * considered to be fatal in kernel mode.
		 */
		case T_DECREMENTER:
		case T_IN_VAIN:			/* Shouldn't ever see this, lowmem_vectors eats it */
		case T_SYSTEM_MANAGEMENT:
		case T_ALTIVEC_ASSIST:
		case T_INTERRUPT:
		case T_FP_UNAVAILABLE:
		case T_IO_ERROR:
		case T_RESERVED:
		default:
			unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			break;


/*
 *			Here we handle a machine check in the kernel
 */

		case T_MACHINE_CHECK:
			handleMck(ssp);						/* Common to both user and kernel */
			break;


		case T_ALIGNMENT:
/*
*			If enaNotifyEMb is set, we get here, and
*			we have actually already emulated the unaligned access.
*			All that we want to do here is to ignore the interrupt. This is to allow logging or
*			tracing of unaligned accesses.  
*/
			
			KERNEL_DEBUG_CONSTANT(
				MACHDBG_CODE(DBG_MACH_EXCP_ALNG, 0) | DBG_FUNC_NONE,
				(int)ssp->save_srr0 - 4, (int)dar, (int)dsisr, (int)ssp->save_lr, 0);
			break;

		case T_EMULATE:
/*
*			If enaNotifyEMb is set we get here, and
*			we have actually already emulated the instruction.
*			All that we want to do here is to ignore the interrupt. This is to allow logging or
*			tracing of emulated instructions.  
*/

			KERNEL_DEBUG_CONSTANT(
				MACHDBG_CODE(DBG_MACH_EXCP_EMUL, 0) | DBG_FUNC_NONE,
				(int)ssp->save_srr0 - 4, (int)((savearea_comm *)ssp)->save_misc2, (int)dsisr, (int)ssp->save_lr, 0);
			break;




			
		case T_TRACE:
		case T_RUNMODE_TRACE:
		case T_INSTRUCTION_BKPT:
			if (!Call_Debugger(trapno, ssp))
				unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			break;

		case T_PROGRAM:
			if (ssp->save_srr1 & MASK(SRR1_PRG_TRAP)) {
				if (!Call_Debugger(trapno, ssp))
					unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			} else {
				unresolved_kernel_trap(trapno, ssp, 
							dsisr, dar, NULL);
			}
			break;

		case T_DATA_ACCESS:
#if	MACH_KDB
			mp_disable_preemption();
			if (debug_mode
			    && debugger_active[cpu_number()]
			    && !let_ddb_vm_fault) {
				/*
				 * Force kdb to handle this one.
				 */
				kdb_trap(trapno, ssp);
			}
			mp_enable_preemption();
#endif	/* MACH_KDB */
			/* can we take this during normal panic dump operation? */
			if (debug_mode
			    && debugger_active[cpu_number()]
			    && !not_in_kdp) {
			        /* 
				 * Access fault while in kernel core dump.
				 */
			        kdp_dump_trap(trapno, ssp); 
			}


			if(ssp->save_dsisr & dsiInvMode) {			/* Did someone try to reserve cache inhibited? */
				panic("trap: disallowed access to cache inhibited memory - %016llX\n", dar);
			}

			if(intr) ml_set_interrupts_enabled(TRUE);	/* Enable if we were */
			
			if(((dar >> 28) < 0xE) | ((dar >> 28) > 0xF))  {	/* Is this a copy in/out? */
			
				offset = (unsigned int)dar;				/* Set the failing address */
				map = kernel_map;						/* No, this is a normal kernel access */
				
/*
 *	Note: Some ROM device drivers will access page 0 when they start.  The IOKit will 
 *	set a flag to tell us to ignore any access fault on page 0.  After the driver is
 *	opened, it will clear the flag.
 */
				if((0 == (offset & -PAGE_SIZE)) && 		/* Check for access of page 0 and */
				  ((thr_act->mact.specFlags) & ignoreZeroFault)) {	/* special case of ignoring page zero faults */
					ssp->save_srr0 += 4;				/* Point to next instruction */
					break;
				}

				code = vm_fault(map, trunc_page_32(offset),
						dsisr & MASK(DSISR_WRITE) ? PROT_RW : PROT_RO,
						FALSE, THREAD_UNINT, NULL, 0);

				if (code != KERN_SUCCESS) {
					unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
				} else { 
					ssp->save_hdr.save_flags |= SAVredrive;	/* Tell low-level to re-try fault */
					ssp->save_dsisr = (ssp->save_dsisr & 
						~((MASK(DSISR_NOEX) | MASK(DSISR_PROT)))) | MASK(DSISR_HASH);	/* Make sure this is marked as a miss */
				}
				break;
			}

			/* If we get here, the fault was due to a copyin/out */

			map = thr_act->map;
			
			offset = (unsigned int)(thr_act->mact.cioRelo + dar);	/* Compute the user space address */

			code = vm_fault(map, trunc_page_32(offset),
					dsisr & MASK(DSISR_WRITE) ? PROT_RW : PROT_RO,
					FALSE, THREAD_UNINT, NULL, 0);

			/* If we failed, there should be a recovery
			 * spot to rfi to.
			 */
			if (code != KERN_SUCCESS) {

				if (thr_act->thread->recover) {
				
					act_lock_thread(thr_act);
					ssp->save_srr0 = thr_act->thread->recover;
					thr_act->thread->recover =
							(vm_offset_t)NULL;
					act_unlock_thread(thr_act);
				} else {
					unresolved_kernel_trap(trapno, ssp, dsisr, dar, "copyin/out has no recovery point");
				}
			}
			else { 
				ssp->save_hdr.save_flags |= SAVredrive;	/* Tell low-level to re-try fault */
				ssp->save_dsisr = (ssp->save_dsisr & 
					~((MASK(DSISR_NOEX) | MASK(DSISR_PROT)))) | MASK(DSISR_HASH);	/* Make sure this is marked as a miss */
			}
			
			break;
			
		case T_INSTRUCTION_ACCESS:

#if	MACH_KDB
			if (debug_mode
			    && debugger_active[cpu_number()]
			    && !let_ddb_vm_fault) {
				/*
				 * Force kdb to handle this one.
				 */
				kdb_trap(trapno, ssp);
			}
#endif	/* MACH_KDB */

			/* Same as for data access, except fault type
			 * is PROT_EXEC and addr comes from srr0
			 */

			if(intr) ml_set_interrupts_enabled(TRUE);	/* Enable if we were */

			map = kernel_map;
			
			code = vm_fault(map, trunc_page_64(ssp->save_srr0),
					PROT_EXEC, FALSE, THREAD_UNINT, NULL, 0);

			if (code != KERN_SUCCESS) {
				unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			} else { 
				ssp->save_hdr.save_flags |= SAVredrive;	/* Tell low-level to re-try fault */
				ssp->save_srr1 = (ssp->save_srr1 & 
					~((unsigned long long)(MASK(DSISR_NOEX) | MASK(DSISR_PROT)))) | MASK(DSISR_HASH);		/* Make sure this is marked as a miss */
			}
			break;

		/* Usually shandler handles all the system calls, but the
		 * atomic thread switcher may throwup (via thandler) and
		 * have to pass it up to the exception handler.
		 */

		case T_SYSTEM_CALL:
			unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			break;

		case T_AST:
			unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			break;
		}
	} else {

		ml_set_interrupts_enabled(TRUE);	/* Processing for user state traps is always enabled */

#ifdef MACH_BSD
		{
		void get_procrustime(time_value_t *);

			get_procrustime(&tv);
		}
#endif /* MACH_BSD */

	
		/*
		 * Trap came from user task
		 */

	      	switch (trapno) {

		case T_PREEMPT:
			unresolved_kernel_trap(trapno, ssp, dsisr, dar, NULL);
			break;	

		case T_PERF_MON:
			perfmon_handle_pmi(ssp);
			break;

			/*
			 * These trap types should never be seen by trap()
			 * Some are interrupts that should be seen by
			 * interrupt() others just don't happen because they
			 * are handled elsewhere.
			 */
		case T_DECREMENTER:
		case T_IN_VAIN:								/* Shouldn't ever see this, lowmem_vectors eats it */
		case T_INTERRUPT:
		case T_FP_UNAVAILABLE:
		case T_SYSTEM_MANAGEMENT:
		case T_RESERVED:
		case T_IO_ERROR:
			
		default:

			ml_set_interrupts_enabled(FALSE);					/* Turn off interruptions */

			panic("Unexpected user state trap(cpu %d): 0x%08X DSISR=0x%08X DAR=0x%016llX PC=0x%016llX, MSR=0x%016llX\n",
			       cpu_number(), trapno, dsisr, dar, ssp->save_srr0, ssp->save_srr1);
			break;


/*
 *			Here we handle a machine check in user state
 */

		case T_MACHINE_CHECK:
			handleMck(ssp);						/* Common to both user and kernel */
			break;

		case T_RESET:
			ml_set_interrupts_enabled(FALSE);					/* Turn off interruptions */
			if (!Call_Debugger(trapno, ssp))
				panic("Unexpected Reset exception: srr0 = %016llx, srr1 = %016llx\n",
					ssp->save_srr0, ssp->save_srr1);
			break;						/* We just ignore these */

		case T_ALIGNMENT:
/*
*			If enaNotifyEMb is set, we get here, and
*			we have actually already emulated the unaligned access.
*			All that we want to do here is to ignore the interrupt. This is to allow logging or
*			tracing of unaligned accesses.  
*/
			
			KERNEL_DEBUG_CONSTANT(
				MACHDBG_CODE(DBG_MACH_EXCP_ALNG, 0) | DBG_FUNC_NONE,
				(int)ssp->save_srr0 - 4, (int)dar, (int)dsisr, (int)ssp->save_lr, 0);
			break;

		case T_EMULATE:
/*
*			If enaNotifyEMb is set we get here, and
*			we have actually already emulated the instruction.
*			All that we want to do here is to ignore the interrupt. This is to allow logging or
*			tracing of emulated instructions.  
*/

			KERNEL_DEBUG_CONSTANT(
				MACHDBG_CODE(DBG_MACH_EXCP_EMUL, 0) | DBG_FUNC_NONE,
				(int)ssp->save_srr0 - 4, (int)((savearea_comm *)ssp)->save_misc2, (int)dsisr, (int)ssp->save_lr, 0);
			break;

		case T_TRACE:			/* Real PPC chips */
		  if (be_tracing()) {
		    add_pcbuffer();
		    return ssp;
		  }
		  /* fall through */

		case T_INSTRUCTION_BKPT:
			exception = EXC_BREAKPOINT;
			code = EXC_PPC_TRACE;
			subcode = (unsigned int)ssp->save_srr0;
			break;

		case T_PROGRAM:
			if (ssp->save_srr1 & MASK(SRR1_PRG_FE)) {
				fpu_save(thr_act->mact.curctx);
				UPDATE_PPC_EXCEPTION_STATE;
				exception = EXC_ARITHMETIC;
				code = EXC_ARITHMETIC;
			
				mp_disable_preemption();
				subcode = ssp->save_fpscr;
				mp_enable_preemption();
			} 	
			else if (ssp->save_srr1 & MASK(SRR1_PRG_ILL_INS)) {
				
				UPDATE_PPC_EXCEPTION_STATE
				exception = EXC_BAD_INSTRUCTION;
				code = EXC_PPC_UNIPL_INST;
				subcode = (unsigned int)ssp->save_srr0;
			} else if ((unsigned int)ssp->save_srr1 & MASK(SRR1_PRG_PRV_INS)) {

				UPDATE_PPC_EXCEPTION_STATE;
				exception = EXC_BAD_INSTRUCTION;
				code = EXC_PPC_PRIVINST;
				subcode = (unsigned int)ssp->save_srr0;
			} else if (ssp->save_srr1 & MASK(SRR1_PRG_TRAP)) {
				unsigned int inst;
				char *iaddr;
				
				iaddr = CAST_DOWN(char *, ssp->save_srr0);		/* Trim from long long and make a char pointer */ 
				if (copyin(iaddr, (char *) &inst, 4 )) panic("copyin failed\n");
				
				if(dgWork.dgFlags & enaDiagTrap) {	/* Is the diagnostic trap enabled? */
					if((inst & 0xFFFFFFF0) == 0x0FFFFFF0) {	/* Is this a TWI 31,R31,0xFFFx? */
						if(diagTrap(ssp, inst & 0xF)) {	/* Call the trap code */
							ssp->save_srr0 += 4ULL;	/* If we eat the trap, bump pc */
							exception = 0;			/* Clear exception */
							break;					/* All done here */
						}
					}
				}
				
				UPDATE_PPC_EXCEPTION_STATE;
				
				if (inst == 0x7FE00008) {
					exception = EXC_BREAKPOINT;
					code = EXC_PPC_BREAKPOINT;
				} else {
					exception = EXC_SOFTWARE;
					code = EXC_PPC_TRAP;
				}
				subcode = (unsigned int)ssp->save_srr0;
			}
			break;
			
		case T_ALTIVEC_ASSIST:
			UPDATE_PPC_EXCEPTION_STATE;
			exception = EXC_ARITHMETIC;
			code = EXC_PPC_ALTIVECASSIST;
			subcode = (unsigned int)ssp->save_srr0;
			break;

		case T_DATA_ACCESS:
			map = thr_act->map;

			if(ssp->save_dsisr & dsiInvMode) {			/* Did someone try to reserve cache inhibited? */
				UPDATE_PPC_EXCEPTION_STATE;				/* Don't even bother VM with this one */
				exception = EXC_BAD_ACCESS;
				subcode = (unsigned int)dar;
				break;
			}
			
			code = vm_fault(map, trunc_page_64(dar),
				 dsisr & MASK(DSISR_WRITE) ? PROT_RW : PROT_RO,
				 FALSE, THREAD_ABORTSAFE, NULL, 0);

			if ((code != KERN_SUCCESS) && (code != KERN_ABORTED)) {
				UPDATE_PPC_EXCEPTION_STATE;
				exception = EXC_BAD_ACCESS;
				subcode = (unsigned int)dar;
			} else { 
				ssp->save_hdr.save_flags |= SAVredrive;	/* Tell low-level to re-try fault */
				ssp->save_dsisr = (ssp->save_dsisr & 
					~((MASK(DSISR_NOEX) | MASK(DSISR_PROT)))) | MASK(DSISR_HASH);	/* Make sure this is marked as a miss */
			}
			break;
			
		case T_INSTRUCTION_ACCESS:
			/* Same as for data access, except fault type
			 * is PROT_EXEC and addr comes from srr0
			 */
			map = thr_act->map;
			
			code = vm_fault(map, trunc_page_64(ssp->save_srr0),
					PROT_EXEC, FALSE, THREAD_ABORTSAFE, NULL, 0);

			if ((code != KERN_SUCCESS) && (code != KERN_ABORTED)) {
				UPDATE_PPC_EXCEPTION_STATE;
				exception = EXC_BAD_ACCESS;
				subcode = (unsigned int)ssp->save_srr0;
			} else { 
				ssp->save_hdr.save_flags |= SAVredrive;	/* Tell low-level to re-try fault */
				ssp->save_srr1 = (ssp->save_srr1 & 
					~((unsigned long long)(MASK(DSISR_NOEX) | MASK(DSISR_PROT)))) | MASK(DSISR_HASH);		/* Make sure this is marked as a miss */
			}
			break;

		case T_AST:
			ml_set_interrupts_enabled(FALSE);
			ast_taken(AST_ALL, intr);
			break;
			
		}
#ifdef MACH_BSD
		{
		void bsd_uprofil(time_value_t *, unsigned int);

		bsd_uprofil(&tv, ssp->save_srr0);
		}
#endif /* MACH_BSD */
	}

	if (exception) {
		/* if this is the init task, save the exception information */
		/* this probably is a fatal exception */
#if 0
		if(bsd_init_task == current_task()) {
			char *buf;
        		int i;

			buf = init_task_failure_data;


			buf += sprintf(buf, "Exception Code = 0x%x, Subcode = 0x%x\n", code, subcode);
			buf += sprintf(buf, "DSISR = 0x%08x, DAR = 0x%016llx\n"
								, dsisr, dar);

			for (i=0; i<32; i++) {
		       		if ((i % 8) == 0) {
					buf += sprintf(buf, "\n%4d :",i);
				}
				buf += sprintf(buf, " %08x",*(&ssp->save_r0+i));
			}

        		buf += sprintf(buf, "\n\n");
        		buf += sprintf(buf, "cr        = 0x%08X\t\t",ssp->save_cr);
        		buf += sprintf(buf, "xer       = 0x%08X\n",ssp->save_xer);
        		buf += sprintf(buf, "lr        = 0x%016llX\t\t",ssp->save_lr);
        		buf += sprintf(buf, "ctr       = 0x%016llX\n",ssp->save_ctr); 
        		buf += sprintf(buf, "srr0(iar) = 0x%016llX\t\t",ssp->save_srr0);
        		buf += sprintf(buf, "srr1(msr) = 0x%016llX\n",ssp->save_srr1,
                   	   "\x10\x11""EE\x12PR\x13""FP\x14ME\x15""FE0\x16SE\x18"
                    	   "FE1\x19""AL\x1a""EP\x1bIT\x1c""DT");
        		buf += sprintf(buf, "\n\n");

        		/* generate some stack trace */
        		buf += sprintf(buf, "Application level back trace:\n");
        		if (ssp->save_srr1 & MASK(MSR_PR)) { 
                	   char *addr = (char*)ssp->save_r1;
                	   unsigned int stack_buf[3];
                	   for (i = 0; i < 8; i++) {
                        	if (addr == (char*)NULL)
                               		break;
                        	if (!copyin(addr,(char*)stack_buf, 
							3 * sizeof(int))) {
                               		buf += sprintf(buf, "0x%08X : 0x%08X\n"
						,addr,stack_buf[2]);
                               		addr = (char*)stack_buf[0];
                        	} else {
                               		break;
                       	   	}
                	   }
        		}
			buf[0] = '\0';
		}
#endif
		doexception(exception, code, subcode);
	}
	/* AST delivery
	 * Check to see if we need an AST, if so take care of it here
	 */
	ml_set_interrupts_enabled(FALSE);
	if (USER_MODE(ssp->save_srr1))
		while (ast_needed(cpu_number())) {
			ast_taken(AST_ALL, intr);
			ml_set_interrupts_enabled(FALSE);
		}

	return ssp;
}

/* This routine is called from assembly before each and every system call.
 * It must preserve r3.
 */

extern int syscall_trace(int, struct savearea *);


extern int pmdebug;

int syscall_trace(int retval, struct savearea *ssp)
{
	int i, argc;
	int kdarg[3];
/* Always prepare to trace mach system calls */

	kdarg[0]=0;
	kdarg[1]=0;
	kdarg[2]=0;
	
	argc = mach_trap_table[-((unsigned int)ssp->save_r0)].mach_trap_arg_count;
	
	if (argc > 3)
		argc = 3;
	
	for (i=0; i < argc; i++)
		kdarg[i] = (int)*(&ssp->save_r3 + i);
	
	KERNEL_DEBUG_CONSTANT(MACHDBG_CODE(DBG_MACH_EXCP_SC, (-(ssp->save_r0))) | DBG_FUNC_START,
		kdarg[0], kdarg[1], kdarg[2], 0, 0);

	return retval;
}

/* This routine is called from assembly after each mach system call
 * It must preserve r3.
 */

extern int syscall_trace_end(int, struct savearea *);

int syscall_trace_end(int retval, struct savearea *ssp)
{
	KERNEL_DEBUG_CONSTANT(MACHDBG_CODE(DBG_MACH_EXCP_SC,(-((unsigned int)ssp->save_r0))) | DBG_FUNC_END,
		retval, 0, 0, 0, 0);
	return retval;
}

/*
 * called from syscall if there is an error
 */

int syscall_error(
	int exception,
	int code,
	int subcode,
	struct savearea *ssp)
{
	register thread_t thread;

	thread = current_thread();

	if (thread == 0)
	    panic("syscall error in boot phase");

	if (!USER_MODE(ssp->save_srr1))
		panic("system call called from kernel");

	doexception(exception, code, subcode);

	return 0;
}

/* Pass up a server syscall/exception */
void
doexception(
	    int exc,
	    int code,
	    int sub)
{
	exception_data_type_t   codes[EXCEPTION_CODE_MAX];

	codes[0] = code;	
	codes[1] = sub;
	exception(exc, codes, 2);
}

char *trap_type[] = {
	"Unknown",
	"0x100 - System reset",
	"0x200 - Machine check",
	"0x300 - Data access",
	"0x400 - Inst access",
	"0x500 - Ext int",
	"0x600 - Alignment",
	"0x700 - Program",
	"0x800 - Floating point",
	"0x900 - Decrementer",
	"0xA00 - n/a",
	"0xB00 - n/a",
	"0xC00 - System call",
	"0xD00 - Trace",
	"0xE00 - FP assist",
	"0xF00 - Perf mon",
	"0xF20 - VMX",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"0x1300 - Inst bkpnt",
	"0x1400 - Sys mgmt",
	"0x1600 - Altivec Assist",
	"0x1700 - Thermal",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"INVALID EXCEPTION",
	"Emulate",
	"0x2000 - Run Mode/Trace",
	"Signal Processor",
	"Preemption",
	"Context Switch",
	"Shutdown",
	"System Failure"
};
int TRAP_TYPES = sizeof (trap_type) / sizeof (trap_type[0]);

void unresolved_kernel_trap(int trapno,
			    struct savearea *ssp,
			    unsigned int dsisr,
			    addr64_t dar,
			    char *message)
{
	char *trap_name;
	extern void print_backtrace(struct savearea *);
	extern unsigned int debug_mode, disableDebugOuput;

	ml_set_interrupts_enabled(FALSE);					/* Turn off interruptions */
	lastTrace = LLTraceSet(0);							/* Disable low-level tracing */

	if( logPanicDataToScreen )
		disableDebugOuput = FALSE;
	
	debug_mode++;
	if ((unsigned)trapno <= T_MAX)
		trap_name = trap_type[trapno / T_VECTOR_SIZE];
	else
		trap_name = "???? unrecognized exception";
	if (message == NULL)
		message = trap_name;

	kdb_printf("\n\nUnresolved kernel trap(cpu %d): %s DAR=0x%016llX PC=0x%016llX\n",
	       cpu_number(), trap_name, dar, ssp->save_srr0);

	print_backtrace(ssp);

	draw_panic_dialog();
		
	if( panicDebugging )
		(void *)Call_Debugger(trapno, ssp);
	panic(message);
}

char *corr[2] = {"uncorrected", "corrected  "};

void handleMck(struct savearea *ssp) {					/* Common machine check handler */

	int cpu;
	
	cpu = cpu_number();

	printf("Machine check (%d) - %s - pc = %016llX, msr = %016llX, dsisr = %08X, dar = %016llX\n",
		cpu, corr[ssp->save_hdr.save_misc3], ssp->save_srr0, ssp->save_srr1, ssp->save_dsisr, ssp->save_dar);		/* Tell us about it */
	printf("Machine check (%d) -   AsyncSrc = %016llX, CoreFIR = %016llx\n", cpu, ssp->save_xdat0, ssp->save_xdat1);
	printf("Machine check (%d) -      L2FIR = %016llX,  BusFir = %016llx\n", cpu, ssp->save_xdat2, ssp->save_xdat3);
	
	if(ssp->save_hdr.save_misc3) return;				/* Leave the the machine check was recovered */

	panic("Uncorrectable machine check: pc = %016llX, msr = %016llX, dsisr = %08X, dar = %016llX\n"
	      "  AsyncSrc = %016llX, CoreFIR = %016llx\n"
	      "     L2FIR = %016llX,  BusFir = %016llx\n",
		  ssp->save_srr0, ssp->save_srr1, ssp->save_dsisr, ssp->save_dar, 
		  ssp->save_xdat0, ssp->save_xdat1, ssp->save_xdat2, ssp->save_xdat3);
	
	return;
}

void
thread_syscall_return(
        kern_return_t ret)
{
        register thread_act_t   thr_act = current_act();
        register struct savearea *regs = USER_REGS(thr_act);

	if (kdebug_enable && ((unsigned int)regs->save_r0 & 0x80000000)) {
	  /* Mach trap */
	  KERNEL_DEBUG_CONSTANT(MACHDBG_CODE(DBG_MACH_EXCP_SC,(-(regs->save_r0))) | DBG_FUNC_END,
		       ret, 0, 0, 0, 0);
	}	    
        regs->save_r3 = ret;

        thread_exception_return();
        /*NOTREACHED*/
}


#if	MACH_KDB
void
thread_kdb_return(void)
{
	register thread_act_t	thr_act = current_act();
	register thread_t	cur_thr = current_thread();
	register struct savearea *regs = USER_REGS(thr_act);

	Call_Debugger(thr_act->mact.pcb->save_exception, regs);
#if		MACH_LDEBUG
	assert(cur_thr->mutex_count == 0); 
#endif		/* MACH_LDEBUG */
	check_simple_locks();
	thread_exception_return();
	/*NOTREACHED*/
}
#endif	/* MACH_KDB */
