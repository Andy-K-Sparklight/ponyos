#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "lib/toaru_auth.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "live-welcome"

int main(int argc, char * argv[]) {

	int _session_pid = fork();
	if (!_session_pid) {
		setuid(1000);
		toaru_auth_set_vars();

		char * args[] = {"/bin/gsession", NULL};
		execvp(args[0], args);
		TRACE("gsession start failed?");
	}

	int _wizard_pid = fork();
	if (!_wizard_pid) {
		setuid(1000);
		toaru_auth_set_vars();

		char * args[] = {"/bin/wizard.py", NULL};
		execvp(args[0], args);
		TRACE("wizard start failed?");
	}

	int pid = 0;
	do {
		pid = wait(NULL);
	} while ((pid > 0 && pid != _session_pid) || (pid == -1 && errno == EINTR));

	char * args[] = {"/bin/glogin",NULL};
	execvp(args[0],args);

	TRACE("failed to start glogin after log out, trying to reboot instead.");
	system("reboot");

	return 1;
}
