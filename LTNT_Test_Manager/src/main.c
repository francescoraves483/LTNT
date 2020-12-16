#include "errorvals.h"
#include "exect.h"
#include "ini.h"
#include "options.h"
#include "tcpsock.h"
#include "version.h"
#include <arpa/inet.h>
#include <dirent.h> 
#include <errno.h>
#include <linux/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MASTER_NUM_REQ_FILES 5

#define STRSIZE_MAX 1024

#define BIDIR_IDX 0
#define UNIDIR_UL_IDX 1
#define UNIDIR_DL_IDX 2

#define LOGNAME_STR_MAX_SIZE 1024
#define IPERF_CMD_STR_MAX_SIZE 1024
#define LATE_CMD_STR_MAX_SIZE 10240
#define TCP_CTRL_MASTER_READY_SIZE 1024
#define PERIOD_STR_MAX_SIZE 10

typedef struct {
	unsigned int late_bidir;
	unsigned int late_unidir_UL;
	unsigned int late_unidir_DL;
	unsigned int iperf_UDP_UL;
	unsigned int iperf_UDP_DL;
	unsigned int iperf_TCP_UL;
	unsigned int iperf_TCP_DL;
} errcounters_t;

#define LOGDIRNAMES_T_INITIALIZER {NULL,NULL,NULL,NULL,NULL}
typedef struct {
	char *logs_bidir_dir_str;
	char *logs_unidir_UL_dir_str;
	char *logs_unidir_DL_dir_str;
	char *logs_iperf_UL_dir_str;
	char *logs_iperf_DL_dir_str;
	char *logs_ping_dir_str;
} logdirnames_t;

// Function prototypes
static int millisleep(int sleep_ms);
static int rmdir_nonempty(char *path);
static int rmlogs(logdirnames_t *logdirnames);
static int mklogdir(char *dirpath);
static void freelogdirs(logdirnames_t *logdirnames);

// Main function for sending/receiving control packets over the TCP control connection
static int send_tcp_ctrl_packet(int tcp_sockd,char *send_buf,size_t send_buf_size,char *rx_msg);

// INIH library configuration file handler
static int config_handler(void *user, const char *section, const char *name, const char *value);

static inline int killwait(pid_t iperf_pid,int sig,bool iperf_tcp,int iperf_test_duration_sec,errcounters_t *ptrerrors);
static inline void print_errcount_report(FILE *stream,struct options *popts,errcounters_t errors);
static inline struct tm *getLocalTime(void);

static int millisleep(int sleep_ms) {
	struct timespec ts;
	int sleep_res=0;

	ts.tv_sec=sleep_ms/1000;
	ts.tv_nsec=(sleep_ms % 1000)*1000000;

	do {
		sleep_res=nanosleep(&ts,&ts);
	} while (errno==EINTR && sleep_res!=0);

	return sleep_res;
}

static inline int killwait(pid_t iperf_pid,int sig,bool iperf_tcp,int iperf_test_duration_sec,errcounters_t *ptrerrors) {
	int pstatus;
	int waitpid_rval=0;
	int wait_cnt=0;
	bool killed=false;

	kill(iperf_pid,sig);

	while(waitpid_rval==0) {
		waitpid_rval=waitpid(iperf_pid,&pstatus,WNOHANG);

		if(waitpid_rval<0) {
			fprintf(stderr,"iperf server process with PID %d caused an error with waitpid() (wait_cnt=%d).\n",iperf_pid,wait_cnt);
			iperf_tcp==true ? ptrerrors->iperf_TCP_UL++ : ptrerrors->iperf_UDP_UL++;
		} else if(waitpid_rval>0) {
			fprintf(stdout,"iperf server process with PID %d terminated (wait_cnt=%d). Status: %d\n",iperf_pid,wait_cnt,WEXITSTATUS(pstatus));
			if(WEXITSTATUS(pstatus)!=0 && killed==false) {
				iperf_tcp==true ? ptrerrors->iperf_TCP_UL++ : ptrerrors->iperf_UDP_UL++;
			}
		// waitpid_rval==0
		} else {
			millisleep(1000);
			wait_cnt+=1;

			if(!killed && wait_cnt>=iperf_test_duration_sec+IPERF_SRV_WAITPID_TIMEOUT) {
				fprintf(stderr,"iperf server process with PID %d was killed after not responding for around %d seconds.\n",iperf_pid,iperf_test_duration_sec+IPERF_SRV_WAITPID_TIMEOUT);
				kill(iperf_pid,sig);
				iperf_tcp==true ? ptrerrors->iperf_TCP_UL++ : ptrerrors->iperf_UDP_UL++;
				killed=true;
			}
		}
	}

	return waitpid_rval;
}

// static inline int set_sys_tcp_fin_timeout(struct configuration *configs, int *forced_value) {
// 	FILE *fp;
// 	int prev_value=NET_IPV4_TCP_FIN_TIMEOUT_DEFVAUL; // Set to a default value (see options.h)
// 	int set_val;

// 	if(configs==NULL && forced_value==NULL) {
// 		fprintf(stderr,"%s error: unexpected NULL pointers.\n",__func__);
// 		return -1;
// 	}

// 	fp=fopen("/proc/sys/net/ipv4/tcp_fin_timeout","r");

// 	if(fp==NULL) {
// 		fprintf(stderr,"Warning: cannot read net.ipv4.tcp_fin_timeout.\n"
// 			"bind errors may occur in TCP iperf tests if the durations are low (< 1 minute).\n");
// 		return -1;
// 	}

// 	fscanf(fp,"%d",&prev_value);

// 	fclose(fp);

// 	set_val = forced_value!=NULL ? *forced_value : 2*configs->test_duration_late_sec+configs->test_duration_iperf_sec;

// 	fp=fopen("/proc/sys/net/ipv4/tcp_fin_timeout","w");

// 	if(fp==NULL) {
// 		fprintf(stderr,"Warning: cannot change net.ipv4.tcp_fin_timeout.\n"
// 			"bind errors may occur in TCP iperf tests if the durations are low (< 1 minute).\n");
// 		return -1;
// 	}

// 	fprintf(fp,"%d",set_val);

// 	fprintf(stdout,"New value set for net.ipv4.tcp_fin_timeout: %d\n",set_val);

// 	return prev_value;
// }

static inline void print_errcount_report(FILE *stream,struct options *popts,errcounters_t errors) {
	fprintf(stream,"--------------------------------------------------\n");
	fprintf(stream,"-------------ERROR COUNTERS REPORT----------------\n");
	fprintf(stream,"--------------------------------------------------\n");

	if(popts==NULL) {
		fprintf(stream,"---------------NULL pointer error-----------------\n");
	} else {
		fprintf(stream,"-------------- Mode: %s\n",popts->opmode==LTNT_OPMODE_MASTER ? "master" : "slave");
	}

	fprintf(stream,"----------LaTe bidirectional: %d\n",errors.late_bidir);
	fprintf(stream,"----------LaTe unidirectional UL: %d\n",errors.late_unidir_UL);
	fprintf(stream,"----------LaTe unidirectional DL: %d\n",errors.late_unidir_DL);
	fprintf(stream,"----------iperf UL (UDP): %d\n",errors.iperf_UDP_UL);
	fprintf(stream,"----------iperf DL (UDP): %d\n",errors.iperf_UDP_DL);
	fprintf(stream,"----------iperf UL (TCP): %d\n",errors.iperf_TCP_UL);
	fprintf(stream,"----------iperf DL (TCP): %d\n",errors.iperf_TCP_DL);
	fprintf(stream,"--------------------------------------------------\n");
	fprintf(stream,"--------------------------------------------------\n");
	fprintf(stream,"--------------------------------------------------\n");
}

// Function to remove a non-empty directory containing only files (i.e. not containing other directories)
static int rmdir_nonempty(char *path) {
	DIR *dirp;
	struct dirent *dir;

	// Size set to the Linux maximum path length (4096) + '\0'
	char file_path[4097]={0};

	dirp=opendir(path);

	while((dir=readdir(dirp))!=NULL) {
		if(strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0) {
			snprintf(file_path,4096,"%s/%s",path,dir->d_name);
			if(remove(file_path)<0) {
				fprintf(stderr,"Error: cannot remove file %s. Details: %s.\n",file_path,strerror(errno));
			} else {
				fprintf(stderr,"Succesfully removed file %s.\n",file_path);
			}
		}
	}

	closedir(dirp);
	return rmdir(path);
}

static int rmlogs(logdirnames_t *logdirnames) {
	int rm_retval=0;
	int rm_retval_accum=0;

	if(logdirnames!=NULL) {
		if((rm_retval=remove("late_errors_bidir.log"))<0) {
			fprintf(stderr,"Error: cannot remove file %s. Details: %s.\n","late_errors_bidir.log",strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed file: %s\n","late_errors_bidir.log");
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=remove("late_errors_unidir_UL.log"))<0) {
			fprintf(stderr,"Error: cannot remove file %s. Details: %s.\n","late_errors_unidir_UL.log",strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed file: %s\n","late_errors_unidir_UL.log");
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=remove("late_errors_unidir_DL.log"))<0) {
			fprintf(stderr,"Error: cannot remove file %s. Details: %s.\n","late_errors_unidir_DL.log",strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed file: %s\n","late_errors_unidir_DL.log");
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=remove("ping_errors.log"))<0) {
			fprintf(stderr,"Error: cannot remove file %s. Details: %s.\n","ping_errors.log",strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed file: %s\n","ping_errors.log");
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=rmdir_nonempty(logdirnames->logs_bidir_dir_str))<0) {
			fprintf(stderr,"Error: cannot remove logs directory: %s. Details: %s.\n",logdirnames->logs_bidir_dir_str,strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed logs directory: %s\n",logdirnames->logs_bidir_dir_str);
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=rmdir_nonempty(logdirnames->logs_unidir_UL_dir_str))<0) {
			fprintf(stderr,"Error: cannot remove logs directory: %s. Details: %s.\n",logdirnames->logs_unidir_UL_dir_str,strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed logs directory: %s\n",logdirnames->logs_unidir_UL_dir_str);
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=rmdir_nonempty(logdirnames->logs_unidir_DL_dir_str))<0) {
			fprintf(stderr,"Error: cannot remove logs directory: %s. Details: %s.\n",logdirnames->logs_unidir_DL_dir_str,strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed logs directory: %s\n",logdirnames->logs_unidir_DL_dir_str);
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=rmdir_nonempty(logdirnames->logs_iperf_UL_dir_str))<0) {
			fprintf(stderr,"Error: cannot remove logs directory: %s. Details: %s.\n",logdirnames->logs_iperf_UL_dir_str,strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed logs directory: %s\n",logdirnames->logs_iperf_UL_dir_str);
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=rmdir_nonempty(logdirnames->logs_iperf_DL_dir_str))<0) {
			fprintf(stderr,"Error: cannot remove logs directory: %s. Details: %s.\n",logdirnames->logs_iperf_DL_dir_str,strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed logs directory: %s\n",logdirnames->logs_iperf_DL_dir_str);
		}
		rm_retval_accum+=rm_retval;

		if((rm_retval=rmdir_nonempty(logdirnames->logs_ping_dir_str))<0) {
			fprintf(stderr,"Error: cannot remove logs directory: %s. Details: %s.\n",logdirnames->logs_ping_dir_str,strerror(errno));
		} else {
			fprintf(stdout,"Successfully removed logs directory: %s\n",logdirnames->logs_ping_dir_str);
		}
		rm_retval_accum+=rm_retval;
	}

	return rm_retval_accum;
}

static int mklogdir(char *dirpath) {
	int mkdir_rval=0;

	errno=0;
	mkdir_rval=mkdir(dirpath,0700);

	if(mkdir_rval<0 && errno==EEXIST) {
		fprintf(stderr,"Warning: the logs directory '%s' already exists.\n",dirpath);
		errno=0;
	} else if(mkdir_rval<0 && errno!=EEXIST) {
		fprintf(stderr,"Error: cannot create logs directory '%s'. Details: %s.\n",dirpath,strerror(errno));
	} else {
		fprintf(stdout,"successfully created logs directory '%s'.\n",dirpath);
	}

	return errno;
}

static int createlogdirs(struct configuration configs, logdirnames_t *logdirnames) {
	// Create the required directories
	size_t logs_bidir_dir_str_size=strlen(configs.logs_late_bidir)+strlen(configs.test_log_dir)+1;
	size_t logs_unidir_UL_dir_str_size=strlen(configs.logs_late_unidir_UL)+strlen(configs.test_log_dir)+1;
	size_t logs_unidir_DL_dir_str_size=strlen(configs.logs_late_unidir_DL)+strlen(configs.test_log_dir)+1;
	size_t logs_iperf_UL_dir_str_size=strlen(configs.logs_iperf_UL)+strlen(configs.test_log_dir)+1;
	size_t logs_iperf_DL_dir_str_size=strlen(configs.logs_iperf_DL)+strlen(configs.test_log_dir)+1;
	size_t logs_ping_str_size=strlen(configs.logs_ping)+strlen(configs.test_log_dir)+1;

	logdirnames->logs_bidir_dir_str=malloc(logs_bidir_dir_str_size*sizeof(char));
	logdirnames->logs_unidir_UL_dir_str=malloc(logs_unidir_UL_dir_str_size*sizeof(char));
	logdirnames->logs_unidir_DL_dir_str=malloc(logs_unidir_DL_dir_str_size*sizeof(char));
	logdirnames->logs_iperf_UL_dir_str=malloc(logs_iperf_UL_dir_str_size*sizeof(char));
	logdirnames->logs_iperf_DL_dir_str=malloc(logs_iperf_DL_dir_str_size*sizeof(char));
	logdirnames->logs_ping_dir_str=malloc(logs_ping_str_size*sizeof(char));

	int mkdir_rval=0;
	
	if(!logdirnames->logs_bidir_dir_str || !logdirnames->logs_unidir_UL_dir_str || !logdirnames->logs_unidir_DL_dir_str || !logdirnames->logs_iperf_UL_dir_str || !logdirnames->logs_iperf_DL_dir_str || !logdirnames->logs_ping_dir_str) {
		fprintf(stderr,"Error: cannot allocate memory to store the name of the logs directories.\n");
		return ERR_MALLOC;
	}

	strncpy(logdirnames->logs_bidir_dir_str,configs.test_log_dir,logs_bidir_dir_str_size-1);
	strncpy(logdirnames->logs_unidir_UL_dir_str,configs.test_log_dir,logs_unidir_UL_dir_str_size-1);
	strncpy(logdirnames->logs_unidir_DL_dir_str,configs.test_log_dir,logs_unidir_DL_dir_str_size-1);
	strncpy(logdirnames->logs_iperf_UL_dir_str,configs.test_log_dir,logs_iperf_UL_dir_str_size-1);
	strncpy(logdirnames->logs_iperf_DL_dir_str,configs.test_log_dir,logs_iperf_DL_dir_str_size-1);
	strncpy(logdirnames->logs_ping_dir_str,configs.test_log_dir,logs_ping_str_size-1);

	strncat(logdirnames->logs_bidir_dir_str,configs.logs_late_bidir,logs_bidir_dir_str_size-1);
	strncat(logdirnames->logs_unidir_UL_dir_str,configs.logs_late_unidir_UL,logs_unidir_UL_dir_str_size-1);
	strncat(logdirnames->logs_unidir_DL_dir_str,configs.logs_late_unidir_DL,logs_unidir_DL_dir_str_size-1);
	strncat(logdirnames->logs_iperf_UL_dir_str,configs.logs_iperf_UL,logs_iperf_UL_dir_str_size-1);
	strncat(logdirnames->logs_iperf_DL_dir_str,configs.logs_iperf_DL,logs_iperf_DL_dir_str_size-1);
	strncat(logdirnames->logs_ping_dir_str,configs.logs_ping,logs_ping_str_size-1);

	// Accumulating the error values to understand if any error occurred at any of the mklogdir() calls
	// If no error occurs, mkdir_rval should remain equal to 0
	mkdir_rval+=mklogdir(logdirnames->logs_bidir_dir_str);
	mkdir_rval+=mklogdir(logdirnames->logs_unidir_UL_dir_str);
	mkdir_rval+=mklogdir(logdirnames->logs_unidir_DL_dir_str);
	mkdir_rval+=mklogdir(logdirnames->logs_iperf_UL_dir_str);
	mkdir_rval+=mklogdir(logdirnames->logs_iperf_DL_dir_str);
	mkdir_rval+=mklogdir(logdirnames->logs_ping_dir_str);

	if(mkdir_rval!=0) {
		fprintf(stderr,"Error: cannot create the required log directories.\n");
		return ERR_MKDIR_LOGS_DIR;
	}

	return ERR_OK;
}

static void freelogdirs(logdirnames_t *logdirnames) {
	if(logdirnames!=NULL) {
		if(logdirnames->logs_bidir_dir_str) free(logdirnames->logs_bidir_dir_str);
		if(logdirnames->logs_unidir_UL_dir_str) free(logdirnames->logs_unidir_UL_dir_str);
		if(logdirnames->logs_unidir_DL_dir_str) free(logdirnames->logs_unidir_DL_dir_str);
		if(logdirnames->logs_iperf_UL_dir_str) free(logdirnames->logs_iperf_UL_dir_str);
		if(logdirnames->logs_iperf_DL_dir_str) free(logdirnames->logs_iperf_DL_dir_str);
		if(logdirnames->logs_ping_dir_str) free(logdirnames->logs_ping_dir_str);
	}
}

static inline struct tm *getLocalTime(void) {
	time_t currtime=time(NULL);

	return localtime(&currtime);
}

static int send_tcp_ctrl_packet(int tcp_sockd,char *send_buf,size_t send_buf_size,char *rx_msg) {
	bool slave_ready=false;
	int rcv_bytes;
	char tcp_ctrl_rx_buffer[TCP_CTRL_RX_BUF_SIZE];

	// Send messages until the slave sends a reply
	while(slave_ready==false) {
		if(send_buf!=NULL) {
			fprintf(stdout,"Sending '%s'...\n",send_buf);

			if(send(tcp_sockd,send_buf,send_buf_size,0)!=send_buf_size) {
				fprintf(stderr,"Error: cannot send TCP master ready packets.\n");
				return -1;
			}
		}

		if(rx_msg!=NULL) {
			// Wait for possible replies (with a timeout)
			rcv_bytes=recv(tcp_sockd,tcp_ctrl_rx_buffer,TCP_CTRL_RX_BUF_SIZE,0);

			if(rcv_bytes>1 && strcmp(tcp_ctrl_rx_buffer,rx_msg)==0) {
				// A slave is ready
				slave_ready=true;
			} else if(rcv_bytes==-1) {
				if(errno==EAGAIN) {
					fprintf(stderr,"Error: the remote peer seems to be unresponsive.\n");
					return -2;
				} else {
					fprintf(stderr,"Error: cannot receive slave replies on the TCP socket.\n");
					return -1;
				}
			} else {
				fprintf(stderr,"Error: remote peer performed a connection shutdown.\n");
				return -1;
			}
		} else {
			slave_ready=true;
		}
	}

	return 1;
}

static int config_handler(void *user, const char *section, const char *name, const char *value) {
	struct configuration* pconfigs = (struct configuration*) user;

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wint-conversion"
	if(false);
	#define CFG(cfg_section, cfg_name, cfg_type, cfg_default_value) \
		else if(strcmp(cfg_section,section)==0 && strcmp(#cfg_name,name)==0) { \
			if(strcmp(#cfg_type,"int")==0) { \
				errno=0; \
				pconfigs->cfg_name=strtoul(value,NULL,10); \
				if(errno) { \
					fprintf(stderr,"Error in parsing the option: [%s]: %s.\n",cfg_section,#cfg_name); \
				} \
			} else { \
				pconfigs->cfg_name=strdup(value); \
			} \
		}
		CONFIG_FIELDS
	#undef CFG
	#pragma GCC diagnostic pop

	// printf("Option: Section: %s, Name: %s, Value: %s\n",section,name,value);

	// #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	// if (MATCH("protocol", "version")) {
	//     pconfig->version = atoi(value);
	// } else if (MATCH("user", "name")) {
	//     pconfig->name = strdup(value);
	// } else if (MATCH("user", "email")) {
	//     pconfig->email = strdup(value);
	// } else {
	//     return 0;  /* unknown section/name, error */
	// }

	return 1;
}

int main (int argc, char **argv) {
	errorvals_t retval=ERR_OK;

	// Options  structure
	struct options opts;

	// Configuration structure
	struct configuration configs;

	// Current date structure
	struct tm *currdate=NULL;

	// Socket descriptors
	int udp_sockd=-1;
	int tcp_sockd=-1;

	// Error counters
	errcounters_t errors={0};

	// Variables (structs) to store dates (in terms of seconds since the epoch)
	struct timeval now;
	struct timeval now_begin;

	// LaTe payloads array pointer
	int *late_payloads=NULL;
	int late_payloads_size=0;

	// ifreq structure to get the IP of the control interface
	struct ifreq ifreq;

	// Variable to store the control interface IP address
	struct in_addr controlIPaddr;

	// Structure to store the address of received UDP packets
	struct sockaddr_in rxSockAddr;
	socklen_t rxSockAddrLen=sizeof(rxSockAddr);

	// struct timeval to store the different socket timeout used in LTNT
	struct timeval rx_timeout;

	// LaTe and iperf command string buffers
	char iperfcmdstr[IPERF_CMD_STR_MAX_SIZE]={0};
	char latecmdstr[LATE_CMD_STR_MAX_SIZE]={0};

	// LaTe payload length array index
	int payload_lengths_idx;

	// Logs directory names (shall be initialized with LOGDIRNAMES_T_INITIALIZER)
	logdirnames_t logdirnames=LOGDIRNAMES_T_INITIALIZER;

	// iPerf process PID
	pid_t iperf_pid;

	// // Variabile to store the net.ipv4.tcp_fin_timeout initial value (for the iperf TCP tests in which a server is killed)
	// int tcp_fin_timeout_prev_val=NET_IPV4_TCP_FIN_TIMEOUT_DEFVAUL;
	// bool tcp_fin_timeout_changed=false;

	// Read options from command line
	options_initialize(&opts);
	if(parse_options(argc,argv,&opts)) {
		return ERR_OPTIONS;
		goto main_error;
	}

	configuration_initialize(&configs);

	if(opts.opmode==LTNT_OPMODE_MASTER || opts.clear_logs==true) {
		// INIH library function to read INI file
		if(ini_parse(opts.config_filename_specified ? opts.config_filename : DEFAULT_CONFIG_FILE_NAME,config_handler,&configs)<0) {
			fprintf(stderr,"Error: unable to read configuration file: %s.\n",opts.config_filename_specified ? opts.config_filename : DEFAULT_CONFIG_FILE_NAME);
			return ERR_CONFIG;
			goto main_error;
		}

		if((late_payloads=config_late_payloads_to_array(configs.late_payload_sizes,&late_payloads_size))==NULL) {
			retval=ERR_CONFIG;
			goto main_error;
		}

		// Create the "Logs" directories
		if(createlogdirs(configs,&logdirnames)!=ERR_OK) {
			retval=ERR_CONFIG;
			goto main_error;	
		}

		// Check if parameters are consistent
		if(configs.port_iperf<1024 || configs.port_iperf>65535) {
			fprintf(stderr,"Error: invalid iPerf port %d.\n",configs.port_iperf);
			retval=ERR_CONFIG;
			goto main_error;
		}

		if(configs.port_late_bidir<1024 || configs.port_late_bidir>65535) {
			fprintf(stderr,"Error: invalid LaTe RTT testing port %d.\n",configs.port_late_bidir);
			retval=ERR_CONFIG;
			goto main_error;
		}

		if(configs.port_late_unidir_UL<1024 || configs.port_late_unidir_UL>65535) {
			fprintf(stderr,"Error: invalid LaTe UL latency testing port %d.\n",configs.port_late_unidir_UL);
			retval=ERR_CONFIG;
			goto main_error;
		}
			
		if(configs.port_late_unidir_DL<1024 || configs.port_late_unidir_DL>65535) {
			fprintf(stderr,"Error: invalid LaTe DL latency testing port %d.\n",configs.port_late_unidir_DL);
			retval=ERR_CONFIG;
			goto main_error;
		}

		// Check if all the IP addresses have been specified in the configuration file
		if(configs.ip_data_remote==NULL || configs.ip_control_remote==NULL || configs.my_ip_data==NULL || configs.my_ip_control==NULL) {
			fprintf(stderr,"Error: not all the required IP addresses have been specified.\n"
				"Please check the file: %s. (null) IP addresses are the missing ones.\n",opts.config_filename_specified ? opts.config_filename : DEFAULT_CONFIG_FILE_NAME);
			fprintf(stderr,"IP addresses: ip_data_remote=%s\n"
						   "              ip_control_remote=%s\n"
						   "              my_ip_data=%s\n"
						   "              my_ip_control=%s\n",
						   configs.ip_data_remote,configs.ip_control_remote,configs.my_ip_data,configs.my_ip_control);
			retval=ERR_CONFIG;
			goto main_error;
		}

			// Clear logs mode: just clear the logs, without executing an actual slave or master
		if(opts.clear_logs==true) {
			char enter=0;
			int enter_cnt=2;

			fprintf(stdout,"Warning! Clearing the logs triggers a potentially destructive action!\n"
				"Please confirm your intention to perform this action by pressing ENTER three times.\n");

			while(enter_cnt>0) {
				if(enter=='\n') {
					fprintf(stdout,"--- %d ---\n",enter_cnt);
					enter_cnt--;
				}

				enter=getchar();
			}

			printf("Logs will be cleared also on the slave side...\n");

			rmlogs(&logdirnames);
		}
	}


	if(access("/root/LaTe",F_OK)==-1) {
		fprintf(stderr,"Error: LaTe is missing! Please check if the LaTe executable is available in /root.\n");
		retval=ERR_LATE_NOT_FOUND;
		goto main_error;
	}

	if(access("/usr/bin/iperf",F_OK)==-1) {
		fprintf(stderr,"Error: bash is missing! Please check if the bash executable is available in /usr/bin.\n");
		retval=ERR_IPERF_NOT_FOUND;
		goto main_error;
	}

	do {
		iperf_pid=0;
		payload_lengths_idx=0;

		// Check the date
		currdate=getLocalTime();
		if(currdate->tm_year+1900<LTNT_YEAR) {
			fprintf(stderr,"Error: The date appears to be in the past (date year: %d, program version year: %d).\n"
				"Please fix the date before proceeding.\n",currdate->tm_year+1900,LTNT_YEAR);
			retval=ERR_WRONG_DATE;
			goto error;
		}

		// Operating modes: master vs slave
		// --------------------------------------------------
		// --------------------- MASTER ---------------------
		// --------------------------------------------------
		if(opts.opmode==LTNT_OPMODE_MASTER) {
			char udp_disc_packet_content[]="LTNT_SLAVE_DISC";
			char tcp_ctrl_master_ready[TCP_CTRL_MASTER_READY_SIZE];
			char tcp_ctrl_master_ready_printf_specifiers[CONFIG_FIELDS_MAX_NUMBER*2+22];
			char tcp_ctrl_master_server_started[]="LTNT_MASTER_LATE_SERVER_STARTED";
			char tcp_ctrl_master_late_terminated[]="LTNT_MASTER_LATE_TERMINATED";
			char tcp_ctrl_master_iperf_terminated[]="LTNT_MASTER_IPERF_CLIENT_TERMINATED";
			char tcp_ctrl_master_iperf_server_terminated[]="LTNT_MASTER_IPERF_SERVER_TERMINATED";

			char tcp_ctrl_master_clear_logs[]="LTNT_CLEAR_LOGS_COMMAND";

			char tcp_ctrl_master_iperf_server_started[36];

			char udp_disc_rx_buffer[UDP_DISC_RX_BUF_SIZE];

			bool iperf_tcp=false;

			int sendtcpctrlpkt_rval=0;

			gettimeofday(&now_begin,NULL);

			fprintf(stdout,"----- MASTER started -----\n");
			fprintf(stdout,"Test interface: %s\n",configs.test_interface);
			fprintf(stdout,"Control interface: %s\n",configs.control_interface);
			fprintf(stdout,"Master data IP: %s\n",configs.my_ip_data);
			fprintf(stdout,"Slave data IP: %s\n",configs.ip_data_remote);
			fprintf(stdout,"Master control IP: %s\n",configs.my_ip_control);
			fprintf(stdout,"Slave control IP: %s\n",configs.ip_control_remote);
			fprintf(stdout,"Logs directory: %s\n",configs.test_log_dir);
			fprintf(stdout,"--------------------------\n");
			fprintf(stdout,"Current time: %d-%02d-%02d,%02d:%02d:%02d (%lu.%lu)\n",
				currdate->tm_year+1900,currdate->tm_mon+1,currdate->tm_mday,currdate->tm_hour,currdate->tm_min,currdate->tm_sec,
				now_begin.tv_sec,now_begin.tv_usec);

			// Look for slave nodes
			udp_sockd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);

			if(udp_sockd<0) {
				fprintf(stderr,"Error: cannot open UDP socket for slave discovery.\n");
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Get the IP address of the control interface
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wstringop-truncation"
			strncpy(ifreq.ifr_name,configs.control_interface,IFNAMSIZ);
			#pragma GCC diagnostic pop

			ifreq.ifr_addr.sa_family=AF_INET;

			if(ioctl(udp_sockd,SIOCGIFADDR,&ifreq)!=-1) {
				controlIPaddr=((struct sockaddr_in*)&ifreq.ifr_addr)->sin_addr;
			} else {
				fprintf(stderr,"Error: control interface '%s' exists, but no IP address could be found for it.\n",configs.control_interface);
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Enable broadcast on the UDP socket
			int enableBcast=1;
			if(setsockopt(udp_sockd,SOL_SOCKET,SO_BROADCAST,&enableBcast,sizeof(enableBcast))<0) {
				fprintf(stderr,"Error: cannot set broadcast permission on UDP socket for slave discovery.\n");
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Bind UDP socket
			struct sockaddr_in bindSockAddr;
			memset(&bindSockAddr,0,sizeof(struct sockaddr_in));
			bindSockAddr.sin_family=AF_INET;
			bindSockAddr.sin_port=htons(0);
			bindSockAddr.sin_addr.s_addr=controlIPaddr.s_addr;

			if(bind(udp_sockd,(struct sockaddr *) &bindSockAddr,sizeof(struct sockaddr_in))<0) {
				fprintf(stderr,"Error: cannot bind the UDP slave discovery socket to the '%s' interface. IP: %s.\nSocket: %d. Error: %s.\n",configs.control_interface,
					inet_ntoa(controlIPaddr),udp_sockd,strerror(errno));
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Set socket timeout
			rx_timeout.tv_sec=(time_t) ((UDP_DISC_RETRY_TIME_MS)/1000);
			rx_timeout.tv_usec=1000000*UDP_DISC_RETRY_TIME_MS-rx_timeout.tv_sec*1000000000;

			if(setsockopt(udp_sockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
				fprintf(stderr,"Error: could not set RCVTIMEO for the UDP socket.\n");
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Start sending discovery packets
			struct sockaddr_in sendSockAddr;
			ssize_t rcv_bytes;

			memset(&sendSockAddr,0,sizeof(sendSockAddr));
			sendSockAddr.sin_family=AF_INET;
			sendSockAddr.sin_port=htons(UDP_DISC_TCP_CTRL_PORT);
			sendSockAddr.sin_addr.s_addr=INADDR_BROADCAST;

			struct in_addr client_addr;
			client_addr.s_addr=0x00000000;

			fprintf(stdout,"Slave discovery started...\n");
			while(client_addr.s_addr==0x00000000) {
				if(sendto(udp_sockd,udp_disc_packet_content,sizeof(udp_disc_packet_content),0,(struct sockaddr *)&sendSockAddr, sizeof(sendSockAddr))!=sizeof(udp_disc_packet_content)) {
					fprintf(stderr,"Error: cannot send UDP slave discovery packets.\n");
					retval=ERR_DISC_SEND;
					goto error;
				}

				// Wait for possible replies (with a timeout)
				rcv_bytes=recvfrom(udp_sockd,udp_disc_rx_buffer,UDP_DISC_RX_BUF_SIZE,0,(struct sockaddr *)&rxSockAddr,&rxSockAddrLen);

				if(rcv_bytes>1 && strcmp(udp_disc_rx_buffer,"LTNT_SLAVE_DISC_READY")==0) {
					// A slave replied -> store its IP address
					client_addr=rxSockAddr.sin_addr;
				} else if(rcv_bytes==-1) {
					fprintf(stderr,"Warning: did not receive slave replies on the UDP discovery socket for more than %lf seconds. Trying again...\n",
						UDP_DISC_RETRY_TIME_MS/1000.0);
				}
			}

			fprintf(stdout,"Found slave at: %s\n",inet_ntoa(client_addr));

			// When a slave has replied, establish a TCP connection for control data exchange (acting as TCP client)

			// As the UDP connection is no more needed, close the UDP socket
			close(udp_sockd);
			udp_sockd=-1;

			// Create the TCP socket
			tcp_sockd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

			if(tcp_sockd<0) {
				fprintf(stderr,"Error: cannot open TCP socket for control data exchange.\n");
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// Bind TCP socket
			if(bind(tcp_sockd,(struct sockaddr *) &bindSockAddr,sizeof(bindSockAddr))<0) {
				fprintf(stderr,"Error: cannot bind the TCP control data exchange socket to the '%s' interface.\n",configs.control_interface);
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// TCP socket timeout (larger than the UDP socket timeout)
			rx_timeout.tv_sec=(time_t) ((TCP_CTRL_GIVEUP_TIME_MS)/1000);
			rx_timeout.tv_usec=((long)1000000*TCP_CTRL_GIVEUP_TIME_MS)-rx_timeout.tv_sec*1000000000;

			if(setsockopt(tcp_sockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
				fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket.\n");
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// Connect to the slave
			struct sockaddr_in connect_addrin;
			int connect_rval;

			connect_addrin.sin_addr=client_addr;
			connect_addrin.sin_port=htons(UDP_DISC_TCP_CTRL_PORT);
			connect_addrin.sin_family=AF_INET;

			fprintf(stdout,"Connecting to the slave (TCP port: %d)...\n",UDP_DISC_TCP_CTRL_PORT);
			if((connect_rval=connectWithTimeout2(tcp_sockd,(struct sockaddr *) &(connect_addrin),sizeof(connect_addrin),TCP_CTRL_TIMEOUT_MS,true,NULL))!=0) {
				fprintf(stderr,"Error: cannot perform the TCP connection to the slave. Details: (%d) %s.\n",connect_rval,connectWithTimeoutStrError(connect_rval));
				retval=ERR_TCP_SOCKET;
				goto error;
			}
			fprintf(stdout,"Connection successful!\n");

			// Send "MASTER_READY" messages, and wait for the slave to reply with "SLAVE_READY"

			fprintf(stdout,"Sending MASTER_READY to %s:%d...\n",inet_ntoa(client_addr),UDP_DISC_TCP_CTRL_PORT);

			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wint-conversion"
			snprintf(tcp_ctrl_master_ready_printf_specifiers,CONFIG_FIELDS_MAX_NUMBER*2+22,
				"LTNT_MASTER_READY$"
				#define CFG(section, name, type, default_value) "%%%c$"
					CONFIG_FIELDS
				#undef CFG
				"%%%c",
				#define CFG(section, name, type, default_value) strcmp(#type,"int")==0 ? 'd' : 's',
					CONFIG_FIELDS
				#undef CFG
				's');
			#pragma GCC diagnostic pop

			snprintf(tcp_ctrl_master_ready,TCP_CTRL_MASTER_READY_SIZE,
				tcp_ctrl_master_ready_printf_specifiers,
				#define CFG(section, name, type, default_value) configs.name,
					CONFIG_FIELDS
				#undef CFG
				"end");

			fprintf(stdout,"LTNT_MASTER_READY message content: %s\n",tcp_ctrl_master_ready);

			if(send(tcp_sockd,tcp_ctrl_master_ready,strlen(tcp_ctrl_master_ready)+1,0)!=strlen(tcp_ctrl_master_ready)+1) {
				fprintf(stderr,"Error: cannot send TCP master ready packets.\n");
				retval=ERR_TCP_SEND;
				goto error;
			}

			sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,NULL,0,"LTNT_SLAVE_READY");

			if(sendtcpctrlpkt_rval==-1) {
				retval=ERR_TCP_SEND;
				goto error;
			} else if(sendtcpctrlpkt_rval==-2) {
				retval=ERR_SLAVE_UNRESPONSIVE;
				goto error;
			}

			fprintf(stdout,"Received SLAVE_READY from %s:%d...\n"
				"The slave is ready to start the measurement session!\n",
				inet_ntoa(client_addr),UDP_DISC_TCP_CTRL_PORT);

			if(opts.clear_logs==true) {
				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_master_clear_logs,sizeof(tcp_ctrl_master_clear_logs),NULL);
				retval=ERR_OK;
				goto error;
			}

			// Start the LaTe and iperf tests
			while(true) {
				pid_t late_pids[3]={0};

				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting LaTe unidirectional (DL: slave->master) server @ %lu...\n",now.tv_sec);

				// Spawn the DL (slave->master) latency server
				if((late_pids[UNIDIR_DL_IDX]=fork())<0) {
					fprintf(stderr,"Error: cannot spawn LaTe unidirectional (DL) process (server).\n");
					errors.late_unidir_DL++;
				} else if(late_pids[UNIDIR_DL_IDX]==0) {
					close(udp_sockd);
					close(tcp_sockd);

					// Child work (exec)
					snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/root/LaTe -s -u -p %d -t 10000 -S %s -W %s/LaTe_unidir_DL_P_%d_%lu_perpkt "
						"-X mnrp --initial-timeout >1 /dev/null >a2 late_errors_unidir_DL.log",
						configs.port_late_unidir_DL,
						configs.test_interface,
						logdirnames.logs_unidir_DL_dir_str,late_payloads[payload_lengths_idx],now.tv_sec);

					if(exect(latecmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn LaTe unidirectional (DL) process (server). exect() error.\n");
						errors.late_unidir_DL++;
					}
				}

				millisleep(configs.late_sleep_between_clients_ms);

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_master_server_started,sizeof(tcp_ctrl_master_server_started),"LTNT_SLAVE_LATE_SERVER_STARTED");
				
				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto error;
				}

				// Start the clients
				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting LaTe bidirectional (master<->slave) client @ %lu...\n",now.tv_sec);

				if((late_pids[BIDIR_IDX]=fork())<0) {
					fprintf(stderr,"Error: cannot spawn LaTe bidirectional process (client).\n");
					errors.late_bidir++;
				} else if(late_pids[BIDIR_IDX]==0) {
					close(udp_sockd);
					close(tcp_sockd);

					// Child work (exec)
					snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/root/LaTe -c %s -u -B -P %d -t %d -R e%d,%d -i %d -p %d -T 10000 -S %s "
						"-W %s/LaTe_bidir_P_%d_%lu_perpkt -X mnrp -f %s/LaTe_bidir_%lu_final >1 /dev/null >a2 late_errors_bidir.log",
						configs.ip_data_remote,
						late_payloads[payload_lengths_idx],
						configs.late_min_periodicity,
						configs.late_mean_periodicity,configs.late_periodicity_batch,
						configs.test_duration_late_sec,
						configs.port_late_bidir,
						configs.test_interface,
						logdirnames.logs_bidir_dir_str,late_payloads[payload_lengths_idx],now.tv_sec,
						logdirnames.logs_bidir_dir_str,now_begin.tv_sec);
					if(exect(latecmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn LaTe unidirectional (DL) process (server). exect() error.\n");
						errors.late_bidir++;
					}
				}

				millisleep(configs.late_sleep_between_clients_ms);

				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting LaTe unidirectional (UL: master->slave) client @ %lu...\n",now.tv_sec);

				if((late_pids[UNIDIR_UL_IDX]=fork())<0) {
					fprintf(stderr,"Error: cannot spawn LaTe unidirectional (UL) process (client).\n");
					errors.late_unidir_UL++;
				} else if(late_pids[UNIDIR_UL_IDX]==0) {
					close(udp_sockd);
					close(tcp_sockd);

					// Child work (exec)
					snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/root/LaTe -c %s -u -U -P %d -t %d -R e%d,%d -i %d -p %d -T 10000 -S %s "
						"-f %s/LaTe_unidir_UL_%lu_final >1 /dev/null >a2 late_errors_unidir_UL.log",
						configs.ip_data_remote,
						late_payloads[payload_lengths_idx],
						configs.late_min_periodicity,
						configs.late_mean_periodicity,configs.late_periodicity_batch,
						configs.test_duration_late_sec,
						configs.port_late_unidir_UL,
						configs.test_interface,
						logdirnames.logs_unidir_UL_dir_str,now_begin.tv_sec);
					if(exect(latecmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn LaTe unidirectional (DL) process (server). exect() error.\n");
						errors.late_unidir_UL++;
					}
				}

				fprintf(stdout,"LaTe process PID [bidirectional]: %d (LaMP payload size: %d)\n"
					"LaTe process PID [unidirectional UL]: %d (LaMP payload size: %d)\n"
					"LaTe process PID [unidirectional DL]: %d (LaMP payload size: %d)\n",
					late_pids[BIDIR_IDX],late_payloads[payload_lengths_idx],
					late_pids[UNIDIR_UL_IDX],late_payloads[payload_lengths_idx],
					late_pids[UNIDIR_DL_IDX],late_payloads[payload_lengths_idx]);

				// Start also ping, if required through the -p option
				pid_t ping_pid=0;

				if(opts.add_ping==true) {
					gettimeofday(&now,NULL);
					fprintf(stdout,"Starting ping (RTT: master<->slave) @ %lu...\n",now.tv_sec);

					if((ping_pid=fork())<0) {
						fprintf(stderr,"Error: cannot spawn ping process.\n");
					} else if(ping_pid==0) {
						close(udp_sockd);
						close(tcp_sockd);

						// Child work (exec) - we can reuse the variable "latecmdstr"
						snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/usr/bin/ping -i %lf -s %d %s >1 %s/ping_log_%lu.txt >a2 ping_errors.log",
							((double) configs.ping_periodicity_ms)/1000.0,
							late_payloads[payload_lengths_idx]+24,
							configs.ip_data_remote,
							logdirnames.logs_ping_dir_str,
							now_begin.tv_sec);
						if(exect(latecmdstr)<0) {
							fprintf(stderr,"Error: cannot spawn ping process (master). exect() error.\n");
						}
					}
				}

				pid_t wpid;
				int pstatus;
				int terminated_instances=0;

				// Wait for all the children to finish
				while((wpid=wait(&pstatus))>0) {
					fprintf(stdout,"LaTe process with PID %d terminated. Status: %d\n",wpid,WEXITSTATUS(pstatus));

					if(opts.add_ping==true) {
						if(wpid==late_pids[BIDIR_IDX]) {
							terminated_instances++;
						} else if(wpid==late_pids[UNIDIR_UL_IDX]) {
							terminated_instances++;
						} else if(wpid==late_pids[UNIDIR_DL_IDX]) {
							terminated_instances++;
						}

						if(terminated_instances==3) {
							break;
						}
					}

					if(WEXITSTATUS(pstatus)!=0) {
						if(wpid==late_pids[BIDIR_IDX]) {
							errors.late_bidir++;
						} else if(wpid==late_pids[UNIDIR_UL_IDX]) {
							errors.late_unidir_UL++;
						} else if(wpid==late_pids[UNIDIR_DL_IDX]) {
							errors.late_unidir_DL++;
						} else {
							fprintf(stderr,"Error: LTNT was waiting for an unknown process with PID %d.\n",wpid);
						}
					}
				}

				// Kill ping, if it was started
				if(opts.add_ping==true) {
					kill(ping_pid,SIGKILL);
					waitpid(iperf_pid,&pstatus,0);

					fprintf(stdout,"ping %d terminated. Status: %d\n",wpid,WEXITSTATUS(pstatus));
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_master_late_terminated,sizeof(tcp_ctrl_master_late_terminated),"LTNT_SLAVE_LATE_TERMINATED");

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto error;
				}

				if(late_payloads_size>1) {
					payload_lengths_idx=(payload_lengths_idx+1)%late_payloads_size;
				}

				// // In case of an iperf TCP test, change the value of net.ipv4.tcp_fin_timeout to avoid bind errors in the following TCP tests
				// if(iperf_tcp==true) {
				// 	tcp_fin_timeout_prev_val=set_sys_tcp_fin_timeout(&configs,NULL);

				// 	if(tcp_fin_timeout_prev_val<0) {
				// 		retval=ERR_SET_TCP_FIN_TIMEOUT;
				// 		goto error;
				// 	}

				// 	tcp_fin_timeout_changed=true;
				// }

				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting iperf DL (slave->master) server @ %lu (%s)...\n",now.tv_sec,iperf_tcp==true ? "tcp" : "udp");

				if((iperf_pid=fork())<0) {
					fprintf(stderr,"Error: cannot spawn the iperf server for the DL (slave->master) test (%s).\n",iperf_tcp==true ? "tcp" : "udp");
					iperf_tcp==true ? errors.iperf_TCP_DL++ : errors.iperf_UDP_DL++;
				} else if(iperf_pid==0) {
					close(udp_sockd);
					close(tcp_sockd);

					// Child work (exec)
					snprintf(iperfcmdstr,IPERF_CMD_STR_MAX_SIZE,"/usr/bin/iperf -s %s -i 1 -p %d -l %s -y C >1 %s/iperf_throughput_%s_%lu.csv",
						iperf_tcp==true ? "" : "-u",
						configs.port_iperf,
						iperf_tcp==true ? configs.TCP_iperf_buf_len : configs.UDP_iperf_packet_len,
						logdirnames.logs_iperf_DL_dir_str,iperf_tcp==true ? "tcp" : "udp",now.tv_sec);
					if(exect(iperfcmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn the iperf server for the DL (slave->master) test (%s). exect() error.\n",iperf_tcp==true ? "tcp" : "udp");
						iperf_tcp==true ? errors.iperf_TCP_DL++ : errors.iperf_UDP_DL++;
					}
				}

				fprintf(stdout,"iperf DL test in progress (protocol: %s) (PID: %d)\n",iperf_tcp==true ? "tcp" : "udp",iperf_pid);

				// Set a TCP timeout equal to the duration of the whole iperf test, plus some margin equal to TCP_CTRL_GIVEUP_TIME_MS*
				rx_timeout.tv_sec=(time_t) configs.test_duration_iperf_sec;
				rx_timeout.tv_usec=((long)1000000*configs.test_duration_iperf_sec)-rx_timeout.tv_sec*1000000;

				rx_timeout.tv_sec+=(TCP_CTRL_GIVEUP_TIME_MS/1000);

				if(setsockopt(tcp_sockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
					fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket (iperf test timeout). Details: %s.",strerror(errno));
					fprintf(stderr,"Was attempting to set timeout to %lu seconds and %lu microseconds.\n",rx_timeout.tv_sec,rx_timeout.tv_usec);
					retval=ERR_TCP_SOCKET;
					goto error;
				}

				snprintf(tcp_ctrl_master_iperf_server_started,36,"LTNT_MASTER_IPERF_SERVER_STARTED,%d",iperf_tcp);

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_master_iperf_server_started,sizeof(tcp_ctrl_master_late_terminated),"LTNT_SLAVE_IPERF_CLIENT_TERMINATED");

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto error;
				}

				// When the client has terminated, kill the iperf server
				killwait(iperf_pid,SIGKILL,iperf_tcp,configs.test_duration_iperf_sec,&errors);

				// // Restore net.ipv4.tcp_fin_timeout (master)
				// if(iperf_tcp==true && tcp_fin_timeout_changed==true) {
				// 	set_sys_tcp_fin_timeout(&configs,&tcp_fin_timeout_prev_val);
				// 	if(tcp_fin_timeout_prev_val<0) {
				// 		retval=ERR_SET_TCP_FIN_TIMEOUT;
				// 		goto error;
				// 	}
				// 	tcp_fin_timeout_changed=false;
				// }

				// Restore the TCP socket timeout*
				rx_timeout.tv_sec=(time_t) ((TCP_CTRL_GIVEUP_TIME_MS)/1000);
				rx_timeout.tv_usec=((long)1000000*TCP_CTRL_GIVEUP_TIME_MS)-rx_timeout.tv_sec*1000000000;

				if(setsockopt(tcp_sockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
					fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket.\n");
					retval=ERR_TCP_SOCKET;
					goto error;
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_master_iperf_server_terminated,sizeof(tcp_ctrl_master_iperf_server_terminated),"LTNT_SLAVE_IPERF_SERVER_STARTED");

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto error;
				}

				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting iperf UL (master->slave) client @ %lu (%s)...\n",now.tv_sec,iperf_tcp==true ? "tcp" : "udp");

				if((iperf_pid=fork())<0) {
					fprintf(stderr,"Error: cannot spawn the iperf client for the UL (master->slave) test (%s).\n",iperf_tcp==true ? "tcp" : "udp");
					iperf_tcp==true ? errors.iperf_TCP_UL++ : errors.iperf_UDP_UL++;
				} else if(iperf_pid==0) {
					close(udp_sockd);
					close(tcp_sockd);

					// Child work (exec)
					snprintf(iperfcmdstr,IPERF_CMD_STR_MAX_SIZE,"/usr/bin/iperf -c %s %s -p %d -l %s -i 1 -t %d -b 1G >1 /dev/null >2 /dev/null",
						configs.ip_data_remote,
						iperf_tcp==true ? "" : "-u -P 3",
						configs.port_iperf,
						iperf_tcp==true ? configs.TCP_iperf_buf_len : configs.UDP_iperf_packet_len,
						configs.test_duration_iperf_sec);
					if(exect(iperfcmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn the iperf client for the UL (master->slave) test (%s). exect() error.\n",iperf_tcp==true ? "tcp" : "udp");
						iperf_tcp==true ? errors.iperf_TCP_UL++ : errors.iperf_UDP_UL++;
					}
				}

				fprintf(stdout,"iperf UL test in progress (protocol: %s) (PID: %d)\n",iperf_tcp==true ? "tcp" : "udp",iperf_pid);

				// Wait for the client to terminate
				if(waitpid(iperf_pid,&pstatus,0)<0) {
					fprintf(stderr,"iperf client process with PID %d caused an error with waitpid().\n",iperf_pid);
					iperf_tcp==true ? errors.iperf_TCP_UL++ : errors.iperf_UDP_UL++;
				} else {
					fprintf(stdout,"iperf client process with PID %d terminated. Status: %d\n",iperf_pid,WEXITSTATUS(pstatus));
					if(WEXITSTATUS(pstatus)!=0) {
						iperf_tcp==true ? errors.iperf_TCP_UL++ : errors.iperf_UDP_UL++;
					}
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_master_iperf_terminated,sizeof(tcp_ctrl_master_iperf_terminated),"LTNT_SLAVE_IPERF_SERVER_TERMINATED");
				fprintf(stdout,"Received LTNT_SLAVE_IPERF_SERVER_TERMINATED from the slave (UL test).\n");

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto error;
				}

				if(iperf_tcp==false) {
					iperf_tcp=true;
				} else {
					iperf_tcp=false;
				}

				fprintf(stdout,"Master iteration terminated...\n");
				print_errcount_report(stdout,&opts,errors);
			}
		// --------------------- SLAVE ---------------------
		} else {
			char udp_disc_packet_content[]="LTNT_SLAVE_DISC_READY";
			char udp_disc_rx_buffer[UDP_DISC_RX_BUF_SIZE];
			char tcp_ctrl_master_ready[TCP_CTRL_MASTER_READY_SIZE];
			char tcp_ctrl_master_ready_scanf_specifiers[CONFIG_FIELDS_MAX_NUMBER*2+22];

			char tcp_ctrl_slave_ready[]="LTNT_SLAVE_READY";
			char tcp_ctrl_slave_late_server_started[]="LTNT_SLAVE_LATE_SERVER_STARTED";
			char tcp_ctrl_slave_late_terminated[]="LTNT_SLAVE_LATE_TERMINATED";
			char tcp_ctrl_slave_iperf_client_terminated[]="LTNT_SLAVE_IPERF_CLIENT_TERMINATED";
			char tcp_ctrl_slave_iperf_server_started[]="LTNT_SLAVE_IPERF_SERVER_STARTED";
			char tcp_ctrl_slave_iperf_server_terminated[]="LTNT_SLAVE_IPERF_SERVER_TERMINATED";

			bool iperf_tcp=false;

			int listen_tcpsockd;

			// Wait for discovery packets from a master node
			udp_sockd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);

			if(udp_sockd<0) {
				fprintf(stderr,"Error: cannot open UDP socket for master-slave association.\n");
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Get the IP address of the control interface
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wstringop-truncation"
			strncpy(ifreq.ifr_name,opts.slave_control_interface,IFNAMSIZ);
			#pragma GCC diagnostic pop

			ifreq.ifr_addr.sa_family=AF_INET;

			if(ioctl(udp_sockd,SIOCGIFADDR,&ifreq)!=-1) {
				controlIPaddr=((struct sockaddr_in*)&ifreq.ifr_addr)->sin_addr;
			} else {
				fprintf(stderr,"Error: control interface '%s' exists, but no IP address could be found for it.\n",opts.slave_control_interface);
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Bind UDP socket
			struct sockaddr_in bindSockAddr;
			memset(&bindSockAddr,0,sizeof(bindSockAddr));
			bindSockAddr.sin_family=AF_INET;
			bindSockAddr.sin_port=htons(UDP_DISC_TCP_CTRL_PORT);
			bindSockAddr.sin_addr.s_addr=INADDR_ANY;

			if(bind(udp_sockd,(struct sockaddr *) &bindSockAddr,sizeof(bindSockAddr))<0) {
				fprintf(stderr,"Error: cannot bind the UDP master discovery socket to the '%s' interface.\n",opts.slave_control_interface);
				retval=ERR_UDP_SOCKET;
				goto error;
			}

			// Start receiving packets from a master node
			ssize_t rcv_bytes;
			bool master_found=false;
			struct in_addr master_addr;

			while(master_found==false) {
				fprintf(stdout,"Waiting for a master...\n");
				rcv_bytes=recvfrom(udp_sockd,udp_disc_rx_buffer,UDP_DISC_RX_BUF_SIZE,0,(struct sockaddr *)&rxSockAddr,&rxSockAddrLen);
				fprintf(stdout,"Received a new message: %s\n",udp_disc_rx_buffer);

				if(rcv_bytes>1 && strcmp(udp_disc_rx_buffer,"LTNT_SLAVE_DISC")==0) {
					// Received a message from a master -> exit from the loop, open TCP socket and send the reply to that master node only
					master_addr=rxSockAddr.sin_addr;
					master_found=true;
				} else if(rcv_bytes==-1) {
					fprintf(stderr,"Warning: cannot receive master discovery replies on the UDP discovery socket. Trying again...\n");
				}
			}

			fprintf(stdout,"Heard a master. Opening TCP socket and sending LTNT_SLAVE_DISC_READY to %s:%d...\n",inet_ntoa(master_addr),rxSockAddr.sin_port);

			// Open TCP socket
			// Create the TCP socket
			listen_tcpsockd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

			if(listen_tcpsockd<0) {
				fprintf(stderr,"Error: cannot open TCP socket for control data exchange.\n");
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// Bind TCP socket
			if(bind(listen_tcpsockd,(struct sockaddr *) &bindSockAddr,sizeof(bindSockAddr))<0) {
				fprintf(stderr,"Error: cannot bind the TCP control data exchange socket to the '%s' interface.\n",opts.slave_control_interface);
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// TCP socket timeout (the same as the master TCP socket timeout)
			rx_timeout.tv_sec=(time_t) ((TCP_CTRL_GIVEUP_TIME_MS)/1000);
			rx_timeout.tv_usec=((long)1000000*TCP_CTRL_GIVEUP_TIME_MS)-rx_timeout.tv_sec*1000000000;

			if(setsockopt(listen_tcpsockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
				fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket.\n");
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// Listen for upcoming connections
			if(listen(listen_tcpsockd,1)<0) {
				fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket.\n");
				retval=ERR_TCP_SOCKET_CANNOT_LISTEN;
				goto error;
			}

			// Send LTNT_SLAVE_DISC_READY
			struct sockaddr_in sendSockAddr;

			memset(&sendSockAddr,0,sizeof(sendSockAddr));
			sendSockAddr.sin_family=AF_INET;
			sendSockAddr.sin_port=rxSockAddr.sin_port;
			sendSockAddr.sin_addr.s_addr=rxSockAddr.sin_addr.s_addr;

			if(sendto(udp_sockd,udp_disc_packet_content,sizeof(udp_disc_packet_content),0,(struct sockaddr *)&sendSockAddr, sizeof(sendSockAddr))!=sizeof(udp_disc_packet_content)) {
				fprintf(stderr,"Error: cannot send LTNT_SLAVE_DISC_READY packets.\n");
				retval=ERR_DISC_SEND;
				goto error;
			}

			// Accept connection from master
			int connect_rval=0;

			//connect_rval=connectWithTimeout2(listen_tcpsockd,NULL,0,TCP_CTRL_TIMEOUT_MS,false,&tcp_sockd);
			tcp_sockd=accept(listen_tcpsockd,NULL,NULL);

			if(tcp_sockd<0 || connect_rval<0) {
				fprintf(stderr,"Error: cannot accept master connection. accept() inside connectWithTimeout() failed. Details: (%d) %s\n",connect_rval,connectWithTimeoutStrError(connect_rval));
				retval=ERR_TCP_SOCKET;
				goto error;
			}

			// Close UDP socket, as it is no more needed now
			close(udp_sockd);

			fprintf(stdout,"Connection from master accepted successfully.\n");

			bool master_ready_received=false;
			while(master_ready_received==false) {
				rcv_bytes=recv(tcp_sockd,tcp_ctrl_master_ready,TCP_CTRL_MASTER_READY_SIZE,0);

				if(rcv_bytes>1 && strncmp(tcp_ctrl_master_ready,"LTNT_MASTER_READY",17)==0) {
					int junk=0;

					fprintf(stdout,"Received the configuration information from the master. Parsing...\n");

					// Master ready message received - parse all the configuration data
					#pragma GCC diagnostic push
					#pragma GCC diagnostic ignored "-Wint-conversion"
					snprintf(tcp_ctrl_master_ready_scanf_specifiers,CONFIG_FIELDS_MAX_NUMBER*2+22,
						"LTNT_MASTER_READY$"
						#define CFG(section, name, type, default_value) "%%%s$"
							CONFIG_FIELDS
						#undef CFG
						"%%*%c",
						#define CFG(section, name, type, default_value) strcmp(#type,"int")==0 ? "d" : "[^$]",
							CONFIG_FIELDS
						#undef CFG
						's');
					#pragma GCC diagnostic pop

					// Allocate memory before reading the configuration
					if(configuration_alloc(&configs)<0) {
						fprintf(stderr,"Error: cannot allocate memory for received configuration information.\n");
						retval=ERR_MALLOC;
						goto slave_error;
					}

					fprintf(stdout,"Reading config with specifiers: %s. Message: %s\n",
						tcp_ctrl_master_ready_scanf_specifiers,
						tcp_ctrl_master_ready);

					char *tokenptr=NULL;
					tokenptr=strtok(tcp_ctrl_master_ready,"$");

					if(tokenptr!=NULL) {
						#pragma GCC diagnostic push
						#pragma GCC diagnostic ignored "-Wint-conversion"
						#define CFG(section, name, type, default_value) \
						tokenptr=strtok(NULL,"$"); \
						if(tokenptr!=NULL) { \
							fprintf(stdout,"Parsing: %s/%s: %s.\n",section,#name,tokenptr); \
							if(strcmp(#type,"int")==0) { \
								errno=0; \
								configs.name=strtol(tokenptr,NULL,10); \
								if(errno) { \
									fprintf(stderr,"Error: cannot parse the configuration option: %s/%s: %s.\n",section,#name,tokenptr); \
									retval=ERR_CONFIG_PARSE; \
									goto slave_error; \
								} \
							} else if(strcmp(#type,"char *")==0) { \
								strcpy(configs.name,tokenptr); \
							} \
						}

							CONFIG_FIELDS
						#undef CFG
						#pragma GCC diagnostic pop
					}

					if((late_payloads=config_late_payloads_to_array(configs.late_payload_sizes,&late_payloads_size))==NULL) {
						fprintf(stderr,"Error: the slave cannot parse the LaTe payload sizes.\n");
						retval=ERR_CONFIG;
						goto slave_error;
					}

					sscanf(tcp_ctrl_master_ready,
						tcp_ctrl_master_ready_scanf_specifiers,
						#define CFG(section, name, type, default_value) &(configs.name),
							CONFIG_FIELDS
						#undef CFG
						&junk);

					if(createlogdirs(configs,&logdirnames)!=ERR_OK) {
						fprintf(stderr,"Error: the slave cannot create the log directories.\n");
						retval=ERR_CONFIG;
						goto slave_error;	
					}

					master_ready_received=true;
				} else if(rcv_bytes==-1) {
					if(errno==EAGAIN) {
						fprintf(stderr,"Error: the master seems to be unresponsive.\n");
						retval=ERR_TCP_SOCKET;
						goto slave_error;
					} else {
						fprintf(stderr,"Error: cannot receive master control messages on the TCP socket.\n");
						retval=ERR_TCP_SOCKET;
						goto slave_error;
					}
				}
			}

			fprintf(stdout,"--------------------------\n");
			fprintf(stdout,"Parsed the configuration information from the master:\n");
			fprintf(stdout,"Test interface: %s\n",configs.test_interface);
			fprintf(stdout,"Control interface: %s\n",configs.control_interface);
			fprintf(stdout,"Master test IP: %s\n",configs.my_ip_data);
			fprintf(stdout,"Slave test IP: %s\n",configs.ip_data_remote);
			fprintf(stdout,"Master control IP: %s\n",configs.my_ip_control);
			fprintf(stdout,"Slave control IP: %s\n",configs.ip_control_remote);
			fprintf(stdout,"LaTe payloads min-max: %d->%d\n",late_payloads[0],late_payloads[late_payloads_size-1]);
			fprintf(stdout,"--------------------------\n");

			gettimeofday(&now_begin,NULL);

			send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_slave_ready,sizeof(tcp_ctrl_slave_ready),NULL);

			// Start the actual tests
			while(true) {
				int sendtcpctrlpkt_rval=0;
				pid_t late_pids[3]={0};
				char tcp_ctrl_rx_buffer_local[TCP_CTRL_RX_BUF_SIZE];

				// Waiting for a "LTNT_MASTER_LATE_SERVER_READY" message from the master
				fprintf(stdout,"Waiting for LTNT_MASTER_LATE_SERVER_STARTED...\n");
				//sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,NULL,0,"LTNT_MASTER_LATE_SERVER_STARTED");

				rcv_bytes=recv(tcp_sockd,tcp_ctrl_rx_buffer_local,TCP_CTRL_RX_BUF_SIZE,0);

				if(rcv_bytes>1 && strcmp(tcp_ctrl_rx_buffer_local,"LTNT_CLEAR_LOGS_COMMAND")==0) {
					fprintf(stdout,"...but received a clear logs command!\n");
					rmlogs(&logdirnames);
					retval=ERR_OK;
					goto slave_error;
				} else if(rcv_bytes==-1) {
					if(errno==EAGAIN) {
						fprintf(stderr,"Error: the remote peer seems to be unresponsive.\n");
						retval=ERR_SLAVE_UNRESPONSIVE;
						goto slave_error;
					} else {
						fprintf(stderr,"Error: cannot receive slave replies on the TCP socket.\n");
						retval=ERR_TCP_RECV;
						goto slave_error;
					}
				} else if(rcv_bytes>1 && strcmp(tcp_ctrl_rx_buffer_local,"LTNT_MASTER_LATE_SERVER_STARTED")!=0) {
					fprintf(stderr,"Error: unexpected message received: %s.\n",tcp_ctrl_rx_buffer_local);
					retval=ERR_TCP_RECV;
					goto slave_error;
				} else if(rcv_bytes==0) {
					fprintf(stderr,"Error: remote peer performed a connection shutdown.\n");
					retval=ERR_TCP_RECV;
					goto slave_error;
				}

				// After the server is ready, spawn the RTT and UL (master->slave) LaTe servers
				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting LaTe bidirectional (master<->slave) server @ %lu...\n",now.tv_sec);

				if((late_pids[BIDIR_IDX]=fork())<0) {
					fprintf(stderr,"Error: cannot spawn LaTe bidirectional process (server).\n");
					errors.late_bidir++;
				} else if(late_pids[BIDIR_IDX]==0) {
					close(udp_sockd);
					close(tcp_sockd);
					close(listen_tcpsockd);
					// Child work (exec)
					snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/root/LaTe -s -u -p %d -t 10000 -S %s -W %s/LaTe_bidir_P_%d_%lu_perpkt "
						"-X mnrp --initial-timeout >1 /dev/null >a2 late_errors_bidir.log",
						configs.port_late_bidir,
						configs.test_interface,
						logdirnames.logs_bidir_dir_str,late_payloads[payload_lengths_idx],now.tv_sec);
					if(exect(latecmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn LaTe bidirectional (RTT) process (server). exect() error.\n");
						errors.late_bidir++;
					}
				}

				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting LaTe unidirectional (UL: master->slave) server @ %lu...\n",now.tv_sec);

				if((late_pids[UNIDIR_UL_IDX]=fork())<0) {
					fprintf(stderr,"Error: cannot spawn LaTe bidirectional process (server).\n");
					errors.late_unidir_UL++;
				} else if(late_pids[UNIDIR_UL_IDX]==0) {
					close(udp_sockd);
					close(tcp_sockd);
					close(listen_tcpsockd);

					// Child work (exec)
					snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/root/LaTe -s -u -p %d -t 10000 -S %s -W %s/LaTe_unidir_UL_P_%d_%lu_perpkt "
						"-X mnrp --initial-timeout >1 /dev/null >a2 late_errors_unidir_UL.log",
						configs.port_late_unidir_UL,
						configs.test_interface,
						logdirnames.logs_unidir_UL_dir_str,late_payloads[payload_lengths_idx],now.tv_sec);
					if(exect(latecmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn LaTe bidirectional (RTT) process (server). exect() error.\n");
						errors.late_unidir_UL++;
					}
				}

				// Send "LTNT_SLAVE_LATE_SERVER_READY" to the master
				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_slave_late_server_started,sizeof(tcp_ctrl_slave_late_server_started),NULL);

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto slave_error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto slave_error;
				}

				// Start the DL (slave->master) client
				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting LaTe unidirectional (DL: slave->master) client @ %lu...\n",now.tv_sec);

				if((late_pids[UNIDIR_DL_IDX]=fork())<0) {
					fprintf(stderr,"Error: cannot spawn LaTe unidirectional (DL) process (client).\n");
					errors.late_unidir_DL++;
				} else if(late_pids[UNIDIR_DL_IDX]==0) {
					close(udp_sockd);
					close(tcp_sockd);
					close(listen_tcpsockd);

					// Child work (exec)
					snprintf(latecmdstr,LATE_CMD_STR_MAX_SIZE,"/root/LaTe -c %s -u -U -P %d -t %d -R e%d,%d -i %d -p %d -T 10000 -S %s "
						"-f %s/LaTe_unidir_DL_%lu_final >1 /dev/null >a2 late_errors_unidir_DL.log",
						configs.my_ip_data,
						late_payloads[payload_lengths_idx],
						configs.late_min_periodicity,
						configs.late_mean_periodicity,configs.late_periodicity_batch,
						configs.test_duration_late_sec,
						configs.port_late_unidir_DL,
						configs.test_interface,
						logdirnames.logs_unidir_DL_dir_str,now_begin.tv_sec);
					if(exect(latecmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn LaTe unidirectional (DL) process (client). exect() error.\n");
						errors.late_unidir_DL++;
					}
				}

				fprintf(stdout,"LaTe process PID [bidirectional]: %d (LaMP payload size: %d)\n"
					"LaTe process PID [unidirectional UL]: %d (LaMP payload size: %d)\n"
					"LaTe process PID [unidirectional DL]: %d (LaMP payload size: %d)\n",
					late_pids[BIDIR_IDX],late_payloads[payload_lengths_idx],
					late_pids[UNIDIR_UL_IDX],late_payloads[payload_lengths_idx],
					late_pids[UNIDIR_DL_IDX],late_payloads[payload_lengths_idx]);

				pid_t wpid;
				int pstatus;

				// Wait for all the children to finish
				while((wpid=wait(&pstatus))>0) {
					fprintf(stdout,"LaTe process with PID %d terminated. Status: %d\n",wpid,WEXITSTATUS(pstatus));
					if(WEXITSTATUS(pstatus)!=0) {
						if(wpid==late_pids[BIDIR_IDX]) {
							errors.late_bidir++;
						} else if(wpid==late_pids[UNIDIR_UL_IDX]) {
							errors.late_unidir_UL++;
						} else if(wpid==late_pids[UNIDIR_DL_IDX]) {
							errors.late_unidir_DL++;
						} else {
							fprintf(stderr,"Error: LTNT slave was waiting for an unknown process with PID %d.\n",wpid);
						}
					}
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,NULL,0,"LTNT_MASTER_LATE_TERMINATED");

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto slave_error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto slave_error;
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_slave_late_terminated,sizeof(tcp_ctrl_slave_late_terminated),NULL);

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto slave_error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto slave_error;
				}


				if(late_payloads_size>1) {
					payload_lengths_idx=(payload_lengths_idx+1)%late_payloads_size;
				}

				// Start iperf tests
				int status;
				char tcp_ctrl_master_iperf_server_started[TCP_CTRL_RX_BUF_SIZE];

				// Wait for the master to start the iperf server
				rcv_bytes=recv(tcp_sockd,tcp_ctrl_master_iperf_server_started,TCP_CTRL_RX_BUF_SIZE,0);

				if(rcv_bytes>1 && strncmp(tcp_ctrl_master_iperf_server_started,"LTNT_MASTER_IPERF_SERVER_STARTED",32)==0) {
					// Master has started the server
					// Get the protocol
					sscanf(tcp_ctrl_master_iperf_server_started,"LTNT_MASTER_IPERF_SERVER_STARTED,%d",(int *)&iperf_tcp);
				} else if(rcv_bytes==-1) {
					if(errno==EAGAIN) {
						fprintf(stderr,"Error: the master seems to be unresponsive.\n");
						retval=ERR_MASTER_UNRESPONSIVE;
						goto slave_error;
					} else {
						fprintf(stderr,"Error: cannot receive slave replies on the TCP socket.\n");
						retval=ERR_TCP_SOCKET;
						goto slave_error;
					}
				}

				fprintf(stdout,"Received LTNT_MASTER_IPERF_SERVER_STARTED from master. Protocol: %s\n",
					iperf_tcp==true ? "tcp" : "udp");

				// Then, start the client
				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting iperf DL (slave->master) client @ %lu (%s)...\n",now.tv_sec,iperf_tcp==true ? "tcp" : "udp");

				if((iperf_pid=fork())<0) {
					fprintf(stderr,"Error: cannot spawn the iperf client for the DL (slave->master) test (%s).\n",iperf_tcp==true ? "tcp" : "udp");
					iperf_tcp==true ? errors.iperf_TCP_DL++ : errors.iperf_UDP_DL++;
				} else if(iperf_pid==0) {
					close(udp_sockd);
					close(tcp_sockd);
					close(listen_tcpsockd);

					// Child work (exec)
					snprintf(iperfcmdstr,IPERF_CMD_STR_MAX_SIZE,"/usr/bin/iperf -c %s %s -p %d -l %s -i 1 -t %d -b 1G >1 /dev/null >2 /dev/null",
						configs.my_ip_data,
						iperf_tcp==true ? "" : "-u -P 3",
						configs.port_iperf,
						iperf_tcp==true ? configs.TCP_iperf_buf_len : configs.UDP_iperf_packet_len,
						configs.test_duration_iperf_sec);
					if(exect(iperfcmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn the iperf client for the UL (master->slave) test (%s). exect() error.\n",iperf_tcp==true ? "tcp" : "udp");
						iperf_tcp==true ? errors.iperf_TCP_DL++ : errors.iperf_UDP_DL++;
					}
				}

				fprintf(stdout,"iperf DL test in progress (protocol: %s) (PID: %d)\n",iperf_tcp==true ? "tcp" : "udp",iperf_pid);

				// Wait for the client to terminate
				if(waitpid(iperf_pid,&status,0)<0) {
					fprintf(stderr,"iperf client process with PID %d caused an error with waitpid().\n",iperf_pid);
					iperf_tcp==true ? errors.iperf_TCP_DL++ : errors.iperf_TCP_DL++;
				} else {
					fprintf(stdout,"iperf client process with PID %d terminated. Status: %d\n",iperf_pid,WEXITSTATUS(pstatus));
					if(WEXITSTATUS(pstatus)!=0) {
						iperf_tcp==true ? errors.iperf_TCP_DL++ : errors.iperf_TCP_DL++;
					}
				}

				// Send to the master the information about the iperf client being terminated
				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_slave_iperf_client_terminated,sizeof(tcp_ctrl_slave_iperf_client_terminated),"LTNT_MASTER_IPERF_SERVER_TERMINATED");
				
				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto slave_error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto slave_error;
				}

				// // In case of an iperf TCP test, change the value of net.ipv4.tcp_fin_timeout to avoid bind errors in the following TCP tests
				// if(iperf_tcp==true) {
				// 	tcp_fin_timeout_prev_val=set_sys_tcp_fin_timeout(&configs,NULL);

				// 	if(tcp_fin_timeout_prev_val<0) {
				// 		retval=ERR_SET_TCP_FIN_TIMEOUT;
				// 		goto error;
				// 	}

				// 	tcp_fin_timeout_changed=true;
				// }

				// Start the iperf server
				gettimeofday(&now,NULL);
				fprintf(stdout,"Starting iperf UL (master->slave) server @ %lu (%s)...\n",now.tv_sec,iperf_tcp==true ? "tcp" : "udp");

				if((iperf_pid=fork())<0) {
					fprintf(stderr,"Error: cannot spawn the iperf server for the UL (master->slave) test (%s).\n",iperf_tcp==true ? "tcp" : "udp");
					iperf_tcp==true ? errors.iperf_TCP_UL++ : errors.iperf_UDP_UL++;
				} else if(iperf_pid==0) {
					close(udp_sockd);
					close(tcp_sockd);
					close(listen_tcpsockd);

					// Child work (exec)
					snprintf(iperfcmdstr,IPERF_CMD_STR_MAX_SIZE,"/usr/bin/iperf -s %s -i 1 -p %d -l %s -y C >1 %s/iperf_throughput_%s_%lu.csv",
						iperf_tcp==true ? "" : "-u",
						configs.port_iperf,
						iperf_tcp==true ? configs.TCP_iperf_buf_len : configs.UDP_iperf_packet_len,
						logdirnames.logs_iperf_UL_dir_str,iperf_tcp==true ? "tcp" : "udp",now.tv_sec);
					if(exect(iperfcmdstr)<0) {
						fprintf(stderr,"Error: cannot spawn the iperf server for the UL (master->slave) test (%s). exect() error.\n",iperf_tcp==true ? "tcp" : "udp");
						iperf_tcp==true ? errors.iperf_TCP_UL++ : errors.iperf_UDP_UL++;
					}
				}

				fprintf(stdout,"iperf UL test in progress (protocol: %s) (PID: %d)\n",iperf_tcp==true ? "tcp" : "udp",iperf_pid);

				// Set a TCP timeout equal to the duration of the whole iperf test, plus some margin equal to TCP_CTRL_GIVEUP_TIME_MS*
				rx_timeout.tv_sec=(time_t) configs.test_duration_iperf_sec;
				rx_timeout.tv_usec=1000000*configs.test_duration_iperf_sec-rx_timeout.tv_sec*1000000;

				rx_timeout.tv_sec+=(TCP_CTRL_GIVEUP_TIME_MS/1000);

				if(setsockopt(tcp_sockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
					fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket (iperf test timeout). Details: %s.\n",strerror(errno));
					fprintf(stderr,"Was attempting to set timeout to %lu seconds and %lu microseconds.\n",rx_timeout.tv_sec,rx_timeout.tv_usec);
					retval=ERR_TCP_SOCKET;
					goto slave_error;
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_slave_iperf_server_started,sizeof(tcp_ctrl_slave_iperf_server_started),"LTNT_MASTER_IPERF_CLIENT_TERMINATED");
				fprintf(stdout,"Received LTNT_MASTER_IPERF_CLIENT_TERMINATED from master (UL test).\n");

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto slave_error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto slave_error;
				}

				// When the client on the master has terminated, kill the iperf server on the slave
				killwait(iperf_pid,SIGKILL,iperf_tcp,configs.test_duration_iperf_sec,&errors);

				// // Restore net.ipv4.tcp_fin_timeout (slave)
				// if(iperf_tcp==true && tcp_fin_timeout_changed==true) {
				// 	set_sys_tcp_fin_timeout(&configs,&tcp_fin_timeout_prev_val);
				// 	if(tcp_fin_timeout_prev_val<0) {
				// 		retval=ERR_SET_TCP_FIN_TIMEOUT;
				// 		goto error;
				// 	}
				// 	tcp_fin_timeout_changed=false;
				// }

				// Restore the TCP socket timeout*
				rx_timeout.tv_sec=(time_t) ((TCP_CTRL_GIVEUP_TIME_MS)/1000);
				rx_timeout.tv_usec=((long)1000000*TCP_CTRL_GIVEUP_TIME_MS)-rx_timeout.tv_sec*1000000000;

				if(setsockopt(tcp_sockd,SOL_SOCKET,SO_RCVTIMEO,&rx_timeout,sizeof(rx_timeout))!=0) {
					fprintf(stderr,"Error: could not set RCVTIMEO for the TCP socket.\n");
					retval=ERR_TCP_SOCKET;
					goto slave_error;
				}

				sendtcpctrlpkt_rval=send_tcp_ctrl_packet(tcp_sockd,tcp_ctrl_slave_iperf_server_terminated,sizeof(tcp_ctrl_slave_iperf_server_terminated),NULL);

				if(sendtcpctrlpkt_rval==-1) {
					retval=ERR_TCP_SEND;
					goto slave_error;
				} else if(sendtcpctrlpkt_rval==-2) {
					retval=ERR_SLAVE_UNRESPONSIVE;
					goto slave_error;
				}

				// Set this, even if it will be overwritten, just in case of communication issues (which should never occur, in any case)
				if(iperf_tcp==false) {
					iperf_tcp=true;
				} else {
					iperf_tcp=false;
				}

				fprintf(stdout,"Slave iteration terminated...\n");
				print_errcount_report(stdout,&opts,errors);
			}

			slave_error:
			if(listen_tcpsockd) {
				close(listen_tcpsockd);
			}

			if(late_payloads) {
				free(late_payloads);
			}

			freelogdirs(&logdirnames);

			configuration_free(&configs);
		}

		error:
		if(udp_sockd) {
			close(udp_sockd);
		}

		if(tcp_sockd) {
			close(tcp_sockd);
		}

		if(opts.terminate_on_error==false) {
			// Kill any running iperf process
			if(iperf_pid>0) {
				kill(iperf_pid,SIGKILL);
			}

			// Wait for any "leftover" process to terminate before starting again the master/slave
			int wpid, pstatus;
			while((wpid=wait(&pstatus))>0) {
				fprintf(stderr,"Leftover process with PID %d terminated. Status: %d\n",wpid,WEXITSTATUS(pstatus));
			}
		}

		// // Restore net.ipv4.tcp_fin_timeout, if it was not restored before, due to errors
		// if(tcp_fin_timeout_changed==true) {
		// 	set_sys_tcp_fin_timeout(&configs,&tcp_fin_timeout_prev_val);
		// 	if(tcp_fin_timeout_prev_val<0) {
		// 		retval=ERR_SET_TCP_FIN_TIMEOUT;
		// 		goto error;
		// 	}

		// 	tcp_fin_timeout_changed=false;
		// }

		if(retval!=ERR_OK) {
			millisleep(1000);
		}
	} while(opts.terminate_on_error==false && opts.clear_logs==false);

	if(opts.opmode==LTNT_OPMODE_MASTER) {
		if(late_payloads) {
			free(late_payloads);
		}

		freelogdirs(&logdirnames);
	}

	main_error:
	options_free(&opts);


	return retval;
}