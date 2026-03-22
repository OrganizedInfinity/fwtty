#include "pty.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define IOBUFFERSIZE 1024

sig_atomic_t windowChanged = false;
sig_atomic_t sigReceived = 0;
sig_atomic_t exitedProcess = 0;

void handleSignal(int signal, siginfo_t *info, void *context) {
	if (signal == SIGWINCH)
		windowChanged = true;
	if (signal == SIGINT || signal == SIGTSTP || signal == SIGQUIT)
		sigReceived = signal;
	if (signal == SIGCHLD)
		exitedProcess = info->si_pid;
	struct sigaction action = { 0 };
	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = handleSignal;
	sigaction(signal, &action, NULL);
}

void* readPTY(void *arg) {
	int status = 0;
	int ptyMaster = *(int*)arg;
	size_t bufferSize = IOBUFFERSIZE;
	char buffer[IOBUFFERSIZE]; 
	while (true) {
		TRY((status = read(ptyMaster, &buffer, bufferSize)) != -1, "Failed to read from pseudoterminal", (void*)9);
		TRY((status = write(STDOUT_FILENO, &buffer, status)) != -1, "Failed to write output", (void*)10);
	}
}

void* writePTY(void *arg) {
	int status = 0;
	int ptyMaster = *(int*)arg;
	size_t bufferSize = IOBUFFERSIZE;
	char buffer[IOBUFFERSIZE];
	while (true) {
		TRY((status = read(STDIN_FILENO, &buffer, bufferSize)) != -1, "Failed to read from stdin", (void*)11);
		TRY((status = write(ptyMaster, &buffer, status)) != -1, "Failed to write input", (void*)12);
	}
}

void* syncTerminalOptions(void *arg) {
	int ptySlave = *(int*)arg;
	struct termios options, newOptions;
	struct winsize size;
	TRY(ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0, "Failed to acquire terminal size", (void*)17);
	TRY(ioctl(ptySlave, TIOCSWINSZ, &size) == 0, "Failed to set pseudoterminal size", (void*)18);
	TRY(tcgetattr(ptySlave, &options) == 0, "Failed to acquire pseudoterminal attributes", (void*)15);
	TRY(tcsetattr(STDIN_FILENO, TCSADRAIN, &options) == 0, "Failed to set terminal attributes", (void*)16);
	while (true) {
		TRY(tcgetattr(ptySlave, &newOptions) == 0, "Failed to acquire pseudoterminal attributes", (void*)15);
		if (memcmp(&newOptions, &options, sizeof(struct termios)) != 0) {
			options = newOptions;
			TRY(tcsetattr(STDIN_FILENO, TCSADRAIN, &options) == 0, "Failed to set terminal attributes", (void*)16);
		}
		if (windowChanged == true) {
			TRY(ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0, "Failed to acquire terminal size", (void*)17);
			TRY(ioctl(ptySlave, TIOCSWINSZ, &size) == 0, "Failed to set pseudoterminal size", (void*)18);
			windowChanged = false;
		}
		sleep(1);
	}
}

int monitorChild(pid_t childPid, int ptyMaster) {
	pid_t pgid;
	while (true) {
		if (exitedProcess == childPid)
			return 0;
		if (sigReceived != 0) {
			TRY((pgid = tcgetpgrp(ptyMaster)) != -1, "Failed to get terminal foreground process group", 20);
			TRY(kill(pgid, sigReceived) != -1, "Failed to send signal to process", 21);
			sigReceived = 0;
		}
		usleep(100);
	}
}

int emulateTerminal(pid_t childPid, int ptyMaster, int ptySlave) {
	int status = 0;
	struct sigaction action = { 0 };
	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = handleSignal;
	sigaction(SIGWINCH, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);
	sigaction(SIGTSTP, &action, NULL);
	sigaction(SIGQUIT, &action, NULL);

	pthread_t writeThread, readThread, optionsThread;
	status = pthread_create(&writeThread, NULL, writePTY, &ptyMaster);
	if (status != 0) {
		fprintf(stderr, "Failed to start writing thread: pthread_create failed with %d (%s)", status, strerror(status));
		return 13;
	}

	status = pthread_create(&readThread, NULL, readPTY, &ptyMaster);
	if (status != 0) {
		fprintf(stderr, "Failed to start reading thread: pthread_create failed with %d (%s)", status, strerror(status));
		return 14;
	}

	status = pthread_create(&optionsThread, NULL, syncTerminalOptions, &ptySlave);
	if (status != 0) {
		fprintf(stderr, "Failed to start terminal options thread: pthread_create failed with %d (%s)", status, strerror(status));
		return 19;
	}
			
	return monitorChild(childPid, ptyMaster);
}
