#ifndef INCLUDED_SRC_OPTS_H
#define INCLUDED_SRC_OPTS_H

#define VERSION 0.5

#include <stdbool.h>

struct options {
	char	*workdir,
		*inputFile,
		*outputFile,
		*errorFile,
		*program,
		**programArgs;
	bool	setCTTY,
		openPTY,
		useCurrentSession;
};

int getOptions(int argc, char **argv, struct options *options);

#endif // #ifndef INCLUDED_SRC_OPTS_H
