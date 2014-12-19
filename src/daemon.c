#include "bolo.h"
#include <pthread.h>
#include <string.h>

int main(int argc, char **argv)
{
	int rc;
	server_t svr;
	pthread_t tid_db, tid_sched, tid_stat, tid_nsca;
	const char *config_file = DEFAULT_CONFIG_FILE;
	if (argc == 2) config_file = argv[1];

	memset(&svr, 0, sizeof(svr));
	rc = configure(config_file, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to read configuration from %s\nAborting...\n",
			config_file);
		return 1;
	}

	svr.zmq = zmq_ctx_new();
	if (!svr.zmq) {
		fprintf(stderr, "Failed to initialize 0MQ\n");
		return 2;
	}

	rc = pthread_create(&tid_db, NULL, db_manager, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up database manager thread\nAborting...\n");
		return 2;
	}
	rc = pthread_create(&tid_sched, NULL, scheduler, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up scheduler thread\nAborting...\n");
		return 2;
	}
	rc = pthread_create(&tid_stat, NULL, stat_listener, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up stat listener thread\nAborting...\n");
		return 2;
	}
	rc = pthread_create(&tid_nsca, NULL, nsca_listener, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up NSCA listener thread\nAborting...\n");
		return 2;
	}

	void *_;
	pthread_join(tid_db,    &_);
	pthread_join(tid_sched, &_);
	pthread_join(tid_stat,  &_);
	pthread_join(tid_nsca,  &_);

	return 0;
}
