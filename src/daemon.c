#include "bolo.h"
#include <pthread.h>
#include <string.h>

int main(int argc, char **argv)
{
	int rc;
	server_t svr;
	pthread_t tid_db, tid_sched, tid_ctrl, tid_lsnr;
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

	log_open("bolo", svr.config.log_facility);
	log_level(0, svr.config.log_level);
	logger(LOG_INFO, "starting up");

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
	rc = pthread_create(&tid_ctrl, NULL, controller, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up controller thread\nAborting...\n");
		return 2;
	}
	rc = pthread_create(&tid_lsnr, NULL, listener, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up NSCA listener thread\nAborting...\n");
		return 2;
	}

	void *_;
	pthread_join(tid_db,    &_);
	pthread_join(tid_sched, &_);
	pthread_join(tid_ctrl,  &_);
	pthread_join(tid_lsnr,  &_);

	logger(LOG_INFO, "shutting down");

	return 0;
}
