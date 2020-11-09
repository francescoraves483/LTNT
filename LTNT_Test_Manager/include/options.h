#ifndef LTNT_OPTIONS_H_INCLUDED
#define LTNT_OPTIONS_H_INCLUDED

#include <inttypes.h>
#include <stdbool.h>

// Valid options
// Any new option should be handled in the switch-case inside parse_options() and the corresponding char should be added to VALID_OPTS
// If an option accepts an additional argument, it is followed by ':'
#define VALID_OPTS "hvcf:smS:T"

// Slave discovery/control port and other options
#define UDP_DISC_TCP_CTRL_PORT 65500
#define UDP_DISC_RX_BUF_SIZE 32
#define TCP_CTRL_RX_BUF_SIZE 128
#define TCP_CTRL_TIMEOUT_MS 5000

// Retry time for discovery
#define UDP_DISC_RETRY_TIME_MS 1000

// Giveup time for control connection
#define TCP_CTRL_GIVEUP_TIME_MS 20000

// Default configuration values
#define DEF_PORT_LATE_BIDIR 				46001
#define DEF_PORT_LATE_UNIDIR_UL				46002
#define DEF_PORT_LATE_UNIDIR_DL				46003
#define DEF_PORT_IPERF 						9000
#define DEF_TEST_DURATION_IPERF_SEC 		900
#define DEF_TEST_DURATION_LATE_SEC			1800

// iperf server timeout margin after which a server is killed (specified in seconds)
#define IPERF_SRV_WAITPID_TIMEOUT 			10

// Default options values
#define INIT_CODE 0x7E
#define DEFAULT_CONFIG_FILE_NAME "LTNT.ini"

// Config file fields
/* CFG(section, name, type, default_value) */
// Maximum number of possible configuration fields
#define CONFIG_FIELDS_MAX_NUMBER 100

// Maximum length of dynamically allocated configuration fields when the configuration is received via TCP (slave)
#define CONFIG_STRING_MALLOC_SIZE 100

#define CONFIG_FIELDS \
	CFG("IP addresses", ip_data_remote, char *, NULL) \
	CFG("IP addresses", ip_control_remote, char *, NULL) \
	CFG("IP addresses", my_ip_data, char *, NULL) \
	CFG("IP addresses", my_ip_control, char *, NULL) \
	CFG("Ports", port_late_bidir, int, 46001) \
	CFG("Ports", port_late_unidir_UL, int, 46002) \
	CFG("Ports", port_late_unidir_DL, int, 46003) \
	CFG("Ports", port_iperf, int, 9000) \
	CFG("Interface", test_interface, char *, "eth0") \
	CFG("Interface", control_interface, char *, "eth1") \
	CFG("Duration", test_duration_iperf_sec, int, 900) \
	CFG("Duration", test_duration_late_sec, int, 1800) \
	CFG("iperf", UDP_iperf_packet_len, char *, "1470") \
	CFG("iperf", TCP_iperf_buf_len, char *, "128K") \
	CFG("Paths", scripts_dir, char *, "./scripts") \
	CFG("Paths", test_log_dir, char *, "./") \
	CFG("LaTe", late_payload_sizes, char *, "0,1448") \
	CFG("LaTe", late_min_periodicity, int, 40) \
	CFG("LaTe", late_mean_periodicity, int, 50) \
	CFG("LaTe", late_periodicity_batch, int, 10) \
	CFG("LaTe", late_sleep_between_clients_ms, int, 100) \
	CFG("Log directory names", logs_late_bidir, char *, "Logs_bidir") \
	CFG("Log directory names", logs_late_unidir_UL, char *, "Logs_unidir_UL") \
	CFG("Log directory names", logs_late_unidir_DL, char *, "Logs_unidir_DL") \
	CFG("Log directory names", logs_iperf_UL, char *, "Logs_iperf_UL") \
	CFG("Log directory names", logs_iperf_DL, char *, "Logs_iperf_DL")

typedef enum {
	LTNT_OPMODE_UNSET,
	LTNT_OPMODE_MASTER,
	LTNT_OPMODE_SLAVE
} ltnt_operating_mode_t;

struct configuration
{
	// = INIT_CODE if 'struct configuration' has been initialized via configuration_initialize()
	uint8_t init_code;

	#define CFG(section, name, type, default_value) type name;
		CONFIG_FIELDS
	#undef CFG
};

struct options {
	// = INIT_CODE if 'struct options' has been initialized via options_initialize()
	uint8_t init_code;

	// Operating mode (master/slave)
	ltnt_operating_mode_t opmode;

	// True/false options
	bool clear_logs;
	bool terminate_on_error;

	// Interface options (slave only)
	char *slave_control_interface;

	// Configuration file options
	bool config_filename_specified;
	char *config_filename; // Filled in if config_filename_specified=true, otherwise = NULL
};

void configuration_initialize(struct configuration *configdata);
int configuration_alloc(struct configuration *configdata);
void configuration_free(struct configuration *configdata);

void options_initialize(struct options *options);
unsigned int parse_options(int argc, char **argv, struct options *options);
void options_free(struct options *options);

int *config_late_payloads_to_array(const char *ini_entry,int *num_payloads_ptr);

#endif