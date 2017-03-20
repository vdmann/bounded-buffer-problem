#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Visitnna Mann and Diego Armando Labrada Sanchez");
MODULE_DESCRIPTION("Project #3 Test Module");

int buffer_size = 0; /* shared variable to handle buffer length conditions*/
int count 		= 0; /* shared counter */
static int bs 	= 0; /* buffer size */
static int np 	= 0; /* number of producers */
static int nc 	= 0; /* number of consumers */
static int st 	= 0; /* sleep time */

module_param(bs, int, 0644);
module_param(np, int, 0644);
module_param(nc, int, 0644);
module_param(st, int, 0644);

struct element {
	int num;
};

/* our queue data structure this structure is a queue of struct element pointers */
DECLARE_KFIFO_PTR(buffer, struct element*); 

/* mutex */
static DEFINE_MUTEX(lock);
struct task_struct **ct_array; /* Array of pointers to the consumer threads */
struct task_struct **pt_array; /* Array of pointers to the producer threads */
/* semaphores */
struct semaphore full;         
struct semaphore empty;
/* element block of the threads */
struct element *foo;

int producer(void *data)
{
	int i = 0;
	while (!kthread_should_stop()) {
		
		msleep(st);
		
		/* wait down empty and mutex, returns 0 if lock is acquired otherwise -EINTR 
			thread waits here for &empty to be signaled to be able enter into its 
			critical section. */
		if ( (i = down_interruptible(&empty)) ) {
			printk(KERN_INFO "producer could not hold empty semaphore\n");
		}

		/* wait down on mutex_lock used for kthreads*/
		if ( (i = mutex_lock_interruptible(&lock)) ) {
			printk(KERN_INFO "value after mutex_lock_interruptable: %d\n", i);
		}
		

		/*----------the critical section----------*/
		if (buffer_size < bs) {
			foo = kmalloc(sizeof(struct element), GFP_KERNEL);
			if (!foo) {
				printk("kmalloc of foo failed\n");
			}
			count++; 
			foo->num = count;
			kfifo_put(&buffer, foo);
			printk(KERN_INFO "Produced %d\n", foo->num);
			buffer_size++;
		}	
		/*----------------------------------------*/

		/* signal */
		mutex_unlock(&lock);
		up(&full);

	}
	do_exit(0);
	return 0;
}

int consumer(void *data)
{
	int i = 0;
	while (!kthread_should_stop()) {

		msleep(st);

		/* down full and mutex*/
		if ( (i = down_interruptible(&full)) ) {
			printk(KERN_INFO "consumer could not hold full semaphore\n");
		}

		/* used for kthreads*/
		if ( (i = mutex_lock_interruptible(&lock)) ) {
			printk(KERN_INFO "value after mutex_lock_interruptable: %d\n", i);
		}
		

		/*----------the critical section----------*/
		if (buffer_size > 0)
		{
			kfifo_get(&buffer, &foo);
			printk(KERN_INFO "Consumed %d\n", foo->num);
			kfree(foo);	
			buffer_size--;
		}
		/*----------------------------------------*/


		/* up mutex and empty*/
		mutex_unlock(&lock);
		up(&empty);

	}
	do_exit(0);
	return 0;
}

/* These two functions just signal the semaphores full and empty so that when
	killing the kthreads none of the threads get locked up in a down()
	loop the mutex should be unlocked within the function itself once its past the 
	semaphore lock */
int awaken_consumer_fn(void *unused){
	while (!kthread_should_stop()) {
		up(&full);
	}
	do_exit(0);
	return 0;
}

int awaken_producer_fn(void *unused){
	while (!kthread_should_stop()) {
		up(&empty);
	}
	do_exit(0);
	return 0;
}

void init_data(void)
{
	int ret;
	ct_array = kmalloc(sizeof(struct task_struct*)*nc, GFP_KERNEL);
	pt_array = kmalloc(sizeof(struct task_struct*)*np, GFP_KERNEL);

	/*Dynamically allocating the buffer bs will be rounded up to the next closest power of 2*/
	ret = kfifo_alloc(&buffer, bs, GFP_KERNEL);
	if (ret) {
		printk(KERN_INFO "fifo failed to allocated %d", kfifo_size(&buffer) );
	}

	sema_init(&empty, bs); /* bs is the buffer size*/
	sema_init(&full, 0);
	mutex_init(&lock);
}

int init_module(void)
{
	/* initialize data*/
	int i;
	init_data();

	printk(KERN_INFO "Loading Project #3 Module");

	/* producer kthread */
	for (i = 0; i < np; i++) {	

		pt_array[i] = kthread_run(&producer, NULL, "p_thread");
	
	}

	/* consumer thread*/
	for (i = 0; i < nc; i++) {	

		ct_array[i] = kthread_run(&consumer, NULL, "c_thread");
	
	}

	return 0;
}

void cleanup_module(void)
{
	int i;
	int ret;
	struct task_struct *awakenerc;
	struct task_struct *awakenerp;

	printk(KERN_INFO "Cleaning and Unloading Project #3 Module");

	// this thread continiously lets consumer processes out of the semaphore
	awakenerc = kthread_run(&awaken_consumer_fn, NULL, "awakener_fn");

	for (i = 0; i < nc; i++) {

		ret = kthread_stop(ct_array[i]);
		if (!ret) {

			printk(KERN_INFO "consumer kthread: %p of task stopped\n", ct_array[i]->comm);
		
		}

	}
	ret = kthread_stop(awakenerc);
	if (!ret) {

		printk(KERN_INFO "awakenerc thread: Stopped.");
	
	}

	// this thread continiously lets producer processes out of the semaphore lock.
	awakenerp = kthread_run(&awaken_producer_fn, NULL, "awakener_fn");

	for (i = 0; i < np; i++) {

		ret = kthread_stop(pt_array[i]);
		
		if (!ret) {

			printk(KERN_INFO "prdoucer kthread: %p of task stopped\n", pt_array[i]->comm);
		
		}
	}

	ret = kthread_stop(awakenerp);
	if (!ret) {

		printk(KERN_INFO "awakenerp thread: Stopped.");
	
	}

	/* The buffer should be full at this time need to free all the elements within the buffer first. 
	   Buffer is rounded up to the nearest power of two but should should only be only have bs elements
	   in it either how. TODO: test.*/
	for(i = 0; i < bs; i++){
		kfifo_get(&buffer, &foo);
		//printk(KERN_INFO "Consumed %d\n", foo->num); // commented out but will use for testing.
		kfree(foo);	
	}

	printk(KERN_INFO "ALL THREADS STOPPED.");

	kfifo_free(&buffer);
	kfree(ct_array);
	kfree(pt_array);
}

/* Known bugs: A soft lockup occurs on the awaken_consumer_fn and awaken_producer_fn functions when a tring to kill
   a large number of threads. Still kills the threads but the warning/error message is displayed. */