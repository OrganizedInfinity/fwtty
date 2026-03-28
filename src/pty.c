#include "pty.h"

#include <fcntl.h>
#include <pthread.h>
#include <pty.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>

#define IOBUFFERSIZE 1024
#define XTY_FILENO 30
#define MAX_RTY 30

sig_atomic_t windowChanged = false;
sig_atomic_t sigReceived = 0;
sig_atomic_t exitedProcess = 0;
sig_atomic_t rtyRequestor = 0;
sig_atomic_t rtyExited = 0;

sig_atomic_t xtyMasterFd = -1;
sig_atomic_t ptyMasterFd;
pthread_mutex_t masterWriteMX = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rtyMX = PTHREAD_MUTEX_INITIALIZER;

struct rty {
	pid_t client;
	int master;
	int slave;
};

int rtyCount = 0;
struct rty rtys[MAX_RTY] = {};

void handleSignal(int signal, siginfo_t *info, void *context) {
	if (signal == SIGUSR1)
		rtyRequestor = info->si_pid;
	if (signal == SIGUSR2)
		rtyExited = info->si_pid;
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
	int bytesRead = 0;
	int ptyMaster = *(int*)arg;
	size_t bufferSize = IOBUFFERSIZE;
	char buffer[IOBUFFERSIZE]; 
	while (true) {
		TRY((status = read(ptyMaster, &buffer, bufferSize)) != -1, "Failed to read from pseudoterminal", (void*)9);
		bytesRead = status;
		TRY((errno = pthread_mutex_lock(&masterWriteMX)) == 0, "Failed to lock for writing", (void*)29);
		TRY((status = write(STDOUT_FILENO, &buffer, bytesRead)) != -1, "Failed to write output", (void*)10);
		TRY((errno = pthread_mutex_unlock(&masterWriteMX)) == 0, "Failed to unlock for writing", (void*)30);
		TRY((errno = pthread_mutex_lock(&rtyMX)) == 0, "Failed to lock pseudoreadterminal datatbase", (void*)34);
		for (int i = 0; i < rtyCount; i++)
			TRY((status = write(rtys[i].master, &buffer, bytesRead)) != -1, "Failed to write pseudoreadterminal output", (void*)38);
		TRY((errno = pthread_mutex_unlock(&rtyMX)) == 0, "Failed to unlock pseudoreadterminal datatbase", (void*)36);

	}
}

void* readXTY(void *arg) {
	int status = 0;
	int xtyMaster = *(int*)arg;
	size_t bufferSize = IOBUFFERSIZE;
	char buffer[IOBUFFERSIZE];

	while (true) {
		TRY((status = read(xtyMaster, &buffer, bufferSize)) != -1, "Failed to read from pseudoexterminal", (void*)31);
		TRY((errno = pthread_mutex_lock(&masterWriteMX)) == 0, "Failed to lock for writing", (void*)29);
		TRY((status = write(ptyMasterFd, &buffer, status)) != -1, "Failed to write output", (void*)10);
		TRY((errno = pthread_mutex_unlock(&masterWriteMX)) == 0, "Failed to unlock for writing", (void*)30);
	};
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

int openXTY() {
	int status = 0;

	if (xtyMasterFd != -1)
		return 0;
	int xtySlave;
	pthread_t readThread;
	TRY(openpty(&xtyMasterFd, &xtySlave, NULL, NULL, NULL) == 0, "Failed to open pseudoexterminal", 32);
	dup2(xtySlave, XTY_FILENO);
	close(xtySlave);
	
	status = pthread_create(&readThread, NULL, readXTY, &xtyMasterFd);
	if (status != 0) {
		fprintf(stderr, "Failed to start exreading thread: pthread_create failed with %d (%s)", status, strerror(status));
		return 33;
	}

	return 0;
}

int sendRTYNotice(pid_t requestor, int notice) {
	int status;
	union sigval val = { .sival_int = notice };
	TRY((status = sigqueue(requestor, SIGUSR1, val)) == 0, "Failed to send pseudoreadterminal notice to client", 37);
	return status;
}

int openRTY(pid_t requestor) {
	int status = 0;
	if (xtyMasterFd == -1)
		if ((status = openXTY()) != 0)
			return status;

	rtyRequestor = 0;
	
	TRY((errno = pthread_mutex_lock(&rtyMX)) == 0, "Failed to lock pseudoreadterminal datatbase", 34);
	for (int i = 0; i < rtyCount; i++) {
		if (rtys[i].client == requestor)
			sendRTYNotice(requestor, rtys[i].slave);
	}

	if (rtyCount >= MAX_RTY)
		return sendRTYNotice(requestor, 0);
	
	struct rty rty = { .client = requestor };
	
	TRY(openpty(&rty.master, &rty.slave, NULL, NULL, NULL) == 0, "Failed to open pseudoreadterminal", 35);

	rtys[rtyCount] = rty;
	rtyCount++;
	
	TRY((errno = pthread_mutex_unlock(&rtyMX)) == 0, "Failed to unlock pseudoreadterminal datatbase", 36);

	return sendRTYNotice(requestor, rty.slave);
}

// Lock the RTY mutex before calling!
void removeRTY(int i) {
	rtyCount--;
	if (i == rtyCount)
		return;

	for (; i < rtyCount; i++)
		rtys[i] = rtys[i+1];
}

int closeRTY(pid_t requestor) {
	rtyExited = 0;
	TRY((errno = pthread_mutex_lock(&rtyMX)) == 0, "Failed to lock pseudoreadterminal datatbase", 34);
	int i = 0;
	struct rty *rty;
	for (; i <= rtyCount; i++) {
		if (i == rtyCount)
			return 0;
		rty = &rtys[i];
		if (rty->client == requestor)
			break;
	}
	
	TRY(close(rty->slave) == 0, "Failed to close pseudoreadterminal slave", 39);
	TRY(close(rty->master) == 0, "Failed to close pseudoreadterminal master", 40);
	removeRTY(i);

	TRY((errno = pthread_mutex_unlock(&rtyMX)) == 0, "Failed to unlock pseudoreadterminal datatbase", 36);
	return 0;
}

int monitorChild(pid_t childPid, int ptyMaster) {
	int status;
	pid_t pgid;
	while (true) {
		if (rtyRequestor != 0)
			if ((status = openRTY(rtyRequestor)) != 0)
				return status;
		if (rtyExited)
			if ((status = closeRTY(rtyExited)) != 0)
				return status;
		if (exitedProcess == childPid) {
			// intentional mutex starvation (again)
			TRY((errno = pthread_mutex_lock(&rtyMX)) == 0, "Failed to lock pseudoreadterminal datatbase", 34);
			for (int i = 0; i < rtyCount; i++)
				kill(rtys[i].client, SIGUSR2);
			return 0;
		}
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
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGUSR2, &action, NULL);

	ptyMasterFd = ptyMaster;

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
