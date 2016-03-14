
#include "type.h"
#include "const.h"
#include "protect.h"
#include "proto.h"
#include "string.h"
#include "proc.h"
#include "global.h"

PUBLIC void schedule()
{
    PROCESS* p;
    int  greatest_ticks = 0;

    while (!greatest_ticks) {
        for (p = proc_table; p < proc_table+NR_TASKS; p++) {
            if (p->ticks > greatest_ticks) {
                greatest_ticks = p->ticks;
                p_proc_ready = p;
            }
        }

        if (!greatest_ticks) {
            for (p = proc_table; p < proc_table+NR_TASKS; p++) {
                p->ticks = p->priority;
            }
        }
    }
}

PUBLIC int sys_get_ticks()
{
	return ticks;
}

