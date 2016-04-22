/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 main.c
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 Forrest Yu, 2005
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

/*======================================================================*
 kernel_main
 *======================================================================*/
PUBLIC int kernel_main()
{
    disp_str("-----\"kernel_main\" begins-----\n");

    struct task* p_task;
    struct proc* p_proc = proc_table;
    char* p_task_stack = task_stack + STACK_SIZE_TOTAL;
    u16 selector_ldt = SELECTOR_LDT_FIRST;

    u8 privilege;
    u8 rpl;
    int eflags;
    int i, j;
    int prio;


    for (i = 0; i < NR_TASKS + NR_PROCS; i++){
        if(i < NR_TASKS){ /* 任务 */
            p_task = task_table + i;
            privilege = PRIVILEGE_TASK;
            rpl = RPL_TASK;
            eflags = 0x1202; /* IF=1, IOPL=1, bit 2 is always 1 */
            prio = 15;
        }
        else{ /* 用户进程 */
            p_task = user_proc_table + (i - NR_TASKS);
            privilege = PRIVILEGE_USER;
            rpl = RPL_USER;
            eflags = 0x202; /* IF=1, bit 2 is always 1 */
            prio = 5;
        }

        strcpy(p_proc->name, p_task->name); /* name of the process */
        p_proc->pid = i; /* pid */

        p_proc->ldt_sel = selector_ldt;

        // GDT[1] copy 到 LDT[0]
        memcpy(&p_proc->ldts[0], &gdt[SELECTOR_KERNEL_CS >> 3],
                sizeof(struct descriptor));
        p_proc->ldts[0].attr1 = DA_C | privilege << 5;

        // GDT[2] copy 到 LDT[1]
        memcpy(&p_proc->ldts[1], &gdt[SELECTOR_KERNEL_DS >> 3],
                sizeof(struct descriptor));
        p_proc->ldts[1].attr1 = DA_DRW | privilege << 5;

        // BIT 0~1: RPL
        // BIT2 :TIL: 1代表位在 LDT
        // BIT3~7: selector
        // CS 指向 LDT 第0條
        // 如果GS,SS..等SS載入時，發現TIL被設成1，代表這條code 要去LDT去找
        p_proc->regs.cs = (0 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
        p_proc->regs.ds = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
        p_proc->regs.es = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
        p_proc->regs.fs = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
        p_proc->regs.ss = (8 & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
        p_proc->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;

        // initial_eip就是該process的 fun_ptr，會在restart中，被 iret還原
        p_proc->regs.eip = (u32) p_task->initial_eip;
        p_proc->regs.esp = (u32) p_task_stack;
        p_proc->regs.eflags = eflags;

        disp_str("\nP=");
        disp_int(i);
        disp_str(",addr=");
        disp_int((int) p_proc);
        disp_str(",esp=");
        disp_int(p_proc->regs.esp);

        //觀察 TSS.esp所在的位置
        disp_str(",tss=");
        disp_int((int) &p_proc->ldt_sel);

        /* p_proc->nr_tty		= 0; */

        p_proc->p_flags = 0;
        p_proc->p_msg = 0;
        p_proc->p_recvfrom = NO_TASK;
        p_proc->p_sendto = NO_TASK;
        p_proc->has_int_msg = 0;
        p_proc->q_sending = 0;
        p_proc->next_sending = 0;

        for (j = 0; j < NR_FILES; j++)
            p_proc->filp[j] = 0;

        p_proc->ticks = p_proc->priority = prio;

        p_task_stack -= p_task->stacksize;
        p_proc++;
        p_task++;
        selector_ldt += 1 << 3;
    }

    /* proc_table[NR_TASKS + 0].nr_tty = 0; */
    /* proc_table[NR_TASKS + 1].nr_tty = 1; */
    /* proc_table[NR_TASKS + 2].nr_tty = 1; */

    k_reenter = 0;
    m_ticks = 0;

    p_proc_ready = proc_table;

    init_clock();
    init_keyboard();

    restart();

    while( 1 ){
    }
}

/*****************************************************************************
 *                                get_ticks
 *****************************************************************************/
PUBLIC int get_ticks()
{
    MESSAGE msg;
    reset_msg(&msg);
    msg.type = GET_TICKS;
    // printf("\nGT=%x", proc2pid(p_proc_ready));
    // both 就是向 task_sys 先送"我要tick"，後收 tick
    send_recv(BOTH, TASK_SYS, &msg);
    return msg.RETVAL;
}

/*****************************************************************************
 *                                Init
 *****************************************************************************/
/**
 * The hen.
 * 
 *****************************************************************************/
void Init()
{
	int fd_stdin  = open("/dev_tty0", O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open("/dev_tty0", O_RDWR);
	assert(fd_stdout == 1);

	printf("Init() is running ...\n");

	int pid = fork();
	if (pid != 0) { /* parent process */
		printf("parent is running, child pid:%d\n", pid);
		spin("parent");
	}
	else {	/* child process */
		printf("child is running, pid:%d\n", getpid());
		spin("child");
	}
}

/*======================================================================*
 TestA
 *======================================================================*/
void TestA()
{
    int fd;
    int i, n;

    char filename[MAX_FILENAME_LEN + 1] = "blah";
    const char bufw[] = "abcde";
    const int rd_bytes = 3;
    char bufr[rd_bytes];

    assert(rd_bytes <= strlen((char* )bufw));

    /* create */
    fd = open(filename, O_CREAT | O_RDWR);
    assert(fd != -1);
    printf("File created: %s (fd %d)\n", filename, fd);

    /* write */
    n = write(fd, bufw, strlen((char*) bufw));
    assert(n == strlen((char* )bufw));

    /* close */
    close(fd);

    /* open */
    fd = open(filename, O_RDWR);
    assert(fd != -1);
    printf("File opened. fd: %d\n", fd);

    /* read */
    n = read(fd, bufr, rd_bytes);
    assert(n == rd_bytes);
    bufr[n] = 0;
    printf("%d bytes read: %s\n", n, bufr);

    /* close */
    close(fd);

    char * filenames[] = { "/foo", "/bar", "/baz" };

    /* create files */
    for (i = 0; i < sizeof(filenames) / sizeof(filenames[0]); i++){
        fd = open(filenames[i], O_CREAT | O_RDWR);
        assert(fd != -1);
        printf("File created: %s (fd %d)\n", filenames[i], fd);
        close(fd);
    }

    char * rfilenames[] = { "/bar", "/foo", "/baz", "/dev_tty0" };

    /* remove files */
    for (i = 0; i < sizeof(rfilenames) / sizeof(rfilenames[0]); i++){
        if(unlink(rfilenames[i]) == 0)
            printf("File removed: %s\n", rfilenames[i]);
        else
            printf("Failed to remove file: %s\n", rfilenames[i]);
    }

    spin("TestA");
}

/*======================================================================*
 TestB
 *======================================================================*/
void TestB()
{
    char tty_name[] = "/dev_tty1";

    int fd_stdin = open(tty_name, O_RDWR);
    assert(fd_stdin == 0);
    int fd_stdout = open(tty_name, O_RDWR);
    assert(fd_stdout == 1);

    char rdbuf[128];

    while( 1 ){
        write(fd_stdout, "$ ", 2);
        int r = read(fd_stdin, rdbuf, 70);
        rdbuf[r] = 0;

        if(strcmp(rdbuf, "hello") == 0){
            write(fd_stdout, "hello world!\n", 13);
        }
        else{
            if(rdbuf[0]){
                write(fd_stdout, "{", 1);
                write(fd_stdout, rdbuf, r);
                write(fd_stdout, "}\n", 2);
            }
        }
    }

    assert(0); /* never arrive here */
}

/*======================================================================*
 TestB
 *======================================================================*/
void TestC()
{
    //spin("TestC");
    /* assert(0); */
    while( 1 ){
        printf("C");
        milli_delay(200);
    }
}

/*****************************************************************************
 *                                panic
 *****************************************************************************/
PUBLIC void panic(const char *fmt, ...)
{
    int i;
    char buf[256];

    /* 4 is the size of fmt in the stack */
    va_list arg = (va_list) ((char*) &fmt + 4);

    i = vsprintf(buf, fmt, arg);

    printl("%c !!panic!! %s", MAG_CH_PANIC, buf);

    /* should never arrive here */
    __asm__ __volatile__("ud2");
}


