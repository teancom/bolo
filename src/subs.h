#ifndef SUBS_H
#define SUBS_H

int subscriber_init(void);

int subscriber_monitor_thread(void *zmq, const char *prefix, const char *endpoint);
int subscriber_scheduler_thread(void *zmq, int interval);
int subscriber_supervisor(void *zmq);

int subscriber_metrics(void *zmq, ...);

int subscriber_connect_monitor(void *zmq, void **zocket);
int subscriber_connect_scheduler(void *zmq, void **zocket);
int subscriber_connect_supervisor(void *zmq, void **zocket);

#endif
