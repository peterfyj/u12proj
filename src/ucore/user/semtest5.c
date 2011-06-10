#include <stdio.h>
#include <ulib.h>
#include <thread.h>

sem_t sem1;
sem_t sem2;

int myThread(void *arg)
{
	sleep(10);
	sem_post(sem1);
	sem2 = sem_init(0);
	sem_wait(sem2);
	cprintf("Child here!\n");
	return 0;
}

int main()
{
	thread_t tid;
	sem1 = sem_init(0);
	thread(myThread, NULL, &tid);
	sem_wait(sem1);
	cprintf("Father here!\n");
	sem_post(sem2);
	
	int exit_code;
	thread_wait(&tid, &exit_code);
	return 0;
}
