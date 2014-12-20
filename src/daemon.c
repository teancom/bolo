#include "bolo.h"
#include <signal.h>

/* signal-handling thread; when it exits, main will
   pthread_cancel all of the other threads and then
   join them, in order. */
void* watcher(void *u)
{
	sigset_t *sigs = (sigset_t*)u;
	int s, sig;
	for (;;) {
		s = sigwait(sigs, &sig);
		if (s != 0) {
			errno = s;
			perror("watcher sigwait");
			exit(1);
		}

		logger(LOG_ERR, "watcher received signal %i; terminating\n", sig);
		return NULL;
	}
}

int main(int argc, char **argv)
{
	int rc;
	server_t svr;
	pthread_t tid_watcher, tid_db, tid_sched, tid_ctrl, tid_lsnr;
	const char *config_file = DEFAULT_CONFIG_FILE;
	if (argc == 2) config_file = argv[1];

	memset(&svr, 0, sizeof(svr));
	svr.config.listener     = strdup(DEFAULT_LISTENER);
	svr.config.controller   = strdup(DEFAULT_CONTROLLER);
	svr.config.log_level    = strdup(DEFAULT_LOG_LEVEL);
	svr.config.log_facility = strdup(DEFAULT_LOG_FACILITY);
	svr.config.runas_user   = strdup(DEFAULT_RUNAS_USER);
	svr.config.runas_group  = strdup(DEFAULT_RUNAS_GROUP);
	svr.config.pidfile      = strdup(DEFAULT_PIDFILE);
	svr.config.savefile     = strdup(DEFAULT_SAVEFILE);
	svr.config.dumpfiles    = strdup(DEFAULT_DUMPFILES);

	svr.interval.tick       = 1000;
	svr.interval.freshness  = 2;
	svr.interval.savestate  = 15;

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

	if (daemonize(svr.config.pidfile, svr.config.runas_user, svr.config.runas_group) != 0) {
		logger(LOG_CRIT, "daemonization failed!");
		return 2;
	}

	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGQUIT);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGTERM);
	sigaddset(&sigs, SIGUSR1);
	rc = pthread_sigmask(SIG_BLOCK, &sigs, NULL);
	if (rc != 0) {
		fprintf(stderr, "Failed to block signals: %s\nAborting...\n", strerror(errno));
		return 2;
	}
	rc = pthread_create(&tid_watcher, NULL, watcher, &sigs);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up watcher thread\nAborting...\n");
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
	rc = pthread_create(&tid_ctrl, NULL, controller, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up controller thread\nAborting...\n");
		return 2;
	}
	rc = pthread_create(&tid_lsnr, NULL, listener, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up listener thread\nAborting...\n");
		return 2;
	}

	void *_;
	pthread_join(tid_watcher, &_);

	pthread_cancel(tid_db);    pthread_join(tid_db,  &_);
	pthread_cancel(tid_sched); pthread_join(tid_sched, &_);
	pthread_cancel(tid_lsnr);  pthread_join(tid_lsnr,  &_);
	pthread_cancel(tid_ctrl);  pthread_join(tid_ctrl,  &_);
	zmq_ctx_destroy(svr.zmq);
	deconfigure(&svr);

	logger(LOG_INFO, "shutting down");
	return 0;
}
