#include <util/atomic.h>
#include<aavr.h>
#include<stdlib.h>

typedef struct aavr_monitor *AavrMonitor;

struct aavr_monitor {
	AavrPoll poll;
	void *poll_context;

	int elapsed;
	int timeout;

	AavrStatus status;

	AavrAsyncCallback callback;
	void *callback_context;

	AavrMonitor next;
};

AavrMonitor to_poll = NULL;
volatile bool quit = false;
bool in_main_loop = false;

void aavr_quit(
	void) {
	quit = true;
}

void aavr_run(
	AavrPoll idle,
	void *idle_context) {
	/* if called while in the mainloop, or there is no work, return */
	if (in_main_loop || to_poll == NULL)
		return;
	quit = false;
	in_main_loop = false;
	while (to_poll != NULL && !quit) {
		AavrMonitor to_run = NULL;
		AavrMonitor *it = &to_poll;
		/* run every poll callback and check each timeout */
		while (*it != NULL) {
			bool invoked = (*it)->poll != NULL && (*it)->poll((*it)->poll_context);
			bool timedout = (*it)->timeout >= 0 && (*it)->elapsed > (*it)->timeout;
			AavrStatus status = (invoked ? AAVR_STATUS_INVOKED : 0) | (timedout ? AAVR_STATUS_TIMED_OUT : 0) | (quit ? AAVR_STATUS_QUIT : 0);
			/* schedule any runable routines */
			if (status != 0) {
				AavrMonitor dequeue = *it;
				dequeue->status = status;
				ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
					*it = dequeue->next;
					dequeue->next = to_run;
					to_run = dequeue;
				}
			} else {
				it = &(*it)->next;
			}
		}
		if (to_run == NULL && idle != NULL) {
			idle(idle_context);
		} else {
			/* run all the routines scheduled */
			while (to_run != NULL) {
				AavrMonitor dequeue = to_run;
				to_run = dequeue->next;
				dequeue->callback(dequeue, dequeue->callback_context);
			}
		}
	}
	in_main_loop = false;
}

/* Update all the elapsed time values every clock tick. */
void aavr_tick(
	void) {
	AavrMonitor it;
	for (it = to_poll; it != NULL; it = it->next) {
		it->elapsed++;
	}
}

void aavr_wait(
	AavrPoll poll,
	void *poll_context,
	int timeout,
	AavrAsyncCallback callback,
	void *callback_context) {
	AavrMonitor self;

	/* Create a new monitor for this state. */
	self = malloc(sizeof(struct aavr_monitor));
	if (self == NULL) {
		/* If memory is not available, invoke the callback immediately with a
		 * failure. */
		callback(NULL, callback_context);
	}

	self->poll = poll;
	self->poll_context = poll_context;

	self->timeout = timeout;
	self->elapsed = 0;
	self->status = 0;

	self->callback = callback;
	self->callback_context = callback_context;

	/* Queue it to be polled. */
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		self->next = to_poll;
		to_poll = self;
	}
}

int aavr_wait_finish(
	void *finish_context,
	int *time_elapsed) {
	AavrMonitor self = (AavrMonitor) finish_context;
	AavrStatus status;

	/* If self is null, we must have failed. */
	if (self == NULL) {
		if (time_elapsed != NULL)
			*time_elapsed = 0;
		return AAVR_STATUS_FAILED;
	}

	/* Otherwise, extract the needed information from the monitor and dispose of
	 * it. */
	status = self->status;
	if (time_elapsed != NULL)
		*time_elapsed = self->elapsed;
	free(self);
	return status;
}
