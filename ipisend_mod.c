#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define NTIMES 100000

#define DRY_RUN 0
#define POKE_SELF 1
#define POKE_LOCAL 2
#define POKE_REMOTE 3
#define POKE_ALL 4
#define SMP_CORE 28
#define DELAY 0 
#define MS_TO_NS 1000


static inline void invlpg(unsigned long addr) {
    asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

static void handle_ipi(void *arg){
    u64 *t = (u64*)arg;
    if (t)
        *t = ktime_get_ns() - *t;
}

/* Send IPI without waiting */
static u64 send_ipi(int flags){
    u64 t = 0;
    unsigned int cpu = get_cpu();
    unsigned int target = 0;
    switch(flags){
        case DRY_RUN:
            /* Do nothing */
            break;
        case POKE_SELF:
            /* Send IPI to itself */
            smp_call_function_single(cpu, handle_ipi, NULL, 0);
            break;
        case POKE_LOCAL:
            /* Assume 2 socket machine here */
            target = (cpu >= SMP_CORE) ? (3 * SMP_CORE - 1 - cpu) : (SMP_CORE - 1 - cpu); 
            smp_call_function_single(target, handle_ipi, NULL, 0);
            break;
        case POKE_REMOTE:
            /* Assume 2 socket machine here */
            target = ( 2 * SMP_CORE - 1 - cpu);
            smp_call_function_single(target, handle_ipi, NULL, 0);
            break;
        case POKE_ALL:
            smp_call_function_many(cpu_online_mask, handle_ipi, NULL, 0);
            break;
        default:
            t = -EINVAL;
    }
    put_cpu();
    return t;
}

static int __bench_ipi(unsigned long times, int flags, u64 *ipi){
    *ipi = 0;
    u64 t;
    while(times --){
        t = send_ipi(flags);
        if ((int)t < 0)
                return (int)t;
        *ipi += t;
        udelay(DELAY);
    }

    return 0;
}

static int bench_ipi(unsigned long times, int flags, u64 *ipi, u64 *total) {
    int ret;
    *total = ktime_get_ns();
    ret = __bench_ipi(times, flags, ipi);
    if(ret) 
        return ret;
    *total = ktime_get_ns() - *total - DELAY * MS_TO_NS * NTIMES;
    return 0;
}

static int  __init module_load(void) {
    u64 ipi, total;
    int ret;
    ret = bench_ipi(NTIMES, DRY_RUN, &ipi, &total);
    if (ret)
        pr_err("Dry-run FAILED: %d\n", ret);
    else
        pr_err("Dry-run:        %18llu, %18llu ns\n", ipi, total);
    
    ret = bench_ipi(NTIMES, POKE_SELF, &ipi, &total);
    if (ret)
        pr_err("Self-IPI FAILED: %d\n", ret);
    else
        pr_err("Self-IPI:       %18llu, %18llu ns\n", ipi, total);
    
    ret = bench_ipi(NTIMES, POKE_LOCAL, &ipi, &total);
    if (ret)
        pr_err("Local IPI FAILED: %d\n", ret);
    else
        pr_err("Local IPI:     %18llu, %18llu ns\n", ipi, total);

    ret = bench_ipi(NTIMES, POKE_REMOTE, &ipi, &total);
    if (ret)
        pr_err("Remote IPI FAILED: %d\n", ret);
    else
        pr_err("Remote IPI:     %18llu, %18llu ns\n", ipi, total);

    ret = bench_ipi(NTIMES, POKE_ALL, &ipi, &total);
    if (ret)
        pr_err("Broadcast IPI FAILED: %d\n", ret);
    else
        pr_err("Broadcast IPI:  %18llu, %18llu ns\n", ipi, total);
    
    return 0;
}

static void __exit module_unload(void) {
    printk("Goodbye\n");
}

module_init(module_load);
module_exit(module_unload);
