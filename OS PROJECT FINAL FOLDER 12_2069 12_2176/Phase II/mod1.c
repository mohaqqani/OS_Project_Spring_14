/* 
 * This is the main, per-CPU runqueue data structure. 
 * 
 * Locking rule: those places that want to lock multiple runqueues 
 * (such as the load balancing or the thread migration code), lock 
 * acquire operations must be ordered by ascending &runqueue. 
 */ 
#include <linux/latencytop.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>
#include <linux/module.h>   // for all modules
#include <linux/init.h>     // for entry/exit macros
#include <linux/kernel.h>   // for printk() definition
#include <asm/current.h>    // process information
#include "sched.h"
	
/* 
 * schedule() is the main scheduler function. 
 */ 
asmlinkage void __sched schedule(void)
{
	struct task_struct *prev, *next;
	unsigned long *switch_count;
	struct rq *rq;
	int cpu;
	need_resched:
	preempt_disable();
	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	//get run queue of CPU rcu_sched_qs(cpu);
	//some lock mechaism prev = rq->curr;
	//current task_struct switch_count = &prev->nivcsw;release_kernel_lock(prev);need_resched_nonpreemptible:schedule_debug(prev);
	//If we are in HRTICK, clear it 
	spin_lock_irq(&rq->lock);
	update_rq_clock(rq);
	clear_tsk_need_resched(prev);
	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) 
	{
		//what is it? 
		if (unlikely(signal_pending_state(prev->state, prev)))
			prev->state = TASK_RUNNING;
		else
		{
	 		deactivate_task(rq, prev, 1);
	 		switch_count = &prev->nvcsw;
		}
		pre_schedule_rt(rq, prev);
		if (unlikely(!rq->nr_running))
			idle_balance(cpu, rq);
		put_prev_task_rt(rq, prev);
		next = pick_next_task(rq);
		
		if(prev->prio < next->prio)
		{
			put_prev_task(rq, prev);
			next = pick_next_task(rq);
		}
		
		if (likely(prev != next)) 
		{
			sched_info_switch(prev, next);
			perf_event_task_sched_out(prev, next, cpu);
			rq->nr_switches++;
			rq->curr = next;
			++*switch_count;
			context_switch(rq, prev, next);
			/* unlocks the rq */ 
			/* 
			 * the context switch might have flipped the stack from under  
			 * us, hence refresh the local variables. 
			 */ 
			cpu = smp_processor_id();
			rq = cpu_rq(cpu);
		}
		else
 			spin_unlock_irq(&rq->lock);
		post_schedule(rq);	
		if (unlikely(reacquire_kernel_lock(current) < 0))
			preempt_enable_no_resched();
		if (need_resched())
			goto need_resched;
	}
}
	EXPORT_SYMBOL(schedule);
	/*  
	 * context_switch - switch to the new MM and the new 
	 * thread's register state. 
	 */ 
static inline void context_switch(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	struct mm_struct *mm, *oldmm;
	prepare_task_switch(rq, prev, next);
	trace_sched_switch(rq, prev, next);
	mm = next->mm;
	oldmm = prev->active_mm;
	/*  
	 * For paravirt, this is coupled with an exit in switch_to to 
	 * combine the page table reload and the switch backend into 
	 * one hypercall. 
	 */ 
	arch_start_context_switch(prev);
	if (unlikely(!mm)) 
	{
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next);
	}
	else
		switch_mm(oldmm, mm, next);
	if (unlikely(!prev->mm)) 
	{
		prev->active_mm = NULL;
		rq->prev_mm = oldmm;
	}
	/*
	 * Since the runqueue lock will be released by the next 
	 * task (which is an invalid locking op but in the case 
	 * of the scheduler it's an obvious special-case), so we 
	 * do an early lockdep release here: 
	 */ 
	#ifndef __ARCH_WANT_UNLOCKED_CTXSWspin_release(&rq->lock.dep_map, 1, _THIS_IP_);
	#endif 
	/* Here we just switch the register state and the stack. */ 
	switch_to(prev, next, prev);
	barrier();
	/*  
	 * this_rq must be evaluated again because prev may have moved 
	 * CPUs since it called schedule(), thus the 'rq' on its stack
	 * frame will be invalid. 
	 */ 
	finish_task_switch(this_rq(), prev);
}
	/* 
	 * fork()/clone()-time setup: 
	 */ 
void sched_fork(struct task_struct *p, int clone_flags)
{
	int cpu = get_cpu(); 
	__sched_fork(p);
	/* 
	 * Revert to default priority/policy on fork if requested. 
	 */ 
	if (unlikely(p->sched_reset_on_fork)) 
	{
		if (p->policy == SCHED_FIFO || p->policy == SCHED_RR) 
		{
			p->policy = SCHED_NORMAL;
			p->normal_prio = p->static_prio;
		}
		if (PRIO_TO_NICE(p->static_prio) < 0) 
		{
			p->static_prio = NICE_TO_PRIO(0);
			p->normal_prio = p->static_prio;
			set_load_weight(p);
		}
		/* 
		 * We don't need the reset flag anymore after the fork. It has 
		 * fulfilled its duty: 
		 */ 
		p->sched_reset_on_fork = 0;
	}
	/* 
	 * Make sure we do not leak PI boosting priority to the child. 
	 */ 
	p->prio = current->normal_prio;
	if (!rt_prio(p->prio))
		p->sched_class = &fair_sched_class;
	#ifdef CONFIG_SMPcpu = p->sched_class->select_task_rq(p, SD_BALANCE_FORK, 0);
	#endif 
 	set_task_cpu(p, cpu);
	#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
	if (likely(sched_info_on()))
		memset(&p->sched_info, 0, sizeof (p->sched_info));
	#endif 
	 
	#if defined(CONFIG_SMP) && defined(__ARCH_WANT_UNLOCKED_CTXSW)p->oncpu = 0;
	#endif 
	 
	#ifdef CONFIG_PREEMPT
	/* Want to start with kernel preemption disabled. */ 
	task_thread_info(p)->preempt_count = 1;
	#endif 
	plist_node_init(&p->pushable_tasks, MAX_PRIO);
	put_cpu();
}
/* 
 * Check this_cpu to ensure it is balanced within domain. Attempt to move 
 * tasks if there is an imbalance. 
 */ 
static int load_balance(int this_cpu, struct rq *this_rq, struct sched_domain *sd, enum cpu_idle_type idle, int *balance)
{
	int ld_moved, all_pinned = 0, active_balance = 0, sd_idle = 0;
	struct sched_group *group;
	unsigned long imbalance;
	struct rq *busiest;
	unsigned long flags;
	struct cpumask *cpus = __get_cpu_var(load_balance_tmpmask);
	cpumask_setall(cpus);
	/* 
	 * When power savings policy is enabled for the parent domain, idle 
	 * sibling can pick up load irrespective of busy siblings. In this case, 
	 * let the state of idle sibling percolate up as CPU_IDLE, instead of  
	 * portraying it as CPU_NOT_IDLE. 
	 */ 
	if (idle != CPU_NOT_IDLE && sd->flags &SD_SHARE_CPUPOWER && !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		sd_idle = 1;
	schedstat_inc(sd, lb_count[idle]);
	redo:
	update_shares(sd);
	group = find_busiest_group(sd, this_cpu, &imbalance, idle, &sd_idle,cpus, balance);
	if  (*balance == 0)
		goto out_balanced;
	if (!group) 
	{
		schedstat_inc(sd, lb_nobusyg[idle]);
		goto out_balanced;
	}
	busiest = find_busiest_queue(group, idle, imbalance, cpus);
	if (!busiest) 
	{
		schedstat_inc(sd, lb_nobusyq[idle]);
		goto out_balanced;
	}
	BUG_ON(busiest == this_rq);
	schedstat_add(sd, lb_imbalance[idle], imbalance);
	ld_moved = 0;
	if (busiest->nr_running > 1) 
	{
		/* 
		 * Attempt to move tasks. If find_busiest_group has found 
		 * an imbalance but busiest->nr_running <= 1, the group is 
		 * still unbalanced. ld_moved simply stays zero, so it is 
		 * correctly treated as an imbalance. 
		 */ 
		local_irq_save(flags);
		double_rq_lock(this_rq, busiest);
		ld_moved = move_tasks(this_rq, this_cpu, busiest,imbalance, sd, idle, &all_pinned);
		double_rq_unlock(this_rq, busiest);
		local_irq_restore(flags);
		/* 
		 * some other cpu did the load balance for us. 
		 */ 
		if (ld_moved && this_cpu != smp_processor_id())
			resched_cpu(this_cpu);
		/* All tasks on this runqueue were pinned by CPU affinity */ 
		if (unlikely(all_pinned)) 
		{
			cpumask_clear_cpu(cpu_of(busiest), cpus);
			if (!cpumask_empty(cpus))
			goto redo;
			goto out_balanced;
		}
	}
	if (!ld_moved) 
	{
		schedstat_inc(sd, lb_failed[idle]);
		sd->nr_balance_failed++;
		if (unlikely(sd->nr_balance_failed > sd->cache_nice_tries+2)) 
		{
			spin_lock_irqsave(&busiest->lock, flags);
			/* 
			 * don't kick the migration_thread, if the curr  
			 * task on busiest cpu can't be moved to this_cpu 
			 */ 
			if (!cpumask_test_cpu(this_cpu,&busiest->curr->cpus_allowed)) 
			{
				spin_unlock_irqrestore(&busiest->lock, flags);all_pinned = 1;
				goto out_one_pinned;
			}
			if (!busiest->active_balance) 
			{
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				active_balance = 1;
			}
			spin_unlock_irqrestore(&busiest->lock, flags);
			if (active_balance)
				wake_up_process(busiest->migration_thread);
				/* 
				 * We've kicked active balancing, reset the failure 
				 * counter. 
				 */ 
				sd->nr_balance_failed = sd->cache_nice_tries+1;
		}
	}
	else
 		sd->nr_balance_failed = 0;
	if (likely(!active_balance)) 
	{
		/* We were unbalanced, so reset the balancing interval */ 
		sd->balance_interval = sd->min_interval;
	}
	else
	{
		/*  
		 * If we've begun active balancing, start to back off. This 
		 * case may not be covered by the all_pinned logic if there 
		 * is only 1 task on the busy runqueue (because we don't call * move_tasks). 
		 */ 
		if (sd->balance_interval < sd->max_interval)
			sd->balance_interval *= 2;
	}
	if (!ld_moved && !sd_idle && sd->flags &SD_SHARE_CPUPOWER && !test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		ld_moved = -1;
	goto out;
	out_balanced:schedstat_inc(sd, lb_balanced[idle]);
	sd->nr_balance_failed = 0;
	out_one_pinned:
	/* tune up the balancing interval */ 
	if ((all_pinned && sd->balance_interval < MAX_PINNED_INTERVAL) ||(sd->balance_interval < sd->max_interval))
		sd->balance_interval *= 2;
	if (!sd_idle && sd->flags & SD_SHARE_CPUPOWER &&!test_sd_parent(sd, SD_POWERSAVINGS_BALANCE))
		ld_moved = -1;
	else
		ld_moved = 0;out:
	if (ld_moved)update_shares(sd);
		return ld_moved;
}

static int __init hi(void)
{
	return 0;
}

static void __exit bye(void)
{

}

module_init(hi);
module_exit(bye);

MODULE_AUTHOR("student");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Doing a whole lot of nothing.");
