#include "options.h"
#include "version.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Macros to perform stringizing: STRINGIFY(MACRO) will convert the macro expansion of MACRO into a proper string
#define STRINGIFY(value) STR(value)
#define STR(value) #value

// Names for the long options
#define LONGOPT_s "slave"
#define LONGOPT_m "master"

#define LONGOPT_c "clear-logs"
#define LONGOPT_f "config-file"
#define LONGOPT_S "control-interface"
#define LONGOPT_T "terminate-on-error"
#define LONGOPT_h "help"
#define LONGOPT_v "version"

#define LONGOPT_STR_CONSTRUCTOR(LONGOPT_STR) "  --"LONGOPT_STR"\n"

// Long options "struct option" array for getopt_long
static const struct option late_long_opts[]={
	{LONGOPT_h,			no_argument, 		NULL, 'h'},
	{LONGOPT_v,			no_argument, 		NULL, 'v'},
	{LONGOPT_c,			no_argument, 		NULL, 'c'},
	{LONGOPT_f,			required_argument, 	NULL, 'f'},
	{LONGOPT_m,			no_argument, 		NULL, 'm'},
	{LONGOPT_s,			no_argument, 		NULL, 's'},
	{LONGOPT_S,			required_argument,	NULL, 'S'},
	{LONGOPT_S,			no_argument,		NULL, 'T'},
	{NULL, 0, NULL, 0}
};


// Option strings: defined here the description for each option to be then included inside print_long_info()
#define OPT_c_description \
	LONGOPT_STR_CONSTRUCTOR(LONGOPT_c) \
	"  -c: Maste only option. Do not start an actual test but clear all the log files, also on\n" \
	"\t  the slave when it is discovered."

#define OPT_f_description \
	LONGOPT_STR_CONSTRUCTOR(LONGOPT_f) \
	"  -f <file name>: name of the .ini configuration file. Default file name: LTNT.ini.\n"

#define OPT_S_description \
	LONGOPT_STR_CONSTRUCTOR(LONGOPT_S) \
	"  -S <interface name>: name of the control interface for the slave. Mandatory in slave mode.\n"

#define OPT_T_description \
	LONGOPT_STR_CONSTRUCTOR(LONGOPT_T) \
	"  -T: Normally, LTNT works in \"daemon\" mode, i.e. after a failure, the master/slave will start\n" \
	"\t  its execution again, without any intervention of the user. This option can be used to make\n" \
	"\t  LTNT terminate its execution in case an error (not related to LaTe/iperf connectivity) occurs.\n"

static char *ltnt_strdup(const char *src) {
	char *res=malloc(strlen(src)+1);

	if(res==NULL) {
		return res;
	}

	strcpy(res,src);

    return res;
}

static void print_long_info(void) {
	fprintf(stdout,"\nUsage: %s [mode] [-S interface (slave mode only)] [options]\n"
		"[mode]:\n"
		"-m | --"LONGOPT_m": master mode - tests will be coordinated by this node\n"
		"-s | --"LONGOPT_s": slave mode - tests will be coordinated by another master node\n\n"
		"%s [-h | --"LONGOPT_h"]: print help and show options\n"
		"%s [-v | --"LONGOPT_v"]: print version information\n\n"

		"[options]:\n"
		OPT_c_description
		OPT_f_description
		OPT_S_description
		OPT_T_description

		"The source code is available at:\n"
		"%s\n",
		PROG_NAME_SHORT,PROG_NAME_SHORT,PROG_NAME_SHORT, // Basic help
		GITHUB_LINK); // Source code link

	exit(EXIT_SUCCESS);
}

static void print_short_info_err(struct options *options) {
	options_free(options);

	fprintf(stdout,"\nUsage: %s [mode] [-S interface (slave mode only)] [options]\n"
		"[mode]:\n"
		"-m | --"LONGOPT_m": master mode - tests will be coordinated by this node\n"
		"-s | --"LONGOPT_s": slave mode - tests will be coordinated by another master node\n\n"
		"%s [-h | --"LONGOPT_h"]: print help and show options\n"
		"%s [-v | --"LONGOPT_v"]: print version information\n\n",
		PROG_NAME_SHORT,PROG_NAME_SHORT,PROG_NAME_SHORT);
}

void configuration_initialize(struct configuration *configdata) {
	configdata->init_code=INIT_CODE;

	#define CFG(section, name, type, default_value) configdata->name=default_value;
		CONFIG_FIELDS
	#undef CFG
}

int configuration_alloc(struct configuration *configdata) {
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wint-conversion"
	#define CFG(section, name, type, default_value) \
		if(strcmp(#type,"char *")==0) { \
			configdata->name=(char *)malloc(CONFIG_STRING_MALLOC_SIZE*sizeof(char)); \
			if(configdata->name<0) { \
				return -1; \
			} \
		}
		CONFIG_FIELDS
	#undef CFG
	#pragma GCC diagnostic pop

	return 1;
}

void configuration_free(struct configuration *configdata) {
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wint-conversion"
	#define CFG(section, name, type, default_value) \
		if(strcmp(#type,"char *")==0) { \
			if(configdata->name) free(configdata->name); \
		}
		CONFIG_FIELDS
	#undef CFG
	#pragma GCC diagnostic pop
}

void options_initialize(struct options *options) {
	options->init_code=INIT_CODE;

	options->opmode=LTNT_OPMODE_UNSET;

	options->clear_logs=false;
	options->terminate_on_error=false;

	options->slave_control_interface=NULL;

	// Config file options
	options->config_filename_specified=false;
	options->config_filename=NULL;
}

unsigned int parse_options(int argc, char **argv, struct options *options) {
	char char_option;
	bool version_flg=false;
	size_t filenameLen=0;

	if(options->init_code!=INIT_CODE) {
		fprintf(stderr,"parse_options: you are trying to parse the options without initialiting\n"
			"struct options, this is not allowed.\n");
		return 1;
	}

	while ((char_option=getopt_long(argc, argv, VALID_OPTS, late_long_opts, NULL)) != EOF) {
		switch(char_option) {
			case 0:
				fprintf(stderr,"Error. An unexpected error occurred when parsing the options.\n"
					"Please report to the developers that getopt_long() returned 0. Thank you.\n");
				exit(EXIT_FAILURE);
				break;

			case 'c':
				options->clear_logs=true;
				break;

			case 'f':
				filenameLen=strlen(optarg)+1;
				if(filenameLen>1) {
					options->config_filename=malloc(filenameLen*sizeof(char));
					if(!options->config_filename) {
						fprintf(stderr,"Error in parsing the configuration file name: cannot allocate memory.\n");
						print_short_info_err(options);
					}
					strncpy(options->config_filename,optarg,filenameLen);
				} else {
					fprintf(stderr,"Error in parsing the filename: null string length.\n");
					print_short_info_err(options);

					return 1;
				}
				options->config_filename_specified=true;
				break;

			case 'm':
				if(options->opmode==LTNT_OPMODE_UNSET) {
					options->opmode=LTNT_OPMODE_MASTER;
				} else {
					fprintf(stderr,"Error: multiple -s/-m option specified. Only one mode at once can be specified.\n");
					print_short_info_err(options);
					return 1;
				}
				break;

			case 's':
				if(options->opmode==LTNT_OPMODE_UNSET) {
					options->opmode=LTNT_OPMODE_SLAVE;
				} else {
					fprintf(stderr,"Error: multiple -s/-m option specified. Only one mode at once can be specified.\n");
					print_short_info_err(options);
					return 1;
				}
				break;

			case 'S':
				{
				int opt_devnameLen=0;

				if(options->opmode==LTNT_OPMODE_MASTER) {
					fprintf(stderr,"Error: -S can only be specified in slave mode.\n");
					print_short_info_err(options);
				}

				opt_devnameLen=strlen(optarg)+1;
				if(opt_devnameLen>1) {
					options->slave_control_interface=malloc(opt_devnameLen*sizeof(char));
					if(!options->slave_control_interface) {
						fprintf(stderr,"Error in parsing the interface name specified with -S: cannot allocate memory.\n");
						print_short_info_err(options);
						return 1;
					}
					strncpy(options->slave_control_interface,optarg,opt_devnameLen);
				} else {
					fprintf(stderr,"Error in parsing the interface name specified with -S: null string length.\n");
					print_short_info_err(options);
					return 1;
				}
				}
				break;

			case 'T':
				options->terminate_on_error=true;
				break;

			case 'v':
				fprintf(stdout,"%s, version %s, date %04d%02d%02d%s\n",PROG_NAME_LONG,VERSION,LTNT_YEAR,LTNT_MONTH,LTNT_DAY,LTNT_SUBVERSION);
				version_flg=true;
				break;

			case 'h':
				print_long_info();
				break;

			default:
				print_short_info_err(options);
				return 1;

		}

	}

	if(version_flg==true) {
		exit(EXIT_SUCCESS);
	}

	if(options->opmode==LTNT_OPMODE_UNSET) {
		fprintf(stderr,"Error: a mode must be specified (either master or slave).\n");
		return 1;
	}

	if(options->opmode==LTNT_OPMODE_SLAVE && options->clear_logs==true) {
		fprintf(stderr,"Error: -c | --clear-logs is a master only option.\n"
			"The master will then instruct the slave to clear the log files.\n");
		return 1;
	}

	if(options->opmode==LTNT_OPMODE_SLAVE && options->slave_control_interface==NULL) {
		fprintf(stderr,"Error: a slave control interface must be specified with -S | --control-interface\n");
		return 1;
	} else if(options->opmode!=LTNT_OPMODE_SLAVE && options->slave_control_interface!=NULL) {
		fprintf(stderr,"Error: a slave control interface cannot be specified in master mode.\n");
		return 1;
	}

	return 0;
}

void options_free(struct options *options) {
	if(options->config_filename_specified==true && options->config_filename!=NULL) {
		free(options->config_filename);
	}

	if(options->slave_control_interface) {
		free(options->slave_control_interface);
	}
}

int *config_late_payloads_to_array(const char *ini_entry,int *num_payloads_ptr) {
	int num_payloads;
	int *array;
	int array_idx=0;
	char *tokenptr;
	char *ini_entry_strtok=ltnt_strdup(ini_entry);

	if(ini_entry_strtok==NULL) {
		fprintf(stderr,"Error: cannot parse the late_payload_sizes configuration option (out of memory).");
		return NULL;
	}

	// Count the number of ',' in the INI entry
	for(num_payloads=1;ini_entry[num_payloads];ini_entry[num_payloads]==',' ? num_payloads++ : *ini_entry++);

	array=(int *)malloc(num_payloads*sizeof(int));

	if(array==NULL) {
		return array;
	}

	tokenptr=strtok(ini_entry_strtok,",");
	array[array_idx++]=strtol(tokenptr,NULL,10);

	while(tokenptr!=NULL) {
		tokenptr=strtok(NULL,",");

		if(tokenptr!=NULL) {
			errno=0;
			array[array_idx++]=strtol(tokenptr,NULL,10);

			if(errno) {
				fprintf(stderr,"Error: cannot parse the late_payload_sizes configuration option (wrong input).");
				free(array);
				free(ini_entry_strtok);
				return NULL;
			}
		}

	}

	free(ini_entry_strtok);

	*num_payloads_ptr=num_payloads;

	return array;
}