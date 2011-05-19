#include <stdio.h>
#include <ulib.h>
#include <thread.h>

int myThread(void *arg)
{
	sleep(100);
	cprintf("Child here!\n");
	return 0;
}

int main()
{
	thread_t tid;
	thread(myThread, NULL, &tid);
	cprintf("Father here!\n");
	return 0;
}

