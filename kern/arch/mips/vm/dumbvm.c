/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

#include "opt-A3.h"
#include <array.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if OPT_A3 // managing Memory
static struct spinlock coremap_stealmem_lock = SPINLOCK_INITIALIZER;
static int* coremap;
static paddr_t coremap_firstaddr = 0;
static paddr_t coremap_lastaddr = 0;
static bool bootstrap = false;
#endif


void
vm_bootstrap(void)
{
#if OPT_A3 // Managing Memory

	// call ram_getsize to get the remaining physical memory in the system
	ram_getsize(&coremap_firstaddr, &coremap_lastaddr);
    
	// calculate number of pages needs for coremap
	int valid_page_num = (coremap_lastaddr - coremap_firstaddr) / PAGE_SIZE; // unsisgned

    // get the ptr to coremap
	coremap = (int*) PADDR_TO_KVADDR(coremap_firstaddr);

    // initialize the coremap: every page is not use
	for (int i = 0; i < valid_page_num; i++) {
		coremap[i] = 0;
  	}
	
	// update bool 
	bootstrap = true;
#endif
}


#if OPT_A3 // Managing Memory 

// change the contents of coremap from unused pages to continuous used pages
static void write_to_coremap(unsigned long npages, int start_idx) {
	for (unsigned long j = 1; j <= npages; j++) {
		coremap[start_idx + j - 1] = (int) j;
    }
}


// return number of unused page start from strat_idx (exluded start_idx) 
static int count_unused(int start_idx){
	int count = 0;
	int valid_page_num = (coremap_lastaddr - coremap_firstaddr) / PAGE_SIZE;
	for (int i = start_idx + 1; i <  valid_page_num; i++) {
		if (coremap[i] == 0) {
			count += 1;
		} else {
			return count;
		}
	}
	return count;
}


static
paddr_t
coremap_stealmem(unsigned long npages)
{
	// calculate number of pages needs for coremap
  	int valid_page_num = (coremap_lastaddr - coremap_firstaddr) / PAGE_SIZE;

	// loop through the coremap 
	for (int i = 0; i < valid_page_num; i++) {
    	int curr = coremap[i];
		unsigned long count = 0;

		if (curr == 0) { // current page is unused
			count +=  1;
			count += count_unused(i);
			if (count >= npages) {
				write_to_coremap(npages, i);
				paddr_t size = (i + 1) * PAGE_SIZE;
				return (coremap_firstaddr + size);
			} else { // current page is used
				continue;
			}
		}
  }
  return 0;
}
#endif





static
paddr_t
getppages(unsigned long npages) 
{

#if OPT_A3 // Managing Memory 
    paddr_t addr;
	if (bootstrap) { // alloactes memory with providing coremap to release pages
   		spinlock_acquire(&coremap_stealmem_lock);
      	addr = coremap_stealmem(npages);
    	spinlock_release(&coremap_stealmem_lock);		
	} else { // alloactes memory without providing any mechanism to release pages
		spinlock_acquire(&stealmem_lock);
      	addr = ram_stealmem(npages); 
    	spinlock_release(&stealmem_lock);
	}
    return addr;
#else
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	
	spinlock_release(&stealmem_lock);
	return addr;
#endif
}


/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}



void 
free_kpages(vaddr_t addr)
{

#if OPT_A3 // Managing Memory 
	paddr_t p_addr = KVADDR_TO_PADDR(addr);

	int valid_page_num = (coremap_lastaddr - coremap_firstaddr) / PAGE_SIZE;
	
	int count = (p_addr - coremap_firstaddr) / PAGE_SIZE;

	spinlock_acquire(&coremap_stealmem_lock);

	for (int i = count - 1; i < valid_page_num; i++) {
		int curr = coremap[i];
		if ((i == count - 1) || (curr > 1)) {
			coremap[i] = 0; // free
		} else { // curr == 0
			break;
		}
	}

  	spinlock_release(&coremap_stealmem_lock);
#else
	/* nothing - leak the memory. */
	(void)addr;
#endif
}


void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
#if OPT_A3 // read-only text seg
		return EFAULT;
		//kill_curthread()
#else
		panic("dumbvm: got VM_FAULT_READONLY\n");
#endif
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
#if OPT_A3 // Page Tables
	KASSERT(as->as_vbase1 != 0);
	//KASSERT(as->as_pbase1 != NULL); 
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	//KASSERT(as->as_pbase2 != NULL);
	KASSERT(as->as_npages2 != 0);
	//KASSERT(as->as_stackpbase != NULL); 
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
#else
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);
#endif

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

#if OPT_A3 // Read-only Text Seg
	bool text_segment = false;
#endif

	// Page Tables
	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1[0];
				text_segment = true;
	
#if OPT_A3 // Read-only Text Seg
		text_segment = true;
#endif

	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2[0]; 
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase[0];
	}

	
	else {
		return EFAULT;
	}


	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
#if OPT_A3 // Read-only Text Seg
		if (as->as_loadelf_complete && text_segment) {
			elo &= ~TLBLO_DIRTY;
		}
#endif
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

#if OPT_A3 //TLB Replacement//
	// TLB is full, call tlb_random to write entry into a random TLB slot
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	if (as->as_loadelf_complete && text_segment) { // Read-only Text Seg
		elo &= ~TLBLO_DIRTY;
	}

	tlb_random(ehi, elo);
	splx(spl);
	return 0;
#else 

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
	#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

#if OPT_A3 // Page Tables
	as->as_vbase1 = 0;
	as->as_pbase1 = NULL;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = NULL;
	as->as_npages2 = 0;
	as->as_stackpbase = NULL;
#else
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
#endif

#if OPT_A3 // Read-only Text Seg
	as->as_loadelf_complete = false;
# endif

	return as;
}

void
as_destroy(struct addrspace *as)
{

#if OPT_A3 // Page Tables

	// call free_kpages on the frames for each segment
	int np1 = (int) as->as_npages1;
	int np2 = (int) as->as_npages2;
	for (int i = 0; i < np1; i++) {
		vaddr_t addr = PADDR_TO_KVADDR(as->as_pbase1[i]);
		free_kpages(addr);
	}
  	for (int i = 0; i < np2; i++) {
		vaddr_t addr = PADDR_TO_KVADDR(as->as_pbase2[i]);
		free_kpages(addr);
  	}
	
	// kfree the page tables
  	for (int i = 0; i < DUMBVM_STACKPAGES; i++) {
		vaddr_t addr = PADDR_TO_KVADDR(as->as_stackpbase[i]);
		free_kpages(addr);
  	}

	kfree(as);
#else
	kfree(as);
#endif
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;

#if OPT_A3 // Page Tables
		as->as_pbase1 = kmalloc( sizeof(paddr_t) * npages);
#endif

		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;

#if OPT_A3 // Page Tables
	   as->as_pbase2 = kmalloc(sizeof(paddr_t) * npages);

	   if (as->as_pbase2 == NULL) {
		   panic("cannot use alloc mem");
	   }
#endif
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	
#if OPT_A3 // Page Tables

	// pre-allocate frames for each page in the segment 
	// allocate each frame one at a time
	int np1 = (int) as->as_npages1;
	int np2 = (int) as->as_npages2;
	unsigned long one = 1;

	for (int i = 0; i < np1; i++) {
    	as->as_pbase1[i] = getppages(one); 
    	if (as->as_pbase1[i] == 0) { // error check
			return ENOMEM;
		}
		as_zero_region(as->as_pbase1[i], 1);
  	}
	for (int i = 0; i < np2; i++) {
    	as->as_pbase2[i] = getppages(one);
		if (as->as_pbase2[i] == 0) { // error check
			return ENOMEM;
		}
    	as_zero_region(as->as_pbase2[i], one);
  	}

	// create a page table for the stack
	as->as_stackpbase = kmalloc(sizeof(paddr_t) * DUMBVM_STACKPAGES);
	if (as->as_stackpbase == NULL) { // error check
		return ENOMEM;
	}

  	for (unsigned int i = 0; i < DUMBVM_STACKPAGES; i++) {
    	as->as_stackpbase[i] = getppages(one);
		if (as->as_stackpbase[i] == 0) { // error check
			return ENOMEM;
		}
    	as_zero_region(as->as_stackpbase[i], 1);
  	}
  	
	return 0;
#else
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
#endif
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
#if OPT_A3 // page table
	//KASSERT(as->as_stackpbase != 0);
	(void)as;
#else
	KASSERT(as->as_stackpbase != 0);
#endif

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{

	// create the address space
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	// create segments based on old address space
	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

#if OPT_A3 // Page Tables
	// allocate frames for the segments
  	new->as_pbase1 = kmalloc(sizeof(paddr_t) * old->as_npages1);
  	new->as_pbase2 = kmalloc(sizeof(paddr_t) * old->as_npages2);
  	new->as_stackpbase = kmalloc(sizeof(paddr_t) * DUMBVM_STACKPAGES);
#endif

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

#if OPT_A3 // Page Tables
	// memcopy frames from the old address space to the frames new address space
	int np1 = old->as_npages1;
	int np2 =  old->as_npages2;

  	for (int i = 0; i < np1; i++) {
		memmove((void *)PADDR_TO_KVADDR(new->as_pbase1[i]),
      	(const void *)PADDR_TO_KVADDR(old->as_pbase1[i]),PAGE_SIZE);
  	}
  	for (int i = 0; i < np2; i++) {
    	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2[i]),
      	(const void *)PADDR_TO_KVADDR(old->as_pbase2[i]),PAGE_SIZE);
  	}
  	for (int i = 0; i < DUMBVM_STACKPAGES; i++) {
    	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase[i]),

      	(const void *)PADDR_TO_KVADDR(old->as_stackpbase[i]),PAGE_SIZE);
  	}
#else
	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
#endif

	*ret = new;
	return 0;
}
