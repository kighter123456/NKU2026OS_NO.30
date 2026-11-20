#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2411520
// schedule - FIFO 轮转调度：从 proc_list 中选出下一个 RUNNABLE 线程
void schedule(void)
{
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        /* 进入调度点：当前线程已经把 CPU 让出来了 */
        current->need_resched = 0;

        /* 当前不是 idleproc 时从它的下一个节点开始，实现轮转；
         * 当前是 idleproc 时从链表头开始，给所有线程平等机会。 */
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do
        {
            if ((le = list_next(le)) != &proc_list)
            {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE)
                {
                    break;
                }
            }
        } while (le != last);

        /* 找不到就绪线程时，用 idleproc 保底 */
        if (next == NULL || next->state != PROC_RUNNABLE)
        {
            next = idleproc;
        }
        next->runs++;

        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}


