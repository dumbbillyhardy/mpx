#include "r3.h"

params *param_ptr;
iocb *trm_iocb, *com_iocb;
char sys_stack[SYS_STACK_SIZE];
unsigned short ss_save;
unsigned short sp_save;
unsigned short new_ss;
unsigned short new_sp;
unsigned short cop_ss;
unsigned short cop_sp;

//LOAD_PROCS
//Author: Billy Hardy
//Input: PCB : The PCB to be inserted
//       Context: The context structure of the PCB
//       Function: The function 'program' the PCB is to run
//Sets the registers and flags for the individual processes that will be loaded into the MPX system
void load_procs(pcb *np, context *npc, void (*func) (void)) {
  npc->IP = FP_OFF(func);
  npc->CS = FP_SEG(func);
  npc->FLAGS = 0x200;
  npc->DS = _DS;
  npc->ES = _ES;
  insertPCB(np);
}

//CALLDISPATCH
//Author: Billy Hardy
//Input: argc, argv (the usual number of args, and arguments themselves)
//Output: LOOP (1)
//Calls the dispatch function the number of arguments is correct
int callDispatch(int argc, char *argv[]) {
  if(argc != 1) {
    invalidArgs(argv[0]);
  } else {
    dispatch();
  }
  return LOOP;
}

//DISPATCH
//Author: Billy Hardy
//Input: void (this is an interrupt)
//Output: void
//Checks if stack pointes have been saved, if not, it saves them.
//Gets the next process to run, or restores the MPX state
//If a PCB is found, a context switch occurs, allowing that process to run
void interrupt dispatch() {
  if(ss_save == NULL) {
    //save stack pointers
    ss_save = _SS;
    sp_save = _SP;
  }
  running = getNextRunning();
  if(running != NULL) {
    removePCB(running);
    running->state = RUNNING;
    new_ss = FP_SEG(running->top);
    new_sp = FP_OFF(running->top);
    _SS = new_ss;
    _SP = new_sp;
  } else {
    //restore stack pointer
    _SS = ss_save;
    _SP = sp_save;
    ss_save = NULL;
    sp_save = NULL;
  }
}

//SYS_CALL
//Author: Billy Hardy
//Input: void (interrupt)
//Output: void
//Accesses the parameters placed on the stack by sys_req, and determines the interrupt reason
void interrupt sys_call() {
  static iocb *device;
  static iod *temp_iod;
  cop_ss = _SS;
  cop_sp = _SP;
  //switch to temp stack
  new_ss = FP_SEG(sys_stack);
  new_sp = FP_OFF(sys_stack) + SYS_STACK_SIZE;
  _SS = new_ss;
  _SP = new_sp;
  trm_getc();
  param_ptr = (params *) (running->top+sizeof(context));
  if(com_iocb->event_flag) {
    com_iocb->event_flag = 0;
    temp_iod = com_iocb->head;
    if(com_iocb->count > 1) {
      com_iocb->head = com_iocb->head->next;
      com_iocb->count--;
      if(param_ptr->op_code == READ 
	 || param_ptr->op_code == WRITE) {
	io_scheduler();
      }
    } else {
      com_iocb->head = com_iocb->tail = NULL;
      com_iocb->count--;
    }
    unblockPCB(temp_iod->curr);
    sys_free_mem(temp_iod);
    temp_iod = com_iocb->head;
    switch(param_ptr->op_code) {
    case(READ):
      com_read(temp_iod->trans_buff, temp_iod->count);
      break;
    case(WRITE):
      com_write(temp_iod->trans_buff, temp_iod->count);
      break;
    }
  } else if(trm_iocb->event_flag) {
    trm_iocb->event_flag = 0;
    temp_iod = trm_iocb->head;
    if(trm_iocb->count > 1) {
      trm_iocb->head = trm_iocb->head->next;
      trm_iocb->count--;
      if(param_ptr->op_code == READ 
	 || param_ptr->op_code == WRITE 
	 || param_ptr->op_code == CLEAR 
	 || param_ptr->op_code == GOTOXY) {
	io_scheduler();
      }
    } else {
      trm_iocb->head = trm_iocb->tail = NULL;
      trm_iocb->count--;
      unblockPCB(temp_iod->curr);
      sys_free_mem(temp_iod);
      temp_iod = trm_iocb->head;
      switch(param_ptr->op_code) {
      case(READ):
	trm_read(temp_iod->trans_buff, temp_iod->count);
	break;
      case(WRITE):
	trm_write(temp_iod->trans_buff, temp_iod->count);
	break;
      case(CLEAR):
	trm_clear();
	break;
      case(GOTOXY):
	trm_gotoxy(0, 0);
	break;
      }
    }
  } else if(running != NULL) {
    switch(param_ptr->op_code) {
    case(IDLE):
      running->state = READY;
      insertPCB(running);
      break;
    case(EXIT):
      freePCB(running);
      running = NULL;
      break;
    }
  }
  running->top = MK_FP(cop_ss, cop_sp);
  dispatch();
}

//R3INIT
//Author: Billy Hardy
//Input: N/A
//Output: N/A
//Program to initialize the stack pointers to NULL
void r3Init() {
  sys_set_vec(sys_call);
  ss_save = NULL;
  sp_save = NULL;
}

//IO_SCHEDULER
//Author: Billy Hardy
//Input: N/A
//Output: N/A
//Determines if the IOD is a terminal or COM_PORT communication
//param_ptr OptCode determines if the functionality is read write clear or gotoxy
void io_scheduler() {
  static iod *new_iod;
  static iocb *device;
  switch(param_ptr->device_id) {
  case(TERMINAL):
    device = trm_iocb;
    break;
  case(COM_PORT):
    device = com_iocb;
    break;
  }
  new_iod = (iod *)sys_alloc_mem(sizeof(iod));
  new_iod->curr = running;
  strcpy(new_iod->name, running->name);
  new_iod->trans_buff = param_ptr->buff_addr;
  new_iod->count = param_ptr->count_addr;
  new_iod->request = param_ptr->op_code;
  insertIOD(device, new_iod);
  if(device->count == 0) {
    char buffer[102];
    int err_code, time_limit, tstart, length = 101;
    switch(param_ptr->device_id) {
    case(TERMINAL):
      switch(new_iod->request) {
      case(READ):
	trm_read(new_iod->trans_buff, new_iod->count);
	break;
      case(WRITE):
	trm_write(new_iod->trans_buff, new_iod->count);
	break;
      case(CLEAR):
	trm_clear();
	break;
      case(GOTOXY):
	trm_gotoxy(0, 0);
	break;
      }
      break;
    case(COM_PORT):
      switch(new_iod->request) {
      case(READ):
	err_code = com_read(new_iod->trans_buff, new_iod->count);
	time_limit = RD_TIME_LIMIT;
	if(err_code != 0) {
	  printf("error reading!\n");
	  printf("error code = %d\n", err_code);
	}
	break;
      case(WRITE):
	err_code = com_write(new_iod->trans_buff, new_iod->count);
	time_limit = WR_TIME_LIMIT;
	if(err_code != 0) {
	  printf("error writing!\n");
	  printf("error code = %d\n", err_code);
	}
	break;
      case(CLEAR):
      case(GOTOXY):
	//error state
	break;
      }
      break;
    }
    device->event_flag = 0;
    tstart = time(NULL);
    while(device->event_flag == 0) {
      if((time(NULL)-tstart) > time_limit) {
	printf("TIMEOUT: event flag not set\n");
      }
    }
  }
  running->state = BLOCKED;
}

//INSERTIOD
//Author: Billy Hardy
//Input: IOCB *device: The queue in which to insert an IODevice
//       IOD: *to_insert: The device to be inserted into the queue
//Output: N/A
//
void insertIOD(iocb *device, iod *to_insert) {
  if(device->count == 0) {
    device->head = device->tail = to_insert;
  } else {
    device->tail->next = to_insert;
    device->tail = to_insert;
  }
  device->count++;
}

//IO_INIT
//Author: Billy Hardy
//Input: N/A
//Output: N/A
//Allocates memory for the terminal and com queues
//sets the terminal event flag, and com port baud rate to 1200
void io_init() {
  trm_iocb = (iocb *) sys_alloc_mem(sizeof(iocb));
  com_iocb = (iocb *) sys_alloc_mem(sizeof(iocb));
  trm_open(&(trm_iocb->event_flag));
  com_open(&(com_iocb->event_flag), 1200);
}

//IO_TEAR_DOWN
//Author: Billy Hardy
//Input: N/A
//Output: N/A
//Closes communications ports, empties queues, and frees allocated queue memory
void io_tear_down() {
  trm_close();
  com_close();
  empty_iocb(trm_iocb);
  empty_iocb(com_iocb);
  sys_free_mem(trm_iocb);
  sys_free_mem(com_iocb);
}

//EMPTY_IOCB
//Author: Billy Hardy
//Input: IOCB *to_clear: The queue to be cleared
//Output: N/A
//Removes all of the IODevices from the passed queue
void empty_iocb(iocb *to_clear) {
  iod *curr;
  while(to_clear->count > 0) {
    curr = to_clear->head;
    to_clear->head = curr->next;
    (to_clear->count)--;
    sys_free_mem(curr);
  }
}
