#ifndef INCLUDED_SRC_PTY_H
#define INCLUDED_SRC_PTY_H

#include <signal.h>
#include <stdbool.h>
#include <errno.h>

#define TRY(assertion, errorMessage, returnCode) \
	if (!(assertion)) { \
		fprintf(stderr, "%s: %s\n", errorMessage, strerror(errno)); \
		return returnCode; \
	}

int emulateTerminal(pid_t childPid, int ptyMaster, int ptySlave);

#endif // #ifndef INCLUDED_SRC_PTY_H
