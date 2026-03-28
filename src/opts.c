#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "opts.h"

#define stringize(s) #s
#define tostring(s) stringize(s)

void printUsage() {
	puts(	"Usage:\n"
		"	fwtty [options] [-- <arguments>]\n\n"
		"Forward the inputs and outputs of <program> to <file>\n\n"
		"Options:\n"
		"	-h		Print usage information and exit.\n"
		"	-v		Print version information and exit.\n"
		"	-w <directory>	Set the working directory of <program> to <directory>\n"
		"	-i <file>	Forward stdin to <file>\n"
		"	-o <file>	Forward stdout to <file>\n"
		"	-e <file>	Forward stderr to <file>\n"
		"	-f <file>	Forward stdin, stdout and stderr to <file>\n"
		"	-c		Attempt to set stderr as the controlling terminal.\n"
		"	-s		Do not create a new session.\n"
		"	-t		Open a new pseudoterminal and run <program> on its slave end.\n"
		"	-p <program>	Specify what <program> will be forwarded. Defaults to sh.\n"
		"	-- <arguments>	End fwtty options and pass <arguments> to <program>"
	);
}

void printVersion() {
	puts("Version: " tostring(VERSION));
}

typedef int (*optionParser)(char*, struct options*);

int parseProgram(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <program> after -p!");
		return 51;
	}
	opts->program = arg;
	return 0;
}

int parseWorkdir(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <directory> after -w!");
		return 56;
	}
	opts->workdir = arg;
	return 0;
}

int parseFile(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <file> after -f!");
		return 52;
	}
	opts->inputFile = arg;
	opts->outputFile = arg;
	opts->errorFile = arg;

	return 0;
}

int parseInput(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <file> after -i!");
		return 53;
	}
	opts->inputFile = arg;

	return 0;
}

int parseOutput(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <file> after -o!");
		return 54;
	}
	opts->outputFile = arg;

	return 0;
}

int parseError(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <file> after -e!");
		return 55;
	}
	opts->errorFile = arg;

	return 0;
}

int failParsing(char *arg, struct options *opts) {return 5;};
int stopParsing(char *arg, struct options *opts) {return -1;};
int parsePArgs(char *arg, struct options *opts) {
	if (arg == NULL) {
		puts("Expected <arguments> after --!");
		return 50;
	};
	return -2;
}

optionParser parseOption(char letter, struct options *options) {
	switch(letter) {
	case 'h':
		printUsage();
		return stopParsing;
	case 'v':
		printVersion();
		return stopParsing;
	case 'p':
		return parseProgram;
	case 'w':
		return parseWorkdir;
	case 'f':
		return parseFile;
	case 'i':
		return parseInput;
	case 'o':
		return parseOutput;
	case 'e':
		return parseError;
	case 'c':
		options->setCTTY = true;
		return NULL;
	case 't':
		options->openPTY = true;
		return NULL;
	case 's':
		options->useCurrentSession = true;
		return NULL;
	default:
		printf("Unknown option -%c!\n", letter);
		return failParsing;
	};
};

optionParser parseArg(char *arg, struct options *options) {
	if (!strcmp("--", arg))
		return parsePArgs;
	if (arg[0] != '-')
		return NULL;
	optionParser parser = NULL;
	char parserLetter;
	for (int i = 1;;i++) {
		char c = arg[i];
		if (c == '\0')
			break;
		optionParser candidate = parseOption(c, options);
		if (candidate == NULL)
			continue;
		if (candidate == stopParsing || candidate == failParsing)
			return candidate;
		if (parser != NULL) {
			printf("Too many options! Please specify a value for -%c before using -%c.\n", parserLetter, c);
			return failParsing;
		}
		parser = candidate;
		parserLetter = c;
	}

	return parser;
}

void setDefaults(struct options *options) {
	memset(options, 0, sizeof(struct options));
	options->program = "sh";
}

int getOptions(int argc, char **argv, struct options *options) {
	int status;
	setDefaults(options);

	if (argv[0] == NULL)
		return 0;

	int argi = 1;
	optionParser parser = NULL;
	for (;argi < argc;argi++) {
		char *arg = argv[argi];
		if (parser != NULL) {
			status = parser(arg, options);
			if (status > 0)
				return status;
			parser = NULL;
			if (status == -1)
				return -1;
			if (status == -2)
				break;
		} else 
			parser = parseArg(arg, options);
	}
	if (parser != NULL)
		return parser(NULL, options);

	size_t nProgramArgs = argc - argi + 2; // args + terminating NULL + program name
	options->programArgs = malloc(nProgramArgs * sizeof(char*));
	options->programArgs[0] = options->program;
	for (int pargi = argi;pargi <= argc;pargi++)
		options->programArgs[pargi-argi+1] = argv[pargi];

	return 0;
}

int getDefaultOptions(struct options *options) {
	struct passwd* passwd = getpwuid(getuid());
	if (passwd == NULL && passwd->pw_dir == NULL) {
		puts("Failed to get home directory!");
		return 57;
	}

	int home = open(passwd->pw_dir, O_DIRECTORY | O_RDONLY);
	if (home < 0) {
		puts("Failed to open home directory!");
		return 58;
	}
	
	int configFd = openat(home, ".fwtty.config", O_RDONLY);
	if (configFd < 0) {
		setDefaults(options);
		return 0;
	}

	FILE* config = fdopen(configFd, "r");
	if (config == NULL) {
		setDefaults(options);
		return 0;
	}

	char *strings = malloc(1024*sizeof(char));
	strings[0] = '\0';

	char **argv = malloc((32+1)*sizeof(char*));
	argv[0] = strings;

	char* argBegin = &strings[1];
	int i = 1;
	int argc = 1;
	for (int c = fgetc(config); i < 1024; i++, c = fgetc(config)) {
		if (c == EOF)
			break;
		if (c == '\n') {
			c = '\0';
			argv[argc] = argBegin;
			argBegin = &strings[i + 1];
			argc++;
			if (argc >= 32)
				break;
		}
		strings[i] = c;
	}
	argv[argc] = NULL;

	return getOptions(argc, argv, options);
}
