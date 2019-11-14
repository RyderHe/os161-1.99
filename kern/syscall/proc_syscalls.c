#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>

#include "opt-A2.h"
#include <array.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <limits.h>
#include <vfs.h>
#include <kern/fcntl.h>

#include <test.h>


#if OPT_A2

int
sys_fork(struct trapframe *tf, pid_t *retval)
{
  KASSERT(curproc != NULL);
  KASSERT(lk != NULL);
  KASSERT(pid_counter >= PID_MIN);

  if (pid_counter > PID_MAX) { // check pid_counter
    return ENPROC;
  } 
  
  // 1. create process structure for child process
  struct proc *new_proc = proc_create_runprogram(curproc->p_name);
  if (new_proc == NULL) { //error check
    panic("sys_fork cannot create a new process");
    return ENOMEM;
  }

  // 2. create and copy address space
  lock_acquire(lk);
  int err1 = as_copy(curproc_getas(), &(new_proc->p_addrspace));
  lock_release(lk);
  if (err1 == 1) { // error check
    panic("sys_fork cannot copy adress space from parent to child");
    proc_destroy(new_proc);
    return ENOMEM;
  }  

  // 3. assign PID to child process
  lock_acquire(lk);
  struct child *curchild = kmalloc(sizeof(struct child)); 
  if (curchild == NULL) { // error check
    panic("sys_fork cannot create child structure");
    as_destroy(new_proc->p_addrspace);
    proc_destroy(new_proc);
  }
  curchild->exit = false;
  curchild->exit_code = 0;
  curchild->pid = new_proc->pid; 
  curchild->location = new_proc;
  lock_release(lk);

  // create the parent/child relationship
  lock_acquire(lk);
  array_add(curproc->children, curchild, NULL);
  new_proc->parent = curproc; 
  lock_release(lk);

  // 4. create new trap frame for child and deep copy from parent
  lock_acquire(lk);
  struct trapframe *new_trapframe = kmalloc(sizeof(struct trapframe));
  lock_release(lk);
  if (new_trapframe == NULL) { // error check
    panic("sys_fork cannot create new trap frame");
    as_destroy(new_proc->p_addrspace);
    proc_destroy(new_proc);
    kfree(curchild); 
  }
  lock_acquire(lk);
  memcpy(new_trapframe, tf, sizeof(struct trapframe));
  lock_release(lk);

  // 5. create thread for child process
  lock_acquire(lk);
  int err2 = thread_fork(curthread->t_name, new_proc, (void *)&enter_forked_process, new_trapframe, 0);
  lock_release(lk);
  if (err2 == 1) { // error check
    panic("sys_fork cannot create thread for child process");
    as_destroy(new_proc->p_addrspace);
    proc_destroy(new_proc);
    kfree(curchild); 
    kfree(new_trapframe);
    return ENOMEM; 
  }

  // update return value to return child pid
  *retval = new_proc->pid;

  return 0;
}
#endif





  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
//kprintf("into exit\n");
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

#if OPT_A2
  KASSERT(curproc != NULL);
  KASSERT(pid_counter >= PID_MIN);
  KASSERT(lk != NULL);

  // 1. notify children
  unsigned int size = array_num(p->children);
  lock_acquire(lk);
  for (unsigned int i = 0; i < size; i++) {
      struct child *curchild = array_get(p->children, i);
      //lock_acquire(lk);
      if (curchild->pid == p->pid) {  
        curchild->location->parent = NULL;
      }
      //lock_release(lk);
  }
  lock_release(lk);

  // 2. notify parent 
  if (p->parent != NULL) { // parent exist
    size = array_num(p->parent->children);
    lock_acquire(lk);
    for (unsigned int i = 0; i < size; i++) {
      struct child *curchild = array_get(p->parent->children, i);
    //  lock_acquire(lk);
      if (curchild->pid == p->pid) { //update 
        curchild->exit = true;
        curchild->exit_code = exitcode;
        break; 
      }
     // lock_release(lk);
    }
    lock_release(lk);

    // broadcast waiting processes
    lock_acquire(lk);
    cv_broadcast(p->child_cv, lk);
    lock_release(lk);
  }
 
#else
  (void)exitcode;
#endif

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p); 

  thread_exit();
  
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  
#if OPT_A2
  KASSERT(curproc != NULL);
  KASSERT(lk != NULL);
  KASSERT(pid_counter >= PID_MIN);

  *retval = curproc->pid;
#else
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

 
#if OPT_A2
  KASSERT(curproc != NULL);
  KASSERT(pid_counter >= PID_MIN);
  KASSERT(lk != NULL);
  
  if (options != 0) {
    return(EINVAL);
  }
  
  if(status == NULL){
    //*retval = -1;
    return(EFAULT);
  }

  // check not wait for myself
  if (curproc->pid == pid) {
    panic("sys_waitpid cannot wait for myself");
    return(EINVAL); 
  }
  // check wait for nonexited process
  if ((pid > PID_MAX) || (pid < PID_MIN)) {
    panic("sys_waitpid cannot wait for a nonexistent process");
    return(ESRCH);
  } 
  
  // find the child it waits for
  bool is_child = true;
  int index = 0;
  struct child *this_child = NULL; 
  for (unsigned int i = 0; i < array_num(curproc->children); i++) {
    struct child *curchild = array_get(curproc->children, i); ////////////////////
    if (curchild->pid == pid) { 
      is_child = true;
      index = i;  
      this_child = curchild;
      break; 
    }
  }

  // check not wait for non-child
  if (is_child == false) {
    panic("sys_waitpid cannot wait for the process that is not curproc's child");
    return(ECHILD);
  }
  
  //lock_acquire(lk);
  if (this_child->exit == true) { // child has already exited => no need to wait
    exitstatus = _MKWAIT_EXIT(this_child->exit_code);
  } else { // child has not exited => need to wait
    lock_acquire(lk);
    while (this_child->exit == false) {
      cv_wait(this_child->location->child_cv, lk); 
    }
    this_child->exit = true;
    exitstatus = _MKWAIT_EXIT(this_child->exit_code);
    lock_release(lk);
  }
 // lock_release(lk);

#else
  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif 
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}


////////////////////////////////////////A2b/////////////////////////////////////////

#if OPT_A2

int
sys_execv(userptr_t program, userptr_t args) {

  struct addrspace *as, *old_as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;

  if (program == NULL) {
    panic("sys_execv: program did not exist\n");
    return ENOENT;
  }

  if (args == NULL) {
    panic("sys_execv: One of the args is an invalid pointer.\n");
    return EFAULT;
  }

  //// 1. count the number of arguments and copy them into the kernel ////
  char* args_array[65]; // store ptr to each arguments 
  int args_len[65];     // store size of each arguments
  int count = 0;        // count number of arguments (do not include NULL terminator)
  for (int i = 0; i < 65; i++){
    userptr_t temp;
    result = copyin(args + i * 4, &temp, sizeof(userptr_t));
    if (result) {
      panic("sys_execv: fail in copyin function\n");
      return result;
    }

    if (temp == NULL) { // check NULL terminator -> STOP
      break;
    }

    count += 1; 
    
    args_array[i] = kmalloc(sizeof(char) * PATH_MAX); // allocate space for each argument
    if (args_array[i] == NULL) {
      for (int j = 0; j < i; j++) {
        kfree(args_array[j]);
      }
      panic("sys_execv: fail on allocating spacr for each argument\n");
      return ENOMEM;
    }
    result = copyinstr(temp, args_array[i], PATH_MAX, (size_t *)&args_len[i]);
    if (result) {
      for (int j = 0; j < i; j++) {
        kfree(args_array[j]);
      }      
      panic("sys_execv: fail in copyinstr function\n");
      return result;
    }
  }
  
  if (args_array[count] != NULL) {
    panic("sys_execv: The total size of the argument strings is too large\n");
    return E2BIG;
  }


  //// 2. copy the program path into the kernel ////
  char progName[PATH_MAX];
  result = copyinstr(program, progName, PATH_MAX, NULL);
  if (result) {
    for (int i = 0; i < count; i ++) {
      kfree(args_array[i]);
    }
    return result;
  }


  //// 3. open the program file using vfs_open(prog_name, ...)*////
  /* Open the file. */
  result = vfs_open((char *)program, O_RDONLY, 0, &v);
  if (result) {
    for (int i = 0; i < count; i ++) {
      kfree(args_array[i]);
    }   
  	return result;
  }


  //// 4. create new address space, set process to the new address space, and activate it ////
  /* Create a new address space. */
  as = as_create();
  if (as == NULL) {
    for (int i = 0; i < count; i ++) {
      kfree(args_array[i]);
    }
  	vfs_close(v);
  	return ENOMEM;
  }

  /* Switch to it and activate it. */
  old_as = curproc_setas(as);
  as_activate();


  //// 5. using the opened program file, load the program image using load_elf ////
  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
  	/* p_addrspace will go away when curproc is destroyed */
    for (int i = 0; i < count; i ++) {
      kfree(args_array[i]);
    }
    curproc_setas(old_as); 
    as_activate();
    as_destroy(as);
  	vfs_close(v);
  	return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr); // modify as_define_stack needs to pass too many arguments, give up
  if (result) {
    for (int i = 0; i < count; i ++) {
      kfree(args_array[i]);
    }
    curproc_setas(old_as); 
    as_activate();
    as_destroy(as);
  	/* p_addrspace will go away when curproc is destroyed */
  	return result;
  }


  //// 6. need to copy the arguments into the new address space ////
  vaddr_t addr_array[count + 1];
  addr_array[count] = 0; // NULL
  for (int i = 0; i < count; i++) {
    size_t size = ROUNDUP(args_len[i],8);
    stackptr -= size;
    addr_array[i] = stackptr;
    result = copyoutstr(args_array[i], (userptr_t)stackptr, (size_t)args_len[i],NULL);
    if (result) {
      for (int j = 0; j < count; j++) {
        kfree(args_array[j]);
      }

      curproc_setas(old_as);
      as_activate();
      as_destroy(as);
      return result;
    }
  }

  size_t size = ROUNDUP(sizeof(addr_array), 8);
  stackptr -= size;
  result = copyout(&addr_array, (userptr_t)stackptr, sizeof(userptr_t) * (count + 1));
  if (result) {
    for (int j = 0; j < count; j++) {
      kfree(args_array[j]);
    }
    curproc_setas(old_as); 
    as_activate();
    as_destroy(as);
    return result;
  }
  
   
  //// 7. delete old address space ////
  as_destroy(old_as);


  //// 8. call enter_new_process with address to the arguments on the stack  ////
  /* Warp to user mode. */
	enter_new_process(count /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
#endif


