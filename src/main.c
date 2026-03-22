#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <pthread.h>

#include "opts.h"
#include "pty.h"

int main(int argc, char **argv) {
	int status = 0;
	struct options options;	

	status = getOptions(argc, argv, &options);
	if (status < 0)
		return 0;
	if (status > 0)
		return status;

	int ptyMaster, ptySlave;
	if (options.openPTY)
		TRY(openpty(&ptyMaster, &ptySlave, NULL, NULL, NULL) == 0, "Failed to open pseudoterminal", 8);

	pid_t pid;
	TRY((pid = fork()) != -1, "Failed to fork", 6);

	// parent
	if (pid != 0) {
		if (options.openPTY)
			return emulateTerminal(pid, ptyMaster, ptySlave);
		return 0;
	}

	if (options.workdir != NULL)
		TRY(chdir(options.workdir) == 0, "Failed to set working directory", 7);

	if (!options.useCurrentSession)
		TRY(setsid() >= 0, "Failed to create session (try using -s)", 6);

	// reset all signal handlers
	struct sigaction defaultAction = { 0 };
	defaultAction.sa_handler = SIG_DFL;
	for (int sig = 0; sig < NSIG; sig++)
		sigaction(sig, &defaultAction, NULL);

	if (options.openPTY) {
		// child shouldnt own the multiplexer
		close(ptyMaster);

		dup2(ptySlave, STDIN_FILENO);
		dup2(ptySlave, STDOUT_FILENO);
		dup2(ptySlave, STDERR_FILENO);
	}

	// the first opened file becomes the controlling terminal by default
	if (options.inputFile != NULL)
		TRY(freopen(options.inputFile, "r", stdin) != NULL, "Failed to set stdin", 2);
	if (options.outputFile != NULL)
		TRY(freopen(options.outputFile, "w", stdout) != NULL, "Failed to set stdout", 3);
	if (options.errorFile != NULL)
		TRY(freopen(options.errorFile, "w", stderr) != NULL, "Failed to set stderr", 4);	

	if (options.setCTTY)
		TRY(ioctl(STDERR_FILENO, TIOCSCTTY, 1) == 0, "Failed to set controlling terminal!", 5);

	TRY(execvp(options.program, options.programArgs) == 0, "Failed to start program", 1);
	
	return 0;
}
