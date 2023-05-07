#include "worker.h"
#define STACK_SIZE SIGSTKSZ


//Structure to compute timestamp of day
struct timeval tv;


//this is used to assign unique tid value to each thread 
worker_t thrdIDvalue = 1;


struct runqueue * jobQueue; //highest priority in case of MLFQ (level 4) 


// For a 4 level MLFQ
struct runqueue * mlfqLvl1; //least priority
struct runqueue * mlfqLvl2;
struct runqueue * mlfqLvl3; 

// Queue for all threads that get blocked
struct runqueue * blockedThreads = NULL;

// Currently working thread
worker * currentThread = NULL;


//For the scheduler to know if the current thread has been blocked
int isThreadBlocked = 0;

//For the scheduler to know if the current thread yielded within its time slice
int threadYield = 0;


int mainThrdstarted = 0;

//schedulerContext and schedulerThread are the context and thread of the scheduler respectively
ucontext_t schedulerContext;
worker * schedulerThread;

//signal handler
struct sigaction sa;

//initializing the timer
struct itimerval timer;

//Array to store the if thread terminated or not and the exit values of all the threads
void * thread_exitValue[150];
int threadTerminated[150];

//Arrival time of thread
unsigned long threadArrivalTime[150];

//Schedule time for threads
unsigned long threadScheduleTime[150];

//Completion time for thread
unsigned long threadCompletionTime[150];

long long int avgResponsetime = 0;

long long int avgTurnaroundTime = 0;


void releaseThreads();
void freeCurrentThread(worker * thread);
static void sched_rr(runqueue * queue);
static void sched_mlfq();
static void schedule();
void timerHandler(int signum);
void addQueue(worker * newThread, runqueue * queue);


bool isInitialized = false;

ucontext_t mainContext;
worker * mainThread;


void initialize()
{
	if (isInitialized == false){

		isInitialized = true;

		//creating scheduling queue for the jobs
		jobQueue = (runqueue *)malloc(sizeof(runqueue)); //level 4

		// creating queue for the MLFQ levels
		mlfqLvl1 = (runqueue *)malloc(sizeof(runqueue));
		mlfqLvl2 = (runqueue *)malloc(sizeof(runqueue));
		mlfqLvl3 = (runqueue *)malloc(sizeof(runqueue));

		//initializing the queue for those thre with blocked threads
		blockedThreads = (runqueue *)malloc(sizeof(runqueue));

		//registering signal handler when a timer interrupt occurs
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = &timerHandler;
		sigaction(SIGPROF, &sa, NULL);

		//Assigning exit value NULL for each thread and terminated value 0
		for (int i =0; i<150; i++){
			threadTerminated[i] = 0;
			thread_exitValue[i] = NULL;	
		
		}

		// Initializing the scheduler context
		if (getcontext(&schedulerContext) < 0){
			perror("getcontext");
			exit(1);
		}
		//Allocating the fields for scheduler context
		void *stack=malloc(STACK_SIZE);
		schedulerContext.uc_link=NULL;
		schedulerContext.uc_stack.ss_sp=stack;
		schedulerContext.uc_stack.ss_size=STACK_SIZE;
		schedulerContext.uc_stack.ss_flags=0;

		// modifying scheduler context to run the schedule function
		makecontext(&schedulerContext, schedule, 0);

		

	}

}

int worker_create(worker_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	   //This method remains the same for both RR and MLFQ because we always ass a new thread to the top level

       initialize();
	
     // Initializing main thread context and adding to shceduler queue  
	if (mainThrdstarted == 0){
		
		getcontext(&mainContext);
		if(mainThrdstarted == 0)
		{
				
			mainThread = (worker *)malloc(sizeof(worker));
			mainThread -> threadControlBlock = (tcb *)malloc(sizeof(tcb));
			mainThread -> threadControlBlock -> threadId = thrdIDvalue;
			thrdIDvalue ++;
	        mainThread -> threadControlBlock -> status = READY;	
			mainThread -> threadControlBlock -> cctx = mainContext;
			mainThread -> threadControlBlock -> priority = 4;
			addQueue(mainThread, jobQueue);
			//Assigning 1 to started so only one main thread is created
			mainThrdstarted = 1;

			//Transfer to scheduler context
			setcontext(&schedulerContext);

		}

		
	}

	//creating the worker thread  
	worker * newThread = (worker *)malloc(sizeof (worker));

	//Allocating memory for thread control block
	newThread -> threadControlBlock = (tcb *)malloc(sizeof(tcb));


	//setting up the context and stack for this thread
	ucontext_t nctx;
	if (getcontext(&nctx) < 0){
		perror("getcontext");
		exit(1);
	}
	void * thisStack = malloc(STACK_SIZE);
	nctx.uc_link = NULL;
	nctx.uc_stack.ss_sp = thisStack;
	nctx.uc_stack.ss_size = STACK_SIZE;
	nctx.uc_stack.ss_flags = 0;
	
	//Modifying the context of thread by passing the function and arguments
	if (arg == NULL){
		makecontext(&nctx, (void *)function, 0);
	}else{
		makecontext(&nctx, (void *)function, 1,arg);
	}
	
	//assigning a unique id to this thread
	*thread = thrdIDvalue;
	(newThread -> threadControlBlock) -> threadId = *thread;
	thrdIDvalue++;

	//updating the status to READY state
	(newThread -> threadControlBlock) -> status = READY;

	//Updating the priority of thread
	(newThread -> threadControlBlock) -> priority = 4;

	//Update the tcb context for this thread
	(newThread -> threadControlBlock) -> cctx = nctx;

	//(newThread -> threadControlBlock) -> threadTime = clock();

	//Calculating the timestamp.
	// gettimeofday(&tv, NULL);
	// unsigned long time = 1000000 * tv.tv_sec + tv.tv_usec;

	// threadArrivalTime[*thread] = time;
	//printf(" thread %d start time is %ld\n",*thread, threadArrivalTime[*thread]);

	
	//Add to job queue
	addQueue(newThread, jobQueue);

	
    return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context
	
	(currentThread -> threadControlBlock) -> status = READY;
	
	//This lets the scheduler for MLFQ know that the thread has yielded before its time slice
	threadYield = 1;

	//Disabling the timer for thread and then performing context switch to scheduler context
	timer.it_value.tv_usec = 0;
	timer.it_value.tv_sec = 0;

	swapcontext(&((currentThread -> threadControlBlock)->cctx), &schedulerContext);
	
	
	
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread

	//Assigning the value_ptr to thread exit value for each thread

	int index = currentThread -> threadControlBlock ->threadId; 

	if(value_ptr != NULL){
		thread_exitValue[index] = value_ptr;
	}else{
		thread_exitValue[index] = NULL;
	}
	

	//indicating this thread has exited 
	threadTerminated[index] = 1;
	
	freeCurrentThread(currentThread);

	//Making current thread to null
	currentThread = NULL;

	//Disabling the timer for thread and then performing context switch to scheduler context

	timer.it_value.tv_usec = 0;
	timer.it_value.tv_sec = 0;
	
	// clock_t threadCompletionTime;

	// threadCompletionTime = clock()  - (currentThread -> threadControlBlock ->threadTime);

	// double time_taken = ((double)threadCompletionTime)/CLOCKS_PER_SEC;
	// printf("Thread took %f time to execute", time_taken);
	// printf("Hello");

	gettimeofday(&tv, NULL);
	unsigned long time = 1000000 * tv.tv_sec + tv.tv_usec;

	threadCompletionTime[index] = time;
	

	long long int turnaroundTime_thread = threadCompletionTime[index] - threadArrivalTime[index];
	long long int responseTime_thread = threadScheduleTime[index] - threadArrivalTime[index];

		
	
	avgTurnaroundTime = (avgTurnaroundTime + turnaroundTime_thread)/(index-1);
	avgResponsetime = (avgResponsetime + responseTime_thread)/(index-1);
	//printf("Average Response Time is %lld\n", avgResponsetime);
	//printf("Average Turnaround Time is %lld\n", avgTurnaroundTime);
	

	
	setcontext(&schedulerContext);

};

void freeCurrentThread(worker * thread){

	//Deallocating all the dynamic memory created for this thread
	free(((thread -> threadControlBlock) -> cctx).uc_stack.ss_sp);
	free(thread -> threadControlBlock);
	free(thread);

}



/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
  
	while (threadTerminated[thread]==0){

	}

	//Store the value returned by the exited array in value_ptr

	if (value_ptr != NULL){
		
		*value_ptr = thread_exitValue[thread];

	}

	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex

	//checking for invalid pointer:
	if(mutex == NULL){
		return -1;
	}
	
	//The flag initialized to 0.
	mutex -> flag = 0;


	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

		if(!(__atomic_test_and_set (&mutex->flag, 1) == 0)){

			
			//Pushing the current thread to block list as mutex is not acquired
			currentThread -> threadControlBlock -> status = BLOCKED;
			addQueue(currentThread, blockedThreads);

			//Assigning 1 in order to make sure this thread is not scheduled
			isThreadBlocked = 1;
			
			//context switch to scheduler thread
			swapcontext(&((currentThread -> threadControlBlock)->cctx), &schedulerContext);
	

		}

		

		//Assigning the  mutex's thread to current thread
		mutex -> thrd = currentThread;
		mutex -> flag = 1;
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	mutex -> flag = 0;
	mutex -> thrd = NULL;
	//Putting threads into run queue 
	releaseThreads();
	return 0;
};


/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init
if (mutex == NULL){
		return -1;
	}else{
		return 0;
	}
	return 0;
};

/* scheduler */
static void schedule() {
	
	// - invoke scheduling algorithms according to the policy (RR or MLFQ)

	// if (sched == RR)
	//		sched_rr();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

// - schedule policy
#ifndef MLFQ
		// Choose RR
     		sched_rr(jobQueue);
	#else 
		// Choose MLFQ
     		sched_mlfq();
	#endif

}

//static void sched_fcfs() {}

/* Round-robin (RR) scheduling algorithm */
static void sched_rr(runqueue * queue) {
	
	#ifndef MLFQ

		if (currentThread != NULL && isThreadBlocked != 1){
			currentThread -> threadControlBlock -> status = READY;
			addQueue(currentThread, jobQueue);
		}
	#endif
	// if atleast one job is present
	if (queue->head !=NULL){

		//popping head from scheduler which will run and assigning head to next job
		node * job = queue -> head;
		queue -> head = queue -> head -> next;

		//For only 1 job in queue
		if (queue -> head == NULL){
			queue -> tail = NULL;
		}

		//isolating the job and pointing its next ot NULL
		job->next = NULL;

		
		currentThread = job -> thr;
		isThreadBlocked = 0;
		currentThread -> threadControlBlock -> status = SCHEDULED;

		free(job);

		//Assigning timer to the time slice of job
		timer.it_value.tv_usec = QUANTUMTIME*1000;
		timer.it_value.tv_sec = 0;
		setitimer(ITIMER_PROF, &timer, NULL);

		gettimeofday(&tv, NULL);
		unsigned long time = 1000000 * tv.tv_sec + tv.tv_usec;

		threadScheduleTime[currentThread->threadControlBlock->threadId] = time;
		
		setcontext(&(currentThread -> threadControlBlock->cctx));
		
		
	}




}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	
	// Check if the thread has not been blocked or has not exited
	if(currentThread != NULL && isThreadBlocked != 1){


		//getting priority of current thread
		int threadPriority = currentThread -> threadControlBlock -> priority;
		//update status to ready
		currentThread -> threadControlBlock -> status = READY;

		if (threadYield == 1){

			
			//if the value is 1 so thread yielded.Hence assign the job in same priority level queue
			
			if(threadPriority == 1){
				addQueue(currentThread, mlfqLvl1);
			}else if (threadPriority == 2){
				
				addQueue(currentThread, mlfqLvl2);
			}else if (threadPriority == 3){
				addQueue(currentThread, mlfqLvl3);
			}else{
				addQueue(currentThread, jobQueue);
			}
			
			//setting threadYield to 0 for the next thread 
			threadYield = 0;

		}else{

			// Put the thread in lower priority level queue if it has not yielded
			if(threadPriority == 1){
				addQueue(currentThread, mlfqLvl1);
			}else if ( threadPriority== 2){
				currentThread -> threadControlBlock -> priority =1; 
				addQueue(currentThread, mlfqLvl1);

			}else if (threadPriority == 3){
				
				currentThread -> threadControlBlock -> priority =2; 
				addQueue(currentThread, mlfqLvl2);
			}else{
				
				currentThread -> threadControlBlock -> priority =3; 
				addQueue(currentThread, mlfqLvl3);
			}
			
		}
	}


	//Now round robin is performed for the jobs inside the queue from highest to lowest
	if (jobQueue->head!=NULL){
		sched_rr(jobQueue);
	}else if (mlfqLvl3->head != NULL){
		sched_rr(mlfqLvl3);
	}else if (mlfqLvl2->head != NULL){
		sched_rr(mlfqLvl2);
	}else if (mlfqLvl1->head != NULL){
		sched_rr(mlfqLvl1);
	}


}

void timerHandler(int signum){

	//On timer trigger swap context to scheduler context
	swapcontext(&((currentThread -> threadControlBlock)->cctx), &schedulerContext);

}


// Function to add all jobs in runqueue
void addQueue(worker * newThread, struct runqueue * queue){

	if((queue -> tail) != NULL){
		//there is at least one thread in the runq queue
		node *temp  = (node *)malloc(sizeof (node));
       	temp -> thr = newThread;
		temp -> next = NULL;
		//Assign old tail point to new address of new thread 
		queue -> tail->next = temp;
		// Updating tail to new thread
       	queue -> tail = temp;	


	}else{

		// for empty queue
		node * temp = (node *)malloc(sizeof (node));
		temp -> thr = newThread;
		temp -> next = NULL;
		//both head and tail to same thread
		queue -> tail = temp;
	    queue -> head = temp;	

	}

	gettimeofday(&tv, NULL);
	unsigned long time = 1000000 * tv.tv_sec + tv.tv_usec;

	threadArrivalTime[(newThread -> threadControlBlock) -> threadId] = time;
}

//Function to release all threads in block list and add in run queue
void releaseThreads() {
	//Take each thread from the blocked list and add it to runqueue based on priority
	//Free that node in blocked list
	node * curr = blockedThreads -> head;
	node * prev;
	prev = curr;
	while (curr != NULL){

		curr->thr->threadControlBlock->status = READY;
		#ifndef MLFQ

			addQueue(curr -> thr, jobQueue);

		#else

			int threadPriority = curr -> thr -> threadControlBlock -> priority;
					
			if(threadPriority == 4){
				addQueue(curr->thr, jobQueue);
			}else if (threadPriority == 3){
				addQueue(curr->thr, mlfqLvl3);
			}else if (threadPriority == 2){
				addQueue(curr->thr, mlfqLvl2);
			}else{
				addQueue(curr-> thr, mlfqLvl1);
			}	


		#endif

		curr = curr -> next;
		//Free the memory for that job as it is moved to run queue 
		free(prev);
		prev = curr;

	}

	//Make the head and tail null as all jobs have moved
	blockedThreads -> head = NULL;
	blockedThreads -> tail = NULL;
}


