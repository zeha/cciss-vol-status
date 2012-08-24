/*
	Copyright (C) 2006,2007 Hewlett-Packard Development Company, L.P.
	Author: Stephen M. Cameron

	This file is part of cciss_vol_status.

	cciss_vol_status is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	cciss_vol_status is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cciss_vol_status; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#if HAVE_CONFIG_H
#include <config.h>
#endif

#define VERSION_NUMBER PACKAGE_VERSION

#include <stdio.h>

#if STDC_HEADERS
#include <stdlib.h>
#include <string.h>
#elif HAVE_STRINGS_H
#include <strings.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if HAVE_ERRNO_H
#include <errno.h>
#else
extern int errno; /* some systems #define this! */
#endif /* HAVE_ERRNO_H */
#include <ctype.h>

#include <libgen.h>
#include <sys/ioctl.h>

#ifdef HAVE_SCSI_SG_H
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <netinet/in.h> /* for htons() */
#endif

#include <inttypes.h>
#include <getopt.h>
#include <dirent.h>

#ifdef HAVE_LINUX_CCISS_IOCTL_H
/* Some versions of cciss_ioctl.h contain a "__user" attribute which
   needs masking out to compile outside the kernel. */
#define __user
#ifndef USE_LOCAL_CCISS_HEADERS
#include <linux/cciss_ioctl.h>
#else
/* some distros have a broken cciss_ioctl.h, so we include a local one
 * in case that works better.
 */
#include "cciss_ioctl.h"
#endif
#elif HAVE_CISSIO_H
/* FreeBSD has cissio.h as a cciss_ioctl.h doppelganger */
#include "/usr/src/sys/dev/ciss/cissio.h"
#else
#error Neither cciss_ioctl.h (linux) nor cissio.h (FreeBSD) were found.
#endif

#define ID_CTLR 0x11 /* Identify controller */
#define ID_LSTATUS 0x12 /* identify logical drive status */

int everything_hunky_dory = 1;
int persnickety = 0;
int be_quiet = 1;
int try_unknown_devices = 0;
int exhaustive_search = 0;
int debug = 0;
int check_smart_data = 0;
int verbose = 0;

#define MSA1000_ID 0xe0100e11

#ifndef SCSI_IOCTL_SEND_COMMAND
#define SCSI_IOCTL_SEND_COMMAND 1
#endif

struct smartarray_id_t {
	uint32_t board_id;
	char *board_name;
	int can_decode_drive_map;
	int supports_sas;
} smartarray_id[] = {
	/* this table heavily Borrowed from linux kernel,
	   from drivers/block/cciss.c */
	{ 0x40700E11, "Smart Array 5300",	1, 0 },
	{ 0x40800E11, "Smart Array 5i",		1, 0},
	{ 0x40820E11, "Smart Array 532",	1, 0},
	{ 0x40830E11, "Smart Array 5312",	1, 0},
	{ 0x409A0E11, "Smart Array 641",	1, 0},
	{ 0x409B0E11, "Smart Array 642",	1, 0},
	{ 0x409C0E11, "Smart Array 6400",	1, 0},
	{ 0x409D0E11, "Smart Array 6400 EM",	1, 0},
	{ 0x40910E11, "Smart Array 6i",		1, 0},
	{ 0x409E0E11, "Smart Array 6422",	1, 0},
	{ 0x3225103C, "Smart Array P600",	0, 1},
	{ 0x3234103C, "Smart Array P400",	0, 1},
	{ 0x3235103C, "Smart Array P400i",	0, 1},
	{ 0x3211103C, "Smart Array E200i",	0, 1},
	{ 0x3212103C, "Smart Array E200",	0, 1},
	{ 0x3213103C, "Smart Array E200i",	0, 1},
	{ 0x3214103C, "Smart Array E200i",	0, 1},
	{ 0x3215103C, "Smart Array E200i",	0, 1},
	{ 0x3223103C, "Smart Array P800",	0, 1},
	{ 0x3237103c, "Smart Array E500",	0, 1},
	{ 0x323D103C, "Smart Array P700m",	0, 1},
	{ 0x3241103C, "Smart Array P212",	0, 1},
	{ 0x3243103C, "Smart Array P410",	0, 1},
	{ 0x3245103C, "Smart Array P410i",	0, 1},
	{ 0x3247103C, "Smart Array P411",	0, 1},
	{ 0x3249103C, "Smart Array P812",	0, 1},
	{ 0xe0110e11, "HP MSA500",		1, 0}, /* aka Smart Array CL */
	{ 0xe0200e11, "HP MSA500 G2",		1, 0},
	{ 0xe0300e11, "HP MSA20",		1, 0},
	{ 0x3118103c, "HP B110i",		0, 1},
	{ 0x324A103C, "Smart Array P712m", 0, 1},
	{ 0x324B103C, "Smart Array P711m", 0, 1},
	{ 0x3350103C, "Smart Array P222", 0, 1},
	{ 0x3351103C, "Smart Array P420", 0, 1},
	{ 0x3352103C, "Smart Array P421", 0, 1},
	{ 0x3353103C, "Smart Array P822", 0, 1},
	{ 0x3354103C, "Smart Array P420i", 0, 1},
	{ 0x3355103C, "Smart Array P220i", 0, 1},
	{ 0x3356103C, "Smart Array P721m", 0, 1},

#ifdef HAVE_SCSI_SG_H
	{ MSA1000_ID, "MSA1000",	1, 0},
#else
#warning Since <scsi/sg.h> is not around, MSA1000 support will not be compiled.
#endif
	{ 0xFFFFFFFF, "Unknown Smart Array",	0, 1},
};

#define ARRAYSIZE(a) (sizeof((a)) / sizeof((a)[0]))
#define UNKNOWN_CONTROLLER ARRAYSIZE(smartarray_id)

unsigned long long zero_lun = 0x00ULL;
#define ZEROLUN ((unsigned char *) &zero_lun)

/* cciss_to_bmic maps cciss logical lun to correspoding
   (controller_lun, BMIC drive number) tuple via cross
   referencing inquiry page 0x83 data with
   BMIC ID logical drive data */

struct cciss_bmic_addr_t {
	unsigned char logical_lun[8];
	unsigned char controller_lun[8];
	unsigned short bmic_drive_number;
	unsigned char bmic_id_ctlr_data[100];
	unsigned char inq_pg_0x83_data[100];
	int tolerance_type;
	int certain; /* MSA500 injects uncertainty of this mapping, unfortunately */
};

struct bmic_addr_t {
	unsigned char controller_lun[8];
	unsigned short bmic_drive_number;
	int tolerance_type;
};

struct cciss_to_bmic_t {
	int naddrs;
	struct cciss_bmic_addr_t addr[1024];  /* 1024 should do us for awhile */
} cciss_to_bmic;

/* List of controllers -- not all controllers in the system, just the
   internal one (e.g. all zero luns, plus externally attached ones --
   like MSA500. */
#define MAX_CONTROLLERS 256 /* this is a ridiculously large number */
unsigned char controller_lun_list[MAX_CONTROLLERS][8];
int busses_on_this_ctlr[MAX_CONTROLLERS];
int num_controllers = 0;

/* See the following documents for information about the hardware
 * specific structures used in this program:
 *
 *	fwspecwww.doc from http://sourceforge.net/progjects/cpqarray
 *	Open_CISS_Spec.pdf from http://sourceforge.net/progjects/cciss
 *	and see also the ciss.h header file from the freebsd ciss driver
 */

/* Structure returned by command to get logical drive status (0x12) */
#pragma pack(1)
struct identify_logical_drive_status {
	unsigned char status;
	uint32_t drive_failure_map;
	unsigned char reserved[416];
	uint32_t blocks_left_to_recover;
	unsigned char drive_rebuilding;
	uint16_t remap_count[32];
	uint32_t replacement_drive_map;
	uint32_t active_spare_map;
	unsigned char spare_status;
	unsigned char spare_to_replace_map[32];
	uint32_t replaced_marked_ok_map;
	unsigned char media_exchanged;
	unsigned char cache_failure;
	unsigned char expand_failure;
	unsigned char unit_flags;
	uint16_t big_failure_map[8];
	uint16_t big_remap_cnt[128];
	uint16_t big_replace_map[8];
	uint16_t big_spare_map[8];
	unsigned char  big_spare_replace_map[128];
	uint16_t big_replace_ok_map[8];
	unsigned char  big_drive_rebuild;
};
#pragma pack()

const char *spare_drive_status_msg[] = {
		/* Corresponds to bits in spare_status field, above */
		/* bit 0 */  "At least one spare drive designated",
		/* bit 1 */  "At least one spare drive activated and currently rebuilding",
		/* bit 2 */  "At least one activated on-line spare drive is completely rebuilt on this logical drive",
		/* bit 3 */  "At least one spare drive has failed",
		/* bit 4 */  "At least one spare drive activated",
		/* bit 5 */  "At least one spare drive remains available",
};

#define NSPARE_MSGS ARRAYSIZE(spare_drive_status_msg)

#pragma pack(1)
/* Structure returned by Identify Controller command (0x11) */
struct identify_controller {
	unsigned char num_logical_drives;
	uint32_t signature;
	unsigned char running_firm_rev[4];
	unsigned char rom_firm_rev[4];
	unsigned char hardware_rev;
	unsigned char reserved[4];
	uint32_t drive_present_bit_map;
	uint32_t external_drive_bit_map;
	uint32_t board_id;
	unsigned char reserved2;
	uint32_t non_disk_map;
	unsigned char reserved3[5];
	unsigned char marketing_revision;
	unsigned char controller_flags;
	// unsigned char reserved4[2];
	unsigned char host_flags;
	unsigned char expand_disable_code;
	unsigned char scsi_chip_count;
	uint32_t reserved5;
	uint32_t ctlr_clock;
	unsigned char drives_per_scsi_bus;
	uint16_t big_drive_present_map[8];
	uint16_t big_ext_drive_map[8];
	uint16_t big_non_disk_map[8];

/* Beyond this point is previously undisclosed stuff which
   my higher-ups at HP have nonetheless allowed me to put in here.
   Was told most of this stuff is exposed to some degree by
   the Array Diagnostics Utility already anyhow, so no big deal.
*/

	unsigned short task_flags;				/* used for FW debugging */
	unsigned char ICL_bus_map;			/* Bitmap used for ICL between controllers */
	unsigned char redund_ctlr_modes_support;	/* See REDUNDANT MODE VALUES */
#define SUPPORT_ACTIVE_STANDBY 0x01
#define SUPPORT_PRIMARY_SECONDARY 0x02
	unsigned char curr_redund_ctlr_mode;		/* See REDUNDANT MODE VALUES */
#define ACTIVE_STANDBY_MODE 0x01
#define PRIMARY_SECONDARY_MODE 0x02
	unsigned char redund_ctlr_status;		/* See REDUNDANT STATUS FLAG */
#define PREFERRED_REDUNDANT_CTLR (0x04)
	unsigned char redund_op_failure_code;		/* See REDUNDANT FAILURE VALUES */
	unsigned char unsupported_nile_bus;
	unsigned char host_i2c_autorev;
	unsigned char cpld_revision;
	unsigned char fibre_chip_count;
	unsigned char daughterboard_type;
	unsigned char reserved6[2];

	unsigned char access_module_status;
	unsigned char features_supported[12];
	unsigned char  bRecRomInactiveRev[4];   /* Recovery ROM inactive f/w revision  */
	unsigned char  bRecRomFlags;            /* Recovery ROM flags                  */
	unsigned char  bPciToPciBridgeStatus;   /* PCI to PCI bridge status            */
	unsigned int   ulReserved;              /* Reserved for future use             */
	unsigned char  bPercentWriteCache;      /* Percent of memory allocated to write cache */
	unsigned short usDaughterboardCacheSize;/* Total cache board size              */
	unsigned char  bCacheBatteryCount;      /* Number of cache batteries           */
	unsigned short usTotalMemorySize;       /* Total size (MB) of atttached memory */
	unsigned char  bMoreControllerFlags;    /* Additional controller flags byte    */
	unsigned char  bXboardHostI2cAutorev;   /* 2nd byte of 3 byte autorev field    */
	unsigned char  bBatteryPicRev;          /* BBWC PIC revision                   */
	unsigned char  bDdffVersion[4];         /* DDFF update engine version          */
	unsigned short usMaxLogicalUnits;       /* Maximum logical units supported */
	unsigned short usExtLogicalUnitCount;   /* Big num configured logical units */
	unsigned short usMaxPhysicalDevices;    /* Maximum physical devices supported */
	unsigned short usMaxPhyDrvPerLogicalUnit; /* Max physical drive per logical unit */
	unsigned char  bEnclosureCount;         /* Number of attached enclosures */
	unsigned char  bExpanderCount;          /* Number of expanders detected */
	unsigned short usOffsetToEDPbitmap;     /* Offset to extended drive present map*/
	unsigned short usOffsetToEEDPbitmap;    /* Offset to extended external drive present map */
	unsigned short usOffsetToENDbitmap;     /* Offset to extended non-disk map */
	unsigned char  bInternalPortStatus[8];   /* Internal port status bytes */
	unsigned char  bExternalPortStatus[8];   /* External port status bytes */
	unsigned int   uiYetMoreControllerFlags; /* Yet More Controller flags  */
	unsigned char  bLastLockup;              /* Last lockup code */
	unsigned char  bSlot;                    /* PCI slot according to option ROM*/
	unsigned short usBuildNum;               /* Build number */
	unsigned int   uiMaxSafeFullStripeSize;  /* Maximum safe full stripe size */
	unsigned int   uiTotalLength;            /* Total structure length */
	unsigned char  bVendorID[8];             /* Vendor ID */
	unsigned char  bProductID[16];           /* Product ID */
	unsigned char  reserved7[288];

/* End of previously unexposed stuff */

};
#pragma pack()

#pragma pack(1)
struct identify_logical_drive {
	unsigned short block_len;
	unsigned int   num_blks;
	unsigned short cylinders;
	unsigned char  heads;
	unsigned char  xsig;
	unsigned char  psectors;
	unsigned short wpre;
	unsigned char  maxecc;
	unsigned char  drive_control;
	unsigned short pcyls;
	unsigned char  pheads;
	unsigned short landz;
	unsigned char  sector_per_track;
	unsigned char  check_sum;
	unsigned char  tolerance_type;
	unsigned char  resv1;
	unsigned char  bios_disable_flg;
	unsigned char  resv2;
	unsigned int  log_drv_id;
	unsigned char  log_drive_label[64];
	unsigned int   big_blocks_available_lo;
	unsigned int   big_blocks_available_hi;
	unsigned char  unique_volume_id[16]; /* Matches inquiry page 0x83 data */
	unsigned char  reserved[394];
};
#define ID_LOGICAL_DRIVE 0x10
#pragma pack()


#pragma pack(1)
typedef struct alarm_struct_t {
	uint8_t alarm_status;
	uint8_t temp_status;
	uint8_t valid_alarm_bits;
	uint16_t alarm_count;
	uint16_t specific_alarm_counts[8];
} alarm_struct;

typedef struct inquiry_data_t {
	uint8_t peripheral_type;
	uint8_t rmb;
	uint8_t versions;
	uint8_t misc;
	uint8_t additional_length;
	uint8_t reserved[2];
	uint8_t support_bits;
	uint8_t vendor_id[8];
	uint8_t product_id[16];
	uint8_t product_revision[4];
} inquiry_data;

typedef struct sense_bus_param_t {
	inquiry_data inquiry;
	uint8_t inquiry_valid;
	uint32_t installed_drive_map;
	uint16_t hot_plug_count[32];
	uint8_t reserved1; /* physical box number? */
	uint8_t reserved2;
	alarm_struct alarm_data;
	uint16_t connection_info; /* 0x01: External Connector is Used? */
	uint8_t scsi_device_revision;
	uint8_t fan_status;
	uint8_t more_inquiry_data[64];
	uint32_t scsi_device_type;
	uint32_t bus_bit_map;
	uint8_t reserved3[8];
	uint8_t scsi_initiator_id;
	uint8_t scsi_target_id;
	uint8_t physical_port[2];
	uint16_t big_installed_drive_map[8];
	uint16_t big_bus_bit_map[8];
	uint16_t big_box_bit_map[8];
	uint8_t installed_box_map;
	uint8_t more_connection_info;
	uint8_t reserved4[2];
	char chassis_sn[40];
} sense_bus_param;
#define SENSE_BUS_PARAM 0x65

struct identify_physical_device {
	uint8_t scsi_bus;
	uint8_t scsi_id;
	uint16_t block_size_in_bytes;
	uint32_t total_blocks;
	uint32_t reserved_blocks;
	unsigned char drive_model[40];
	unsigned char drive_serial_no[40];
	unsigned char drive_fw_rev[8];
	uint8_t scsi_inquiry_byte_7; /* byte 7 from inquiry page 0 data */
	uint8_t reserved[2];
	/* physical_drive_flags:
	 * Bit 0 set if drive present and operational at this position.
	 * Bit 1 set if non-disk device detected at this position.
	 * Bit 2 set if wide SCSI transfers are currently enabled for this drive.
	 * Bit 3 set if synchronous (fast or ultra) SCSI transfers are enabled
	 *       for this drive.  If bit 6 is set, bit 3 will also be set.
	 * Bit 4 set if this drive is in a narrow drive tray.
	 * Bit 5 set if drive is wide, but firmware reverted to narrow
	 *       transfers on this drive due to wide SCSI transfer failure.
	 * Bit 6 set if Ultra-SCSI transfers are enabled for this drive.  If
	 *       bit 7 is set, bit 6 will also be set.
	 * Bit 7 set if Ultra-2 SCSI transfers are enabled for this drive.
	 *       (bit 7 defined in f/w 3.02 only)
	 */
	uint8_t physical_drive_flags;
	/* more_physical_drive_flags:
	 * Valid for f/w 1.50 only.
	 * Bit 0 set if drive supports S.M.A.R.T. predictive failure according
	 *       to the drive’s mode page 0x1C (byte 2, bit 3, is “changeable”).
	 * Bit 1 set if S.M.A.R.T. predictive failure (01/5D) errors are
	 *       recorded for this drive.  This bit should be ignored if the drive
	 *       does not support S.M.A.R.T. (according to bit 0).
	 * Bit 2 set if S.M.A.R.T. predictive failure is enabled according to
	 *       the “saved parameters” in the drive’s mode page 0x1C (byte 2, bit 3,
	 *       is set).  Also see bits 0 & 1, above.
	 * Bit 3 set if S.M.A.R.T. predictive failure (01/5D) errors are
	 *       recorded for this drive since the last controller reset.  This bit
	 *       should be ignored if the drive does not support S.M.A.R.T.
	 *       (according to bit 0).
	 * Bit 4 set if drive is attached to the external SCSI connector (f/w
	 *       2.50)
	 * Bit 5 set if drive is configured as part of a logical volume (f/w
	 *       2.50).
	 * Bit 6 set if drive is configured to be a spare (f/w 2.50).
	 * Bit 7 set if controller detected that the drive write cache was
	 *       enabled in the “saved” Caching_Parameters mode page at the time when
	 *       the drive was spun up (if a configuration exists, the write cache
	 *       will subsequently be disabled in the “saved” page by the controller
	 *       based upon the setting in Set Controller Parameters).  (f/w  2.60).
	 */
	uint8_t more_physical_drive_flags;
	uint8_t reserved2;
	/* yet_more_physical_drive_flags:
	 * Bits 0-5:  Reserved
	 * Bit 6 set if drive write cache is enabled in the “current”
	 *       Caching_Parameters mode page.
	 * Bit 7 set if drive write cache setting is changeable and drive has a
	 *       “safe” write cache according to the “WCE” bit in the default
	 *       Caching_Parameters mode page.
	 * (f/w 2.84 only)
	 */
	uint8_t yet_more_physical_drive_flags;
	uint8_t reserved3[5];
	unsigned char phys_connector[2];
	uint8_t phys_box_on_bus; /* <-- In firmware spec addendum: fwspecwww-sourceforge.addendum1.111510.doc */
				 /* see https://sourceforge.net/projects/cciss/files/Firmware%20documentation/ */
	uint8_t phys_bay_in_box;
};
#define BMIC_IDENTIFY_PHYSICAL_DEVICE 0x15
#pragma pack()

char *progname = "cciss_vol_status";

void usage()
{
	fprintf(stderr, "%s: usage: %s [-p] [-q] [-v] [-u] [-x] /dev/cciss/c*d0 /dev/sg*\n", progname, progname);
	fprintf(stderr, " -p  --persnickety          Complain about device nodes which can't be opened.\n");
	fprintf(stderr, " -u  --try-unknown-devices  Allow interrogation of even unrecognized controllers\n");
	fprintf(stderr, "                            (useful for brand new hardware.)\n");
	fprintf(stderr, " -s  --smart                Report S.M.A.R.T. predictive failures. \n");
	fprintf(stderr, " -v  --version              Print program version and exit.\n");
	fprintf(stderr, " -V  --verbose              Print more info about controller and disks.\n");
	fprintf(stderr, " -C  --copyright            Print copyright notice first.\n");
	exit(-1);
}

static void find_bus_target(struct identify_controller *id, int bmic_drive_number,
	int *bus, int *target)
{
	int big_map_support;
	int drives_per_scsi_bus;

	big_map_support = (id->controller_flags && (1<<7));
	drives_per_scsi_bus = id->drives_per_scsi_bus;;

	/* if no big drive map support, & zero devices per scsi bus */
	if (!big_map_support && (drives_per_scsi_bus == 0))
		drives_per_scsi_bus = 7;

	if (drives_per_scsi_bus == 0) {
		/* should never happen, but guard against. */
		fprintf(stderr, "Controller reports zero devices per "
			"scsi bus.  This is not reasonable. Exiting.\n");
		exit(-1);
	}
	*bus    = bmic_drive_number / drives_per_scsi_bus;
	*target = bmic_drive_number % drives_per_scsi_bus;
}

static void copy_drive_field(unsigned char *to, unsigned char *from, int limit)
{
	int i;

	for (i = 0; i < limit; i++) {
		if (isprint(from[i]) || from[i] == '\0')
			to[i] = from[i];
		else
			to[i] = '?';
	}
	to[limit] = '\0';
}

static void format_phys_drive_location(char *location, int bus, int target,
	int ctlrtype, unsigned char *controller_lun, struct identify_physical_device *device_data)
{
	char tail[300];
	unsigned char model[sizeof(device_data->drive_model) + 1];
	unsigned char serial_no[sizeof(device_data->drive_serial_no) + 1];
	unsigned char fw_rev[sizeof(device_data->drive_fw_rev) + 1];

	if (smartarray_id[ctlrtype].can_decode_drive_map && (device_data || !controller_lun))
		sprintf(location, "    b%dt%d", bus, target);
	else
		sprintf(location, "        ");
	if (device_data && controller_lun) {
		copy_drive_field(model, device_data->drive_model, sizeof(device_data->drive_model));
		copy_drive_field(serial_no, device_data->drive_serial_no, sizeof(device_data->drive_serial_no));
		copy_drive_field(fw_rev, device_data->drive_fw_rev, sizeof(device_data->drive_fw_rev));
		sprintf(tail, " connector %c%c box %d bay %d %40s %40s %8s",
			device_data->phys_connector[0],
			device_data->phys_connector[1],
			device_data->phys_box_on_bus,
			device_data->phys_bay_in_box,
			model, serial_no, fw_rev);
	} else
		sprintf(tail, " connector ?? box ?? bay ?? %40s %40s %8s",
			"unknown-model",
			"unknown-serial-no",
			"unknown firmware rev");
	strcat(location, tail);
}

static int bitisset(unsigned char bitstring[], int bit, int bitstringlength)
{
	int element;
	int offset;
	unsigned char or_val;

	element = (bit / 8);

	if (element >= bitstringlength) {
		fprintf(stderr, "Bug detected at %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	offset  = bit % 8;
	or_val = (unsigned char) (1 << offset);

	return ((bitstring[element] & or_val) != 0);
}

static int check_ioctl_results(char *filename, char *command,
		int rc, IOCTL_Command_struct *cmd, int ldrive_num)
{
	char numstring[120];

	if (debug) {
		fprintf(stderr, "CHK: %s: CommandStatus=%d\n", command,
			cmd->error_info.CommandStatus);
		if (cmd->error_info.CommandStatus == 1) {
			fprintf(stderr, "ScsiStatus = %d\n", cmd->error_info.ScsiStatus);
			if (cmd->error_info.ScsiStatus == 2)
				fprintf(stderr, "Check condition, sense key = %d\n", cmd->error_info.SenseInfo[2]);
		}
	}

	sprintf(numstring, " (logical drive number = %d)", ldrive_num);

	if (rc != 0) {
		fprintf(stderr, "%s: %s, %s ioctl failed, %s, returning -1\n", progname,
			filename, command, strerror(errno));
		return -1;
	}
	if (cmd->error_info.CommandStatus != 0 /* everything cool */ &&
		cmd->error_info.CommandStatus != 2 /* underrun */ ) {
		if (debug) {
			fprintf(stderr, "%s: %s%s, %s ioctl has Command Status=%d, returning -1\n",
				progname, filename, command,
				(ldrive_num == -1) ? "" : numstring,
				cmd->error_info.CommandStatus);
		}
		return -1;
	}
	return 0;
}

/* macro to check results of ioctl, and command status,
	and return -1 if something bad happens */
#define CHECK_IOCTL(filename, command, rc, cmd, ldrive_num) \
	if (check_ioctl_results(filename, command, rc, cmd, ldrive_num) !=0) \
		return -1;

static void setup_for_ioctl(IOCTL_Command_struct *cmd,
		unsigned char *lun,
		unsigned char *cdb, int cdblen,
		int write_direction,
		unsigned char *buf,
		int bufsize)
{
	memset(cmd, 0, sizeof(*cmd));
	if (lun != NULL)
		memcpy(&cmd->LUN_info, lun, 8);
	cmd->Request.CDBLen = cdblen;
	cmd->Request.Type.Type = TYPE_CMD;
	cmd->Request.Type.Attribute = ATTR_SIMPLE;
	cmd->Request.Type.Direction = (write_direction) ? XFER_WRITE : XFER_READ;
	cmd->Request.Timeout = 0;
	memcpy(cmd->Request.CDB, cdb, cdblen);
	cmd->buf_size = bufsize;
	cmd->buf = buf;
}

static int do_report_luns(char *filename, int fd, int *count, unsigned char *lun, int physical)
{
	IOCTL_Command_struct cmd;
	int rc;
	unsigned char cdb[16];
	unsigned int bufsize;
	char *cmdname;
	int becount;

	memset(lun, 0, *count * 8);
	bufsize = htonl(*count *8);
	memset(cdb, 0, 16);
	cdb[0] = physical ? 0xc3 : 0xc2; /* report physical/logical LUNs */
	memcpy(&cdb[6], &bufsize, 4);

	setup_for_ioctl(&cmd, ZEROLUN, cdb, 12, 0, lun, *count * 8);
	rc = ioctl(fd, CCISS_PASSTHRU, &cmd);
	cmdname = physical ? "REPORT_PHYSICAL" : "REPORT_LOGICAL";
	CHECK_IOCTL(filename, cmdname, rc, &cmd, -1); /* macro which may return... */

	/* memcpy to avoid possible unaligned access on ia64, */
	/* in case lun isn't aligned on a 4 byte boundary */
	/* (it is, but, why assume it?). */
	memcpy(&becount, lun, sizeof(becount));
	*count = ntohl(becount) / 8;

	if (debug) {
		int i;
		fprintf(stderr, "%s: %d %s luns:\n\n", filename, *count, physical ? "physical" : "logical");
		for (i = 8; i < (*count+1) * 8; i++) {
			if ((i % 8) == 0)
				fprintf(stderr, "%d:  ", (i-1) / 8);
			fprintf(stderr, "%02x", lun[i]);
			if (((i+1) % 8) == 0)
				fprintf(stderr, "\n");
		}
		fprintf(stderr, "\n");
	}
	return 0;
}

static int do_bmic_id_logical_drive(char *filename, int fd,
	unsigned char *controller_lun, int bmic_drive_number,
	unsigned char *buffer)
{
	IOCTL_Command_struct cmd;
	unsigned char cdb[16];
	unsigned short buffer_size = htons(512); /* Always 512 bytes for this cmd */
	int rc;

	memset(cdb, 0, 16);
	cdb[0] = 0x26; /* cciss read */
	cdb[1] = 0xff & bmic_drive_number;
	cdb[6] = ID_LOGICAL_DRIVE;
	memcpy(&cdb[7], &buffer_size, 2);
	cdb[9] = 0xff && (bmic_drive_number >> 8);

	setup_for_ioctl(&cmd, controller_lun, cdb, 16, 0, buffer, 512);
	rc = ioctl(fd, CCISS_PASSTHRU, &cmd);
	CHECK_IOCTL(filename, "IDENTIFY_LOGICAL_DRIVE", rc, &cmd, -1); /* macro which may return... */
	return 0;
}

static int do_bmic_identify_physical_device(char *filename, int fd,
	unsigned char *controller_lun, int bmic_drive_number,
	struct identify_physical_device *id_phys_device)
{
	IOCTL_Command_struct cmd;
	int is_internal_controller;
	uint8_t lunzero[8];
	uint16_t buflen_be;
	int rc;

	memset(&cmd, 0, sizeof(cmd));
	memset(lunzero, 0, sizeof(lunzero));

	is_internal_controller = (memcmp(lunzero, controller_lun, 8) == 0);
	buflen_be = htons(sizeof(*id_phys_device));
	memcpy(&cmd.LUN_info, controller_lun, sizeof(cmd.LUN_info));
        cmd.Request.CDBLen = 10;
        cmd.Request.CDB[0] = 0x26; /* CISS READ */
        if (is_internal_controller)
                cmd.Request.CDB[2] = bmic_drive_number & 0xff;
        else
                cmd.Request.CDB[5] = bmic_drive_number & 0xff;
        cmd.Request.CDB[6] = BMIC_IDENTIFY_PHYSICAL_DEVICE;
        cmd.Request.CDB[9] = (bmic_drive_number >> 8) & 0xff;
        memcpy(&cmd.Request.CDB[7], &buflen_be, sizeof(buflen_be));
        cmd.Request.Type.Type = TYPE_CMD;
        cmd.Request.Type.Attribute = ATTR_SIMPLE;
        cmd.Request.Type.Direction = XFER_READ;
        cmd.Request.Timeout = 0;
        cmd.buf = (void *) id_phys_device;
        cmd.buf_size = sizeof(*id_phys_device);

	rc = ioctl(fd, CCISS_PASSTHRU, &cmd);
	CHECK_IOCTL(filename, "IDENTIFY_PHYSICAL_DEVICE", rc, &cmd, -1); /* macro which may return... */
	return rc;
}

#ifdef HAVE_SCSI_SG_H
static int do_sg_io(int fd, unsigned char *cdb, unsigned char cdblen, unsigned char *buffer,
	unsigned int buf_size, int direction);
static int do_sgio_bmic_identify_physical_device(int fd, int bmic_drive_number,
	struct identify_physical_device *id_phys_device)
{
	unsigned char cdb[16];
	uint8_t lunzero[8];
	uint16_t buflen_be;

	memset(lunzero, 0, sizeof(lunzero));

        cdb[0] = 0x26; /* CISS READ */

	/* This is tricky.  We're supposed to use either byte 2, or byte 5
	 * for the bmic drive number depending on if the controller is internal
	 * or external.  (This is an accidental difference in the firmwares that
	 * cropped up in the '90s.)  I've found that you can just set them *both*
	 * and it seems to work.  Which is a good thing, because there's not a 
	 * good way to tell if it's internal or external with SG_IO.
	 */
	cdb[2] = bmic_drive_number & 0xff;
	cdb[5] = bmic_drive_number & 0xff;
        cdb[6] = BMIC_IDENTIFY_PHYSICAL_DEVICE;
        cdb[9] = (bmic_drive_number >> 8) & 0xff;
	buflen_be = htons(sizeof(id_phys_device));
        memcpy(&cdb[7], &buflen_be, sizeof(buflen_be));

	return do_sg_io(fd, cdb, 10, (unsigned char *) &id_phys_device,
		sizeof(id_phys_device), SG_DXFER_FROM_DEV);
}
#endif

static int lookup_controller(uint32_t board_id);
static int id_ctlr_fd(char * filename, int fd, unsigned char *lun, struct identify_controller *id)
{
	int rc;
	unsigned char cdb[16];
	IOCTL_Command_struct cmd;
	unsigned short bebufsize;
	int ctlrtype;

	memset(id, 0, sizeof(*id));
	memset(&cmd, 0, sizeof(cmd));
	memset(cdb, 0, 16);
	bebufsize = htons(sizeof(*id));
	cdb[0] = 0x26;
	cdb[6] = ID_CTLR;
	memcpy(&cdb[7], &bebufsize, 2);

	setup_for_ioctl(&cmd, lun, cdb, 10, 0, (unsigned char *) id, sizeof(*id));
	rc = ioctl(fd, CCISS_PASSTHRU, &cmd);
	CHECK_IOCTL(filename, "ID_CONTROLLER", rc, &cmd, -1); /* macro which may return... */

	ctlrtype = lookup_controller(id->board_id);
	if (ctlrtype == -1) {
		fprintf(stderr, "%s: Warning: unknown controller type 0x%08x\n",
			progname, id->board_id);
		everything_hunky_dory = 0;
	} else if (ctlrtype == -1 || smartarray_id[ctlrtype].supports_sas) {
		id->drives_per_scsi_bus = 16; /* bit of a kludge here */
		id->scsi_chip_count = 8;
	}

	/* printf("board_id=0x%08x id->drives_per_scsi_bus = %d\n",
		id->board_id, id->drives_per_scsi_bus); */

	if (id->drives_per_scsi_bus == 0)  /* not just CASA apparently. */
		id->drives_per_scsi_bus = 7;
	return(0);
}

static void show_disk_map(char *title, char *filename, int fd,
		struct identify_controller *id, unsigned char *controller_lun, int ctlrtype,
		unsigned char bigmap[],
		uint32_t smallmap
		)
{
	int bus, target, first_time;
	int i, rc;
	int big_map_support;
	int ndisks=0;
	int drives_per_scsi_bus;
	struct identify_physical_device device_data;
	char location[300];

	big_map_support = (id->controller_flags && (1<<7));
	drives_per_scsi_bus = id->drives_per_scsi_bus;;

	/* if no big drive map support, & zero devices per scsi bus */
	if (!big_map_support && (drives_per_scsi_bus == 0))
		drives_per_scsi_bus = 7;

	if (drives_per_scsi_bus == 0) {
		/* should never happen, but guard against. */
		fprintf(stderr, "Controller reports zero devices per "
			"scsi bus.  This is not reasonable. Exiting.\n");
		exit(-1);
	}

	first_time = 1;
	for (i = 0; i < (big_map_support ? 128 : 16); i++)  { /* FIXME magic 128,16 */
		find_bus_target(id, i, &bus, &target);

	  	if (big_map_support && !bitisset((unsigned char *) bigmap, i, 128 / 8))
			continue;
		else
			if (!big_map_support && !bitisset((unsigned char *) &smallmap, i, sizeof(smallmap)))
				continue;

		rc = 0;
		memset(&device_data, 0, sizeof(device_data));
#ifdef HAVE_SCSI_SG_H
		if (controller_lun)
#endif
			rc = do_bmic_identify_physical_device(filename, fd,
				controller_lun, i, &device_data);
#ifdef HAVE_SCSI_SG_H
		else
			rc = do_sgio_bmic_identify_physical_device(fd, i, &device_data);
#endif
		if (first_time) {
			printf("\n%s\n", title);
			first_time = 0;
		}

		format_phys_drive_location(location, bus, target, ctlrtype,
			controller_lun, rc ? NULL : &device_data);
		printf("%s\n", location);
		ndisks++;
	}
}

static char *decode_status[] = {
	/*  0 */ "OK",
	/*  1 */ "FAILED",
	/*  2 */ "Not configured",
	/*  3 */ "Using interim recovery mode",
	/*  4 */ "Ready for recovery operation",
	/*  5 */ "Currently recovering",

	/* Note: If the unit_status value is 6 or 7, the unit_status
		of all other configured logical drives will be marked as
		1 (Logical drive failed). This is to force the user to
		correct the problem and to insure that once the problem
		is corrected, the data will not have been corrupted by
		any user action. */

	/*  6 */ "Wrong physical drive was replaced",
	/*  7 */ "A physical drive is not properly connected",

	/*  8 */ "Hardware is overheating",
	/*  9 */ "Hardware was overheated",
	/* 10 */ "Currently expannding",
	/* 11 */ "Not yet available",
	/* 12 */ "Queued for expansion",
};

#define MAX_DECODE_STATUS ARRAYSIZE(decode_status)

static void print_volume_status(char *file, int fd, int ctlrtype,
	unsigned char *controller_lun, int volume_number,
	struct identify_logical_drive_status *vs, struct identify_controller *id,
	int tolerance_type, int certain)
{
	unsigned int spare_bit;
	unsigned int i, j, failed_drive_count;
	char raid_level[100];

	if (vs->status == 2) {/* This logical drive is not configured, so no status. */
		if (debug)
			fprintf(stderr, "volume %d not configured.\n", volume_number);
		return;
	}

	if (vs->status != 0)
		everything_hunky_dory = 0;

	switch (tolerance_type) {
		case 0: sprintf(raid_level, "RAID 0");
			break;
		case 1: sprintf(raid_level, "RAID 4");
			break;
		case 2: sprintf(raid_level, "RAID 1");
			break;
		case 3: sprintf(raid_level, "RAID 5");
			break;
		case 5: sprintf(raid_level, "RAID 6");
			break;
		default: /* mysteriously we don't know (shouldn't get here.) */
			sprintf(raid_level, "(Unknown RAID level (tolerance_type = %d)",
				tolerance_type);
			break;
	}

	printf("%s: (%s) %s Volume %d%s status: ", file,
		smartarray_id[ctlrtype].board_name, raid_level, volume_number,
		certain ? "" : "(?)");
	if (vs->status < MAX_DECODE_STATUS)
		printf("%s. ", decode_status[vs->status]);
	else
		printf("Unknown status %d.", vs->status);

	for (spare_bit = 0; spare_bit < NSPARE_MSGS; spare_bit++)
		if ((vs->spare_status >> spare_bit) & 0x01)
			printf("  %s.", spare_drive_status_msg[spare_bit]);
#if 0
	This message just makes people ask questions, without actually helping them.
	They can just go look at the LEDs to see which disks are failed.

	if (vs->status != 0 && !smartarray_id[ctlrtype].can_decode_drive_map)
		printf("%s: Don't know how to decode drive map data for %s controllers.\n",
			progname, smartarray_id[ctlrtype].board_name);
#endif
	show_disk_map("  Failed drives:", file, fd, id, controller_lun, ctlrtype,
			(unsigned char *) vs->big_failure_map, vs->drive_failure_map);
	show_disk_map("  'Replacement' drives:", file, fd, id, controller_lun, ctlrtype,
			(unsigned char *) vs->big_replace_map, vs->replacement_drive_map);
	show_disk_map("  Drives currently substituted for by spares:", file, fd, id, controller_lun, ctlrtype,
			(unsigned char *) vs->big_spare_map, vs->active_spare_map);

	printf("\n");

	/* Scan the failed drives map.  If any bits set, we're not happy.  Note, the
	 * logical drive status may still be zero in this instance, as a spare drive
	 * may have taken over but we don't want to exit with status of zero in such
	 * cases.
	 */

	failed_drive_count = 0;
	for (i = 0; i < ARRAYSIZE(vs->big_failure_map); i++) {
		if (vs->big_failure_map[i]) {
			for (j = 0; j < sizeof(vs->big_failure_map[i]) * 8; j++)
				if ((1 << j) & vs->big_failure_map[i])
					failed_drive_count++;
		}
	}
	if (failed_drive_count != 0) {
		everything_hunky_dory = 0;
		printf("    Total of %d failed physical drives "
			"detected on this logical drive.\n",
			failed_drive_count);
	}
}

static int lookup_controller(uint32_t board_id)
{
	int i;

	for (i = 0; smartarray_id[i].board_id != 0xFFFFFFFF; i++)
		if (smartarray_id[i].board_id == board_id)
			return i;
	if (try_unknown_devices)
		return i; /* Must be some fancy new controller we don't know yet. */
	return -1;
}

static void print_bus_status(char *file, int ctlrtype,
	int bus_number, sense_bus_param *bus_param)
{
	int alarms;
	char status[4*60];
	char enclosure_name[17];
	char enclosure_sn[41];
	int i;

	/* check if inquiry was valid (if not, bus does not exist) */
	if (bus_param->inquiry_valid == 0) return;

	/* prepare enclosure name */
	strncpy(enclosure_name, (char *) bus_param->inquiry.product_id, 16);
	enclosure_name[16] = '\0';
	for (i = 16 - 1; i > 0; i--)
		if (enclosure_name[i] == ' ') {
			enclosure_name[i] = '\0';
		} else {
			break;
		}
	for (i = 0; i < 16; i++)
		if (enclosure_name[i] != ' ')
			break;
	strncpy(enclosure_name, enclosure_name+i, 16-i);

	/* prepare enclosure S/N -- sometimes this is screwed up. */
	/* Eg. 6400 with internal card cage gives back bogus info */
	/* for me for the serial number. */
	strncpy(enclosure_sn, bus_param->chassis_sn, 40);
	enclosure_sn[41] = '\0';
	for (i = 40 - 1; i > 0; i--)
		if (enclosure_sn[i] == ' ') {
			enclosure_sn[i] = '\0';
		} else {
			break;
		}
	for (i = 0; i < 40; i++)
		if (enclosure_sn[i] != ' ')
			break;
	strncpy(enclosure_sn, enclosure_sn+i, 40-i);

	/* check only for valid alarm bits */
	alarms = bus_param->alarm_data.alarm_status & bus_param->alarm_data.valid_alarm_bits;

	if (alarms) {
		everything_hunky_dory = 0;

		status[0] = '\0';
		if (alarms & 0x1) {
			/* fan alert */
			if (strlen(status) > 0) strcat(status, ", ");
			strcat(status, "Fan failed");
		}
		if (alarms & 0x2) {
			/* temperature alert */
			if (strlen(status) > 0) strcat(status, ", ");
			strcat(status, "Temperature problem");
		}
		if (alarms & 0x4) {
			/* door alert */
			if (strlen(status) > 0) strcat(status, ", ");
			strcat(status, "Door alert");
		}
		if (alarms & 0x8) {
			/* power supply alert */
			if (strlen(status) > 0) strcat(status, ", ");
			strcat(status, "Power Supply Unit failed");
		}
		if (strlen(status) == 0) {
			sprintf(status, "Unknown problem (alarm value: 0x%X, allowed: 0x%X)", bus_param->alarm_data.alarm_status, bus_param->alarm_data.valid_alarm_bits);
		}
	} else {
		strcpy(status, "OK");
	}

	printf("%s: (%s) Enclosure %s (S/N: %s) on Bus %d, Physical Port %c%c status: %s.\n", file,
		smartarray_id[ctlrtype].board_name, enclosure_name, enclosure_sn,
		bus_number, bus_param->physical_port[0], bus_param->physical_port[1],
		status);
}

#ifdef HAVE_SCSI_SG_H

static void setup_sgio(sg_io_hdr_t *sgio,
		unsigned char *cdb, unsigned char cdblen,
		unsigned char *databuffer, unsigned int databufferlen,
		unsigned char *sensebuffer, unsigned char sensebufferlen,
		int direction
	       	)
{
	sgio->interface_id = 'S';
	sgio->dxfer_direction = direction;
	sgio->cmd_len = cdblen;
	sgio->mx_sb_len = sensebufferlen;
	sgio->iovec_count = 0;
	sgio->dxfer_len = databufferlen;
	sgio->dxferp = databuffer;
	sgio->cmdp = cdb;
	sgio->sbp = sensebuffer;
	sgio->timeout = 0x0fffffff; /* long timeout */
	/* I used a long timeout here instead of no timeout (0xffffffff) because */
	/* some drivers (mptlinux) appear not to like a timeout value of */
	/* 0xffffffff at all.  This way, using cciss_vol_status indiscriminately */
	/* (e.g. cciss_vol_status /dev/sg* ) doesn't make mptlinux driver mad. */

	sgio->flags = 0;
	sgio->pack_id = 0;
	sgio->usr_ptr = NULL;
	sgio->status = 0;
	sgio->masked_status = 0;
	sgio->msg_status = 0;
	sgio->sb_len_wr = 0;
	sgio->host_status = 0;
	sgio->driver_status = 0;
	sgio->resid = 0;
	sgio->duration = 0;
	sgio->info = 0;
}

#define debug_sgio 0

static inline int min(int x, int y)
{
	return x < y ? x : y;
}

static int do_sg_io(int fd, unsigned char *cdb, unsigned char cdblen, unsigned char *buffer,
	unsigned int buf_size, int direction)
{
	int status;
	sg_io_hdr_t sgio;
	unsigned char sensebuffer[64];

	memset(buffer, 0, buf_size);
	memset(&sgio, 0, sizeof(sgio));

	setup_sgio(&sgio, (unsigned char *) cdb, cdblen, buffer,
		buf_size, sensebuffer, sizeof(sensebuffer), direction);

	status = ioctl(fd, SG_IO, &sgio);

	if (status == 0 && sgio.host_status == 0 && sgio.driver_status == 0) {   /* cmd succeeded */
		if (sgio.status == 0 || (sgio.status == 2 &&
			(((sensebuffer[2] & 0x0f) == 0x00) || /* no error */
			((sensebuffer[2] & 0x0f) == 0x01)))) {
			return 0;
		}
		if (debug_sgio)
			fprintf(stderr, "sgio cmd 0x%02x check condition, sense key = %d\n",
				cdb[0], sensebuffer[2] & 0x0f);
		return -1;
	}
	if (debug_sgio)
		fprintf(stderr, "sgio ioctl: %d, cdb[0]=0x%02x, "
			"status host/driver/scsi/sensekey = %d/%d/%d/0x%02x\n",
			status, cdb[0], sgio.host_status, sgio.driver_status,
			sgio.status, sensebuffer[2]);
	return -1;
}

static int do_inquiry(int fd, unsigned char inquiry_page,
	unsigned char* inquiry_buf, unsigned char buf_size)
{
	unsigned char cdb[6];

	memset(inquiry_buf, 0, buf_size);
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x12;
	if (inquiry_page != 0) /* EVPD? */
		cdb[1] = 1;
	else
		cdb[1] = 0;
	cdb[2] = inquiry_page;
	cdb[4] = buf_size;

	return do_sg_io(fd, cdb, 6, inquiry_buf, (unsigned int) buf_size, SG_DXFER_FROM_DEV);
}

static int msa1000_passthru_ioctl (int fd, int cmd, void *buffer, int size, unsigned int log_unit)
{
	unsigned char sensebuffer[64];
	int direction = SG_DXFER_FROM_DEV;
	unsigned char cdb[16];
	int cdblen;
	unsigned short bufsize;

	memset(cdb, 0, sizeof(cdb));
	memset(sensebuffer, 0, 64);
	switch (cmd) {
	case ID_LOGICAL_DRIVE:
	case ID_LSTATUS: {
	        bufsize = htons((unsigned short) size);
	        cdb[0] = 0x26;
		cdb[1] = log_unit;
		cdb[6] = cmd;
		memcpy(&cdb[7], &bufsize, sizeof(bufsize));
		direction = SG_DXFER_FROM_DEV;
		cdblen = 10;
		break;
		}
	case ID_CTLR: {	/* 0x11 */
	        bufsize = htons((unsigned short) size);
	        cdb[0] = 0x26;
		cdb[6] = cmd;
		memcpy(&cdb[7], &bufsize, sizeof(bufsize));
		direction = SG_DXFER_FROM_DEV;
		cdblen = 10;
		break;
		}
	default:
		fprintf(stderr, "msa1000_passthru_ioctl: unknown command %d\n", cmd);
		return -1;
	}
	return do_sg_io(fd, cdb, cdblen, buffer, size, direction);
}

static int is_msa1000(int fd)
{
	int status;
	char std_inq[256];
	char *prod =  "COMPAQ  MSA1000";
	char *prod2 =  "MSA CONTROLLER";

	status = do_inquiry(fd, 0, (unsigned char *) std_inq, 255);
	if (status < 0)
		return 0;
	if (strncmp(std_inq+8, prod, strlen(prod)) == 0)
		return 1;
	if (strstr(std_inq+8, prod2) != 0)
		return 1;
	return 0;
}

static int is_smartarray_driver(int fd)
{
	struct _cciss_pci_info_struct pciinfo;

	/* If the CCISS_GETPCIINFO ioctl is known, we can presume
	 * it's a Smart Array.
	 *
	 * This does rely on linux's ioctl numbers being unique
	 * system wide.  Should be the case on vmware and freebsd too.
	 *
	 * I memset the buffer to zero anyway just on the off chance that
	 * this assumption turns out to be false in order to make the
	 * program at least behave consistently by not passing
	 * uninitialized stack data to an unknown ioctl. 
	 */
	memset(&pciinfo, 0, sizeof(pciinfo));
	return (ioctl(fd, CCISS_GETPCIINFO, &pciinfo) == 0);
}

#define MAX_CACHED_DEVICE_NODES 2048
static struct serial_number_map {
	char *device_node;
	char serial_no[16];
} serial_no_map[MAX_CACHED_DEVICE_NODES];
static int ncached_device_nodes = 0;

static char *lookup_cached_device_node(unsigned char *serial_no)
{
	int i;
	for (i = 0; i < ncached_device_nodes; i++) {
		if (memcmp(serial_no, serial_no_map[i].serial_no, 16) == 0)
			return serial_no_map[i].device_node;
	}
	return NULL;
}

static char *lookup_cached_serialno(char *filename)
{
	int i;
	for (i = 0; i < ncached_device_nodes; i++) {
		if (strcmp(filename, serial_no_map[i].device_node) == 0)
			return serial_no_map[i].serial_no;
	}
	return NULL;
}

static void cache_device_node(char *device_node, unsigned char *serial_no)
{
	int i;
	for (i = 0; i < ncached_device_nodes; i++) {
		if (memcmp(serial_no, serial_no_map[i].serial_no, 16) == 0 &&
			strcmp(device_node, serial_no_map[i].device_node) == 0)
			/* already cached */
			return;
	}
	if (i >= MAX_CACHED_DEVICE_NODES)
		return; /* cache is full. */
	ncached_device_nodes = i;
	memcpy(serial_no_map[i].serial_no, serial_no, 16);
	serial_no_map[i].device_node = malloc(strlen(device_node)+1);
	strcpy(serial_no_map[i].device_node, device_node);
}

static void free_device_node_cache()
{
	int i;
	for (i = 0; i < ncached_device_nodes; i++)
		if (serial_no_map[i].device_node)
			free(serial_no_map[i].device_node);
}

static int scsi_device_scandir_filter(const struct dirent *d)
{
	int len;

	len = strlen(d->d_name);



	/* Skip non /dev/sd* devices */
	if (strncmp(d->d_name, "sd", 2) != 0)
		return 0;

	/* Skip partitions.  Only hit the whole disk device. */
	if (d->d_name[len-1] <= '9' && d->d_name[len-1] >= '0')
		return 0;

	return 1;
}

/* Given a IDENTIFY LOGICAL DRIVE data, find the matching /dev/sd* */
/* note.  "Matching" means, the one with the same serial number. */
static char *unknown_scsi_device = "/dev/???";
static void find_scsi_device_node(unsigned char *unique_volume_id, char **scsi_device_node)
{
	struct dirent **namelist = NULL;
	int nents, rc, i, fd;
	unsigned char buffer[64];
	char filename[1024];
	char *device_node;


	/* see if we already know the device node for this serialno.  Unlikely */
	device_node = lookup_cached_device_node(unique_volume_id);
	if (device_node != NULL) {
		*scsi_device_node = malloc(strlen(device_node) + 1);
		strcpy(*scsi_device_node, device_node);
		return;
	}

	/* Scan for /dev/sd[a-z]+ device nodes */
	*scsi_device_node = unknown_scsi_device;
	nents = scandir("/dev", &namelist, scsi_device_scandir_filter, alphasort);
	if (nents < 0)
		return;

	for (i = 0; i < nents; i++) { /* for each device node /dev/sd[a-z]+ */
		char *sn;
		snprintf(filename, 1023, "/dev/%s", namelist[i]->d_name);

		/* see if we already know the serial no. */
		sn = lookup_cached_serialno(filename);
		if (sn != NULL) {
			/* see if the serial number matches. */
			if (memcmp(sn, unique_volume_id, 16) == 0) {
				*scsi_device_node = malloc(strlen(filename)+1);
				strcpy(*scsi_device_node, filename);
				break; /* serial number matches, we're done. */
			} else
				continue; /* serial number doesn't match. */
		}

		/* we don't know the serial number for this dev node, find it. */
		fd = open(filename, O_RDWR);
		if (fd < 0)
			continue;

		rc = do_inquiry(fd, 0x83, buffer, sizeof(buffer));
		if (rc < 0)
			goto next;

		/* remember the serial number for this device node for next time. */
		cache_device_node(filename, &buffer[8]);

		/* Does it match? */
		if (memcmp(&buffer[8], unique_volume_id, 16) == 0) {
			*scsi_device_node = malloc(strlen(filename)+1);
			strcpy(*scsi_device_node, filename);
			close(fd);
			break;	/* matching serial number, we're done. */
		}
next:
		close(fd);
	}

	/* free what scandir() allocated. */
	for (i = 0; i < nents; i++)
		free(namelist[i]);
	free(namelist);

	return;
}

static void msa1000_logical_drive_status(char *file, int fd,
	unsigned int logical_drive, struct identify_controller *id)
{
	int rc, ctlrtype;
	struct identify_logical_drive_status ldstatus;
	struct identify_logical_drive drive_id;
	int tolerance_type = -1;
	char *scsi_device_node;

	memset(&drive_id, 0, sizeof(drive_id));
	rc = msa1000_passthru_ioctl(fd, ID_LOGICAL_DRIVE, &drive_id,
		sizeof(drive_id), logical_drive);
	if (rc == 0)
		tolerance_type = drive_id.tolerance_type;

	memset(&ldstatus, 0, sizeof(ldstatus));
	rc = msa1000_passthru_ioctl(fd, ID_LSTATUS, &ldstatus,
		sizeof(ldstatus), logical_drive);
	if (rc < 0) {
		fprintf(stderr, "%s: %s: ioctl: logical drive: %d %s\n",
				progname, file, logical_drive, strerror(errno));
		return;
	}
	ctlrtype = lookup_controller(id->board_id);
	if (ctlrtype == -1) {
		fprintf(stderr, "%s: %s: Unknown controller, board_id = 0x%08x\n",
			progname, file, id->board_id);
		return;
	}
	find_scsi_device_node(drive_id.unique_volume_id, &scsi_device_node);
	print_volume_status(scsi_device_node, fd, ctlrtype, NULL,
		logical_drive, &ldstatus, id, tolerance_type, 1);
	if (scsi_device_node != unknown_scsi_device)
		free(scsi_device_node);
}
#else
static void free_device_node_cache()
{
}
#endif /* ifdef HAVE_SCSI_SG_H */

static int all_same(unsigned char *buf, unsigned int bufsize, unsigned char value)
{
	unsigned int i;
	int rc;

	rc = 0;
	for (i = 0; i < bufsize; i++) {
		if (buf[i] != value)
			return 0;
	}
	return 1;
}

static int do_cciss_inquiry(char *file, int fd,
		unsigned char *lun,
		unsigned char inquiry_page,
		unsigned char *inquiry_buf,
		unsigned char buf_size)
{
	IOCTL_Command_struct cmd;
	unsigned char cdb[16];
	int status;

	memset(&cmd, 0, sizeof(cmd));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x12; /* inquiry */
	cdb[1] = inquiry_page ? 1 : 0;
	cdb[2] = inquiry_page;
	cdb[4] = buf_size;

	setup_for_ioctl(&cmd, lun, cdb, 6, 0, inquiry_buf, buf_size);

	if (debug)
		fprintf(stderr, "Getting inquiry page 0x%02x from LUN:0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			inquiry_page,
			lun[0], lun[1], lun[2], lun[3],
			lun[4], lun[5], lun[6], lun[7]);

	status = ioctl(fd, CCISS_PASSTHRU, &cmd);
	CHECK_IOCTL(file, "INQUIRY", status, &cmd, -1); /* macro which may return... */
	return 0;
}

/*****************************************************************

	init_cciss_to_bmic()

	There are two ways of addressing logical drives, and it depends what
	you're doing which way you have to use.  Generally, SCSI things,
	like reads, writes, inquiries, etc. use the 8-byte LUN as reported by
	REPORT_LOGICAL_LUNS.  And BMIC commands use another way, I'll call
	it "the BMIC way."

	There can be controllers behind controllers.  For instance, you might
	have a 6400 HBA cabled to a MSA500 external box.  The 6400's a smartarray
	PCI board connected via SCSI cables to the MSA500.  The MSA500 has a
	Smartarray RAID controller inside it.  The controller in the MSA500 will
	have an 8-byte LUN address, as does the 6400.  The 6400's LUN will be all
	zeroes.  The MSA500's will be something else, as reported by REPORT_PHYSICAL_LUNS,
	and identified as an MSA500 by an inquiry to that LUN.

	Further complications can come up with SSP, in that there may be logical
	drives addressable the "BMIC way", which are not addressable via any
	8-byte LUN from the host. (more of a SAN thing, but anyhow...)

	So, an address of a logical drive "the BMIC way" consists of the LUN of
	the controller which that logical drive is on, plus a "BMIC drive number"
	which gets crammed into the BMIC CDB.

	So, how do you figure out the BMIC address which corresponds to a given
	8-byte lun address?  For specific controllers, there may be some shortcuts.
	However, to do it in a general way, without having to be aware of nasty
	controller details nobody should be forced to be aware of, the following
	algorithm is implemented.

		Do a report logical LUNs to get a list of LUNs.  (On some controllers
		which support a large number of logical drives, this may take
		awhile to complete.)

		Get inquiry page 0x83 from each LUN which supports it,
		and remember this data.

		Do a report physical LUNs, and an inquiry to each to find
		any external RAID controllers (e.g. MSA500).

		For each controller (LUN 0x0000000000000000 plus any external
		controllers found in previous step) do:

			BMIC 0x10, identify logical drive.
			This returns, among other things, same data as inquiry page 0x83, above.

		Match up the data returned by BMIC 0x10 with Inquiry page 0x83.

	init_cciss_to_bmic implements this algorithm.

	You might wonder why instead, we don't just have for example vendor specific
	inquiry pages accessible via the 8-byte LUN which report the same things
	as are accessible via the old "BMIC way", esp, e.g. SENSE_LOGICAL_DRIVE_STATUS.
	I wonder the same thing myself.

	This function has the side effect of filling out controller_lun_list[]
	and num_controllers.

***************************************8***************************/

static int init_cciss_to_bmic(char *file, int fd)
{

	/* Create a mapping between 8-byte luns from REPORT_LOGICAL_LUNS
	   and 8-byte controller LUN address + BMIC drive number tuples.
	   This mapping is stored in the cciss_to_bmic global array.  */

#pragma pack(1)
	struct identify_logical_drive id_logical_drive_data;
#pragma pack()
	unsigned long long lunlist[1025];
	int luncount = 1024;
	unsigned long long physlunlist[1025];
	unsigned char buf[256];
	struct bmic_addr_t missed_drive[1025];
	int nmissed = 0;
	int nguessed = 0;
	int rc;
	int i, j, k, m;
	int ctlrtype;

	memset(&cciss_to_bmic, 0, sizeof(cciss_to_bmic));
	memset(lunlist, 0, sizeof(lunlist));
	memset(physlunlist, 0, sizeof(physlunlist));
	memset(missed_drive, 0, sizeof(missed_drive));

	/* Do report LOGICAL LUNs to get a list of all logical drives */
	rc = do_report_luns(file, fd, &luncount, (unsigned char *) lunlist, 0);
	if (rc != 0) {
		fprintf(stderr, "do_report_luns(logical) failed.\n");
		everything_hunky_dory = 0;
		return -1;
	}

	cciss_to_bmic.naddrs = luncount;

	/* For each logical drive... */
	for (i = 0; i < cciss_to_bmic.naddrs; i++) {
		memcpy(cciss_to_bmic.addr[i].logical_lun, &lunlist[i+1], 8);
		if (debug) {
			unsigned char *x = (unsigned char *) &lunlist[i+1];
			fprintf(stderr, "%d: ", i);
			for (j = 0; j < 8; j++)
				fprintf(stderr, "%02x", x[j]);
			fprintf(stderr, "\n");
		}
		memset(buf, 0, sizeof(buf));

		/* Get inquiry page 0x83 to get the unique identifire */
		rc = do_cciss_inquiry(file, fd, cciss_to_bmic.addr[i].logical_lun, 0x83, buf, 255);
		if (rc != 0) {
			fprintf(stderr, "logical lun %d inquiry 0x83 failed, rc = %d.\n", i, rc);
			continue;
		}
		if (buf[7] != 16) {
			fprintf(stderr, "%s: buf[7] is not 16!  Very unexpected...\n", progname);
		}
		memcpy(&cciss_to_bmic.addr[i].inq_pg_0x83_data[0], &buf[8], 16);
	}

	/* Get a list of physical luns... we're looking for RAID controller devices */
	luncount = 1024;
	rc = do_report_luns(file, fd, &luncount, (unsigned char *) physlunlist, 1);
	if (rc != 0) {
		fprintf(stderr, "%s: do_report_physical failed.\n", progname);
		everything_hunky_dory = 0;
		return -1;
	}

	/* Add the PCI host controller itself to this list... */
	/* Careful, first 8 bytes of physlunlist are a count, not lun */
	memcpy(&physlunlist[luncount+1], ZEROLUN, 8);
	luncount++;
	memset(controller_lun_list[0], 0, 8);
	num_controllers = 0;

	for (i = 0;i < luncount;i++) { /* For each physical LUN... */

		struct identify_controller id_ctlr_data;
		int max_possible_drives = 0;

		/* Get the standard inquiry page, so we can look at the device type */
		memset(buf, 0, 100);
		rc = do_cciss_inquiry(file, fd, (unsigned char *) &physlunlist[i+1], 0, buf, 100);
		if (rc != 0) {
			if (debug)
				fprintf(stderr, "Inquiry to phys device %d failed.\n", i);
			/* Some devices won't respond well to inquiry, this is expected, if hokey. */
			continue;
		}

		/* If it's not a RAID controller, skip it. */
		if ((buf[0] & 0x0f) != 0x0C) { /* devicetype != RAID_CONTROLLER */
			if (debug)
				fprintf(stderr, "Not a RAID controller, skipping.\n");
			continue;
		}

		if (debug) {
			int m;
			fprintf(stderr, "Querying RAID controller: ");
			for (m = 16; m < 36; m++)
				fprintf(stderr, "%c", buf[m]);
			fprintf(stderr,"\n");
		}

		/* Issue IDENTIFY_PHYSICAL_CONTROLLER to get number of logical drives */
		/* possible and present on this particular controller */

		memset(&id_ctlr_data, 0, sizeof(id_ctlr_data));
		rc = id_ctlr_fd(file, fd, (unsigned char *) &physlunlist[i+1], &id_ctlr_data);
		if (rc != 0) {
			fprintf(stderr, "%s: do_id_ctlr on lun %d failed.\n", progname, i);
			continue;
		}

		/* Check to see if it's a standby or secondary controller in an */
		/* active/standby or primary/secondary configuration.  If it is */
		/* not the preferred controller, you can't really talk to it */
		/* very much.  (MSA500 stuff, typically).  */
		if (((id_ctlr_data.redund_ctlr_modes_support & SUPPORT_ACTIVE_STANDBY) ||
			(id_ctlr_data.redund_ctlr_modes_support & SUPPORT_PRIMARY_SECONDARY)) &&
			((id_ctlr_data.curr_redund_ctlr_mode & 0x03) != 0) &&
			!(id_ctlr_data.redund_ctlr_status & PREFERRED_REDUNDANT_CTLR)) {
			if (debug)
				fprintf(stderr, "\nSkipping standby/secondary controller.\n");
			continue;
		}

		/* Record this controller for later checking of fan/power/temp status */
		memcpy(controller_lun_list[num_controllers], (unsigned char *) &physlunlist[i+1], 8);
		busses_on_this_ctlr[num_controllers] = id_ctlr_data.scsi_chip_count;

		/* For SAS controllers, there's a different way to figure "busses"
		 * (how many storage boxes can be attached) than using the scsi_chip_count,
		 * but it's not publically documented.  So, I'm just going to use 16 for SAS
		 * and "unknown" controllers.
		 */
		ctlrtype = lookup_controller(id_ctlr_data.board_id);
		if (ctlrtype == -1 || smartarray_id[ctlrtype].supports_sas)
			busses_on_this_ctlr[num_controllers] = 16;

		num_controllers++;

		max_possible_drives = id_ctlr_data.usMaxLogicalUnits;
		if (debug) {
			fprintf(stderr, "%s: max possible drives is %d\n\n",
				progname, max_possible_drives);
		}
		if (max_possible_drives > 512) {
			fprintf(stderr, "%s: max possible drives reportedly %d > 512,"
					" very unexpected, exiting.\n",
				progname, max_possible_drives);
			exit(1);
		}

		if (max_possible_drives == 0) { /* some controllers are just too old... */
			if (debug)
				fprintf(stderr, "max_possible drives was zero, adjusting to 32.\n");
			max_possible_drives = 32;
		}

		/* The below loop could be optimized slightly, in that ID_CONTROLLER
		   tells us also the number of logical drives present as well as possible,
		   so, we could bail as soon as we discover that number.  Also we know
		   a number from Report logical luns, and this is potentially smaller
		   than that reported by ID_CONTROLLER, due to SSP (selective storage
		   presentation.)  We could bail when all logical drives reported by
		   REPORT_LOGICAL_LUNS were matched.  Also, we could remove items from
		   the list being matched against once they were matched.
		*/

		/* For each possible logical drive on this raid controller... */
		for (j = 0; j < max_possible_drives; j++) {
			unsigned char *lundata = (unsigned char *) &physlunlist[i+1];

			/* Issue BMIC IDENTIFY_LOGICAL_DRIVE cmd to get unique identifier */
			memset(&id_logical_drive_data, 0, sizeof(id_logical_drive_data));
			rc = do_bmic_id_logical_drive(file, fd,
				lundata, j, (unsigned char *) &id_logical_drive_data);
			if (rc != 0) {
				fprintf(stderr, "%s: do_bmic_id_logical_drive failed for drive %d\n",
					progname, j);
				continue;
			}

			/* If every byte of identify logical drive data is 0, drive doesn't exist. */
			if (all_same((unsigned char *) &id_logical_drive_data,
					sizeof(id_logical_drive_data), 0)) {
				if (debug)
					fprintf(stderr, "id_logical_drive_data is all zeroes, "
						"means there is no logical drive %d\n", j);
				continue;
			}

			if (debug) {
				fprintf(stderr, "Logical drive 0x%02x%02x%02x%02x%02x%02x%02x%02x:%d has unique id: 0x",
					lundata[0], lundata[1], lundata[2], lundata[3],
					lundata[4], lundata[5], lundata[6], lundata[7], j);
				for (m = 0; m < 16; m++)
					fprintf(stderr, "%02x", id_logical_drive_data.unique_volume_id[m]);
				fprintf(stderr, "\nSearching....\n");
			}

			/* Search through list of previously found page 0x83 data for a match */
			for (k = 0; k < cciss_to_bmic.naddrs; k++) {
				int m;
				/* Check if results of id_logical_drive match
					cciss_to_bmic.addr[i].inq_pg_0x83_data */


				if (debug) {
					fprintf(stderr, "    ");
					for (m = 0; m < 16; m++)
						fprintf(stderr, "%02x", cciss_to_bmic.addr[k].inq_pg_0x83_data[m]);
					fprintf(stderr, "\n");
				}

				if (memcmp(id_logical_drive_data.unique_volume_id,
					cciss_to_bmic.addr[k].inq_pg_0x83_data, 16) == 0) {
						/* Found a match, store it away... */
						if (debug)
							fprintf(stderr, "Found!, k = %d\n", k);
						cciss_to_bmic.addr[k].bmic_drive_number = j;
						memcpy(cciss_to_bmic.addr[k].controller_lun, &physlunlist[i+1], 8);
						memcpy(cciss_to_bmic.addr[k].bmic_id_ctlr_data,
							id_logical_drive_data.unique_volume_id, 16);
						cciss_to_bmic.addr[k].tolerance_type =
							id_logical_drive_data.tolerance_type;
						cciss_to_bmic.addr[k].certain = 1;
					break;
				}
			}
			if (k == cciss_to_bmic.naddrs) {
				if (debug)
					fprintf(stderr, "Didn't find %d here.  Adding to missed drive list as bmic drive %d\n", i, j);
				memcpy(missed_drive[nmissed].controller_lun, &physlunlist[i+1], 8);
				missed_drive[nmissed].bmic_drive_number = j;
				missed_drive[nmissed].tolerance_type = id_logical_drive_data.tolerance_type;
				nmissed++;
			}
		}



		/* Ugh.  This is really ugly.  MSA500 does not have the */
		/* Unique ID in the identify_logical_drive_data.  In that case...sigh... */
		/* just scan through our list of unmatched ones and assign them sequentially */
		/* MOST times this will be correct.  Apparently some other old controllers */
		/* have the same problem as well, e.g.: Smart Array 5300 */

		for (m = 0; m < nmissed; m++) {
			for (k = 0; k < cciss_to_bmic.naddrs; k++) {
				if (!all_same(cciss_to_bmic.addr[k].bmic_id_ctlr_data, 16, 0))
					continue;

				if (debug)
					fprintf(stderr, "mapping logical drive %d to bmic drive %d(%d)\n",
					k, missed_drive[m].bmic_drive_number, m);
				memcpy(cciss_to_bmic.addr[k].controller_lun,
					missed_drive[m].controller_lun, 8);
				cciss_to_bmic.addr[k].bmic_drive_number =
					missed_drive[m].bmic_drive_number;
				cciss_to_bmic.addr[k].tolerance_type =
					missed_drive[m].tolerance_type;
				memset(cciss_to_bmic.addr[k].bmic_id_ctlr_data, 0xff, 16);
				/* Make a note of the fact that we're guessing. */
				cciss_to_bmic.addr[k].certain = 0;
				nguessed++;
				break;
			}
		}

		if (nguessed != nmissed) {
			/* This happens because the controller reported a maximum */
			/* of 0 logical drives, and we bumped it up to 32 knowing that */
			/* was wrong, so these "missed" drives aren't real. Nothing to do. */
			;
		}
	}

	if (debug) {
		/* Print table of mappings between Logical LUNS and (controller lun, bmic drive number) tuples. */
		fprintf(stderr, "\n\n\n");
		for (i = 0; i < cciss_to_bmic.naddrs; i++) {
			fprintf(stderr, "0x%02x%02x%02x%02x%02x%02x%02x%02x 0x%02x%02x%02x%02x%02x%02x%02x%02x %d\n",
				cciss_to_bmic.addr[i].logical_lun[0],
				cciss_to_bmic.addr[i].logical_lun[1],
				cciss_to_bmic.addr[i].logical_lun[2],
				cciss_to_bmic.addr[i].logical_lun[3],
				cciss_to_bmic.addr[i].logical_lun[4],
				cciss_to_bmic.addr[i].logical_lun[5],
				cciss_to_bmic.addr[i].logical_lun[6],
				cciss_to_bmic.addr[i].logical_lun[7],
				cciss_to_bmic.addr[i].controller_lun[0],
				cciss_to_bmic.addr[i].controller_lun[1],
				cciss_to_bmic.addr[i].controller_lun[2],
				cciss_to_bmic.addr[i].controller_lun[3],
				cciss_to_bmic.addr[i].controller_lun[4],
				cciss_to_bmic.addr[i].controller_lun[5],
				cciss_to_bmic.addr[i].controller_lun[6],
				cciss_to_bmic.addr[i].controller_lun[7],
				cciss_to_bmic.addr[i].bmic_drive_number);
		}
	}
	return 0;
}

static void setup_sense_bus_params_cmd(IOCTL_Command_struct *c,
	unsigned char lunaddr[], unsigned char bus,
	sense_bus_param *bus_param)
{
	memset(c, 0, sizeof(*c));
	memcpy(&c->LUN_info, lunaddr, 8);
	c->Request.CDBLen = 10;
	c->Request.Type.Type = TYPE_CMD;
	c->Request.Type.Attribute = ATTR_SIMPLE;
	c->Request.Type.Direction = XFER_READ;
	c->Request.Timeout = 0;
	c->Request.CDB[0] = 0x26; /* 0x26 means "CCISS READ" */
	c->Request.CDB[1] = 0; /* logical drive id? */
	c->Request.CDB[5] = bus; /* bus id */
	c->Request.CDB[6] = SENSE_BUS_PARAM;
	c->buf_size = sizeof(*bus_param);
	c->Request.CDB[7] = (c->buf_size >> 8) & 0xff;
	c->Request.CDB[8] = c->buf_size  & 0xff;
	c->buf = (unsigned char *) bus_param;
}

static void print_error_info(IOCTL_Command_struct *c)
{
	int i;
	printf("Error info:\n");
	printf("CDB = ");
	for (i = 0; i < 16; i++)
		printf("%02x ", c->Request.CDB[i]);
	printf("\n");
	printf("CommandStatus = %d\n", c->error_info.CommandStatus);
	printf("ScsiStatus = %d\n", c->error_info.ScsiStatus);
	printf("SenseLen = %d\n", c->error_info.SenseLen);
	printf("ResidualCnt = %d\n", c->error_info.ResidualCnt);
	printf("SenseInfo = ");
	if (c->error_info.SenseLen > SENSEINFOBYTES)
		c->error_info.SenseLen = SENSEINFOBYTES;
	for (i = 0; i < c->error_info.SenseLen; i++)
		printf("%02x ", c->error_info.SenseInfo[i]);
	printf("\n\n");
}

int do_sense_bus_parameters(char *file, int fd, unsigned char lunaddr[],
	int ctlr, unsigned char bus, sense_bus_param *bus_param)
{
	IOCTL_Command_struct c;
	int status;

	setup_sense_bus_params_cmd(&c, lunaddr, bus, bus_param);
	if (debug)
		fprintf(stderr, "Getting bus status for bus %d\n", bus);
	/* Get bus status */
	status = ioctl(fd, CCISS_PASSTHRU, &c);

	if (status != 0) {
		fprintf(stderr, "%s: %s: ioctl: controller: %d bus: %d %s\n",
			progname, file, ctlr, bus, strerror(errno));
		/* This really should not ever happen. */
		everything_hunky_dory = 0;
		return -1;
	}

	/* This happens when we query busses that don't exist. */
	if (c.error_info.CommandStatus == 4) /* 4 means "invalid command" */
		return -1;

	if (c.error_info.CommandStatus == 1)
		print_error_info(&c);

	if (c.error_info.CommandStatus != 0) {
		fprintf(stderr, "Error getting status for %s "
			"controller: %d bus: %d: Commandstatus is %d\n",
			file, ctlr, bus, c.error_info.CommandStatus);
		everything_hunky_dory = 0;
		return -1;
	}
	if (debug)
		fprintf(stderr, "Status for controller %d bus %d "
			"seems to be gettable.\n", ctlr, bus);
	return 0;
}

static void check_fan_power_temp(char *file, int ctlrtype, int fd, int num_controllers)
{
	int i, j;

	/* Check the fan/power/temp status of each controller */
	for (i = 0; i < num_controllers; i++) {
		for (j = 0; j < busses_on_this_ctlr[i]; j++) {
			sense_bus_param bus_param;
			memset(&bus_param, 0, sizeof(bus_param));
			if (do_sense_bus_parameters(file, fd, controller_lun_list[i],
				i, j, &bus_param) != 0)
				continue;
			print_bus_status(file, ctlrtype, j, &bus_param);
		}
	}
}

static int msa1000_status(char *file, int fd)
{
	int i, rc, numluns;
	struct identify_controller id;

	rc = msa1000_passthru_ioctl(fd, ID_CTLR, &id, sizeof(id), 0);
	if (rc < 0) {
		fprintf(stderr, "%s: %s: Can't identify controller, id = 0x%08x.\n",
			progname, file, id.board_id);
		if (persnickety)
			everything_hunky_dory = 0;
		return -1;
	}
	numluns = id.num_logical_drives;
	for (i = 0; i < numluns; i++) /* We know msa1000 supports only 32 logical drives */
		msa1000_logical_drive_status(file, fd, i, &id);

	/* We're not doing fan/temp/power for msa1000 for now. */
	close(fd);
	return 0;
}

static int cciss_device_type_is_correct(char *file, int fd)
{
	int rc;
	struct stat statbuf;

	/* Not a SCSI device.  Stat the file to see if it's a block device file */
	rc = fstat(fd, &statbuf);
	if (rc < 0) {
		fprintf(stderr, "%s: cannot stat %s: %s\n",
			progname, file, strerror(errno));
		if (persnickety)
			everything_hunky_dory = 0;
		return -1;
	}

	/* On linux, we want a block device; on freebsd, character */
#if HAVE_CISSIO_H
#define WANTED_DEVICE_TYPE
#define WANTED_DEVICE_TYPE_STRING "character"
#else
#define WANTED_DEVICE_TYPE !
#define WANTED_DEVICE_TYPE_STRING "block"
#endif
	if (WANTED_DEVICE_TYPE S_ISBLK(statbuf.st_mode)) {
		fprintf(stderr, "%s: %s is not a %s device.\n",
			progname, file, WANTED_DEVICE_TYPE_STRING);
		if (persnickety)
			everything_hunky_dory = 0;
		return -1;
	}
	return 0;
}

static void cciss_logical_drive_status(char *file, int fd,
	struct identify_controller *id, int ctlrtype, int volume_number, int is_scsi)
{
	IOCTL_Command_struct c;
	struct identify_logical_drive_status ldstatus;
	int status;
	char *scsi_device_node;

	/* Construct command to get logical drive status */
	memset(&c, 0, sizeof(c));
	memset(&ldstatus, 0, sizeof(ldstatus));
	memcpy(&c.LUN_info, cciss_to_bmic.addr[volume_number].controller_lun, 8);
	c.Request.CDBLen = 10;
	c.Request.Type.Type = TYPE_CMD;
	c.Request.Type.Attribute = ATTR_SIMPLE;
	c.Request.Type.Direction = XFER_READ;
	c.Request.Timeout = 0;
	c.Request.CDB[0] = 0x26; /* 0x26 means "CCISS READ" */
	c.Request.CDB[1] = cciss_to_bmic.addr[volume_number].bmic_drive_number & 0xff;
	c.Request.CDB[6] = ID_LSTATUS;
	c.buf_size = sizeof(ldstatus);
	c.Request.CDB[7] = (c.buf_size >> 8) & 0xff;
	c.Request.CDB[8] = c.buf_size  & 0xff;
	c.Request.CDB[9] = (cciss_to_bmic.addr[volume_number].bmic_drive_number >> 8) & 0x0ff;
	c.buf = (unsigned char *) &ldstatus;

	if (debug)
		fprintf(stderr, "Getting logical drive status for drive %d\n", volume_number);
	/* Get logical drive status */
	status = ioctl(fd, CCISS_PASSTHRU, &c);
	if (status != 0) {
		fprintf(stderr, "%s: %s: ioctl: logical drive: %d %s\n",
			progname, file, volume_number, strerror(errno));
		/* This really should not ever happen. */
		everything_hunky_dory = 0;
		return;
	}
	if (c.error_info.CommandStatus != 0) {
		fprintf(stderr, "Error getting status for %s "
			"volume %d: Commandstatus is %d\n",
			file, volume_number, c.error_info.CommandStatus);
		everything_hunky_dory = 0;
		return;
	} else {
		if (debug)
			fprintf(stderr, "Status for logical drive %d seems to be gettable.\n",
				volume_number);
	}
	scsi_device_node = file;
	if (is_scsi)
		find_scsi_device_node(cciss_to_bmic.addr[volume_number].bmic_id_ctlr_data,
			&scsi_device_node);
	else
		scsi_device_node = file;
	print_volume_status(scsi_device_node, fd, ctlrtype,
		cciss_to_bmic.addr[volume_number].controller_lun,
		volume_number, &ldstatus, id,
		cciss_to_bmic.addr[volume_number].tolerance_type,
		cciss_to_bmic.addr[volume_number].certain);
}

static inline int bmic_supports_big_maps(struct identify_controller *id)
{
	return id->controller_flags && (1<<7);
}

static int bmic_next_disk_bits(uint8_t *bits, int bitmapsize, int disk)
{
	int i;
	for (i = disk + 1; i < bitmapsize; i++ )
		if (bitisset(bits, i, bitmapsize))
			return i;
	return -1;
}

static inline int bmic_next_phy_disk(struct identify_controller *id, int bmic_drive_number)
{
	if (bmic_supports_big_maps(id))
		return bmic_next_disk_bits((uint8_t *) id->big_drive_present_map, 128, bmic_drive_number); 
	else
		return bmic_next_disk_bits((uint8_t *) &id->drive_present_bit_map, 32, bmic_drive_number); 
}

static inline int bmic_next_ext_phy_disk(struct identify_controller *id, int bmic_drive_number)
{
	if (bmic_supports_big_maps(id))
		return bmic_next_disk_bits((uint8_t *) id->big_ext_drive_map, 128, bmic_drive_number); 
	else
		return bmic_next_disk_bits((uint8_t *) &id->external_drive_bit_map, 32, bmic_drive_number); 
}

static inline int bmic_next_non_disk(struct identify_controller *id, int bmic_drive_number)
{
	if (bmic_supports_big_maps(id))
		return bmic_next_disk_bits((uint8_t *) id->big_non_disk_map, 128, bmic_drive_number); 
	else
		return bmic_next_disk_bits((uint8_t *) &id->non_disk_map, 32, bmic_drive_number); 
}

static void check_physical_drive(char *file, int fd,
	unsigned char *controller_lun, struct identify_controller *id,
	int bmic_drive_number)
{
	int rc = 0, bus, target;
	struct identify_physical_device device_data;
	char location[1000];
	int ctlrtype;
	char status[100];

	ctlrtype = lookup_controller(id->board_id);

	/* Get IDENTIFY PHYSICAL DEVICE data for this drive */
	rc = 0;
	memset(&device_data, 0, sizeof(device_data));
#ifdef HAVE_SCSI_SG_H
	if (controller_lun)
#endif
		rc = do_bmic_identify_physical_device(file, fd,
			controller_lun, bmic_drive_number, &device_data);
#ifdef HAVE_SCSI_SG_H
	else
		rc = do_sgio_bmic_identify_physical_device(fd, bmic_drive_number, &device_data);
#endif
	if (rc) {
		printf("Cannot get device information for %s: controller: 0x%02x%02x%02x%02x%02x%02x%02x%02x"
			" bmic drive number: %d\n", 
			file,
			controller_lun[0], controller_lun[1], controller_lun[2], controller_lun[3],
			controller_lun[4], controller_lun[5], controller_lun[6], controller_lun[7],
			bmic_drive_number);
		everything_hunky_dory = 0;
		return;
	}

	find_bus_target(id, bmic_drive_number, &bus, &target);
	format_phys_drive_location(location, bus, target, ctlrtype,
		controller_lun, rc ? NULL : &device_data);
	sprintf(status, "OK");
	/* Check S.M.A.R.T data */
	
	if (!(device_data.more_physical_drive_flags & 0x01)) /* supports S.M.A.R.T.? */
		goto print_data;
	if (!(device_data.more_physical_drive_flags & 0x04)) /* S.M.A.R.T enabled? */
		goto print_data;
	if (device_data.more_physical_drive_flags & 0x02) { /* S.M.A.R.T. predictive failure bit set? */
		everything_hunky_dory = 0;
		sprintf(status, "S.M.A.R.T. predictive failure.");
	}
print_data:
	if (strcmp(status, "OK") == 0 && !verbose)
		return;
	printf("%s %s\n", location, status);
}

static void check_ctlr_physical_drives(char *file, int fd,
	unsigned char *controller_lun)
{
	int i, rc;
	struct identify_controller id;
	int physical_drive_count = 0;

	rc = id_ctlr_fd(file, fd, controller_lun, &id);
	if (rc != 0) {
		fprintf(stderr, "%s: cannot identify controller 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			file,
			controller_lun[0], controller_lun[1], controller_lun[2], controller_lun[3],
			controller_lun[4], controller_lun[5], controller_lun[6], controller_lun[7]);
		everything_hunky_dory = 0;
		return;
	}

	for (i = bmic_next_phy_disk(&id, -1); i != -1; i = bmic_next_phy_disk(&id, i))
		physical_drive_count++;

	if (verbose)
		printf("  Physical drives: %d\n", physical_drive_count);

	/* For each physical disk i... */
	for (i = bmic_next_phy_disk(&id, -1); i != -1; i = bmic_next_phy_disk(&id, i))
		check_physical_drive(file, fd, controller_lun, &id, i);
}

static void check_physical_drives(char *file, int fd)
{
	int i;

	if (!check_smart_data && !verbose)
		return;

	for (i = 0; i < num_controllers; i++)
		check_ctlr_physical_drives(file, fd, controller_lun_list[i]);
}

static void print_controller_info(struct identify_controller *id, int ctlrtype)
{
	if (!verbose)
		return;

	printf("Controller: %s\n", smartarray_id[ctlrtype].board_name);
	printf("  Board ID: 0x%08x\n", id->board_id);
	printf("  Logical drives: %hu\n", id->usExtLogicalUnitCount);
	printf("  Running firmware: %c%c%c%c\n",
		id->running_firm_rev[0], id->running_firm_rev[1],
		id->running_firm_rev[2], id->running_firm_rev[3]);
	printf("  ROM firmware: %c%c%c%c\n",
		id->rom_firm_rev[0], id->rom_firm_rev[1],
		id->rom_firm_rev[2], id->rom_firm_rev[3]);
}

static int cciss_status(char *file)
{

	int fd, i, rc;
	struct identify_controller id;
	int ctlrtype;
	int bus;
	int is_scsi = 0;

	fd = open(file, O_RDWR);
	if (fd < 0) {
		if (errno != ENXIO || persnickety)
			fprintf(stderr, "%s: open %s: %s\n",
				progname, file, strerror(errno));
		if (persnickety)
			everything_hunky_dory = 0;
		return -1;
	}

#ifdef HAVE_SCSI_SG_H
	/* See if it's a scsi device... */
	rc = ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus);
	if (rc == 0) {
		is_scsi = 1;
		/* It's a SCSI device... */
		if (is_msa1000(fd))
			return msa1000_status(file, fd);

		if (!is_smartarray_driver(fd)) {
			fprintf(stderr, "%s: %s: Unknown SCSI device.\n", progname, file);
			if (persnickety)
				everything_hunky_dory = 0;
			close(fd);
			return -1;
		}
	} else
#endif
		if (cciss_device_type_is_correct(file, fd) != 0) /* sanity check on device node */
			return -1;

	/* See if it's a smartarray of some kind... */
	rc = id_ctlr_fd(file, fd, ZEROLUN, &id);
	if (rc < 0) {
		if (persnickety)
			everything_hunky_dory = 0;
		return -1;
	}

	/* Look it up in the table */
	ctlrtype = lookup_controller(id.board_id);
	if (ctlrtype == -1) {
		fprintf(stderr, "%s: %s: Unknown controller, board_id = 0x%08x\n",
			progname, file, id.board_id);
		if (persnickety)
			everything_hunky_dory = 0;
		return -1;
	}

	print_controller_info(&id, ctlrtype);

	/* Construct mapping of CISS LUN addresses to "the BMIC way" */
	rc = init_cciss_to_bmic(file, fd);
	if (rc != 0) {
		fprintf(stderr, "%s: Internal error, could not construct CISS to BMIC mapping.\n",
				progname);
		everything_hunky_dory = 0;
		return -1;
	}

	for (i = 0; i < cciss_to_bmic.naddrs; i++)
		cciss_logical_drive_status(file, fd, &id, ctlrtype, i, is_scsi);

	check_physical_drives(file, fd);

	/* Check the fan/power/temp status of each controller */
	check_fan_power_temp(file, ctlrtype, fd, num_controllers);
	close(fd);
	return 0;
}

static void intro()
{
	if (isatty(2) && !be_quiet) { /* Only print this message if stderr is a tty */
		fprintf(stderr, "%s version %s,\n"
			"Copyright (C) 2007 Hewlett-Packard Development Company, L.P.\n",
			progname, VERSION_NUMBER);
		fprintf(stderr, "%s comes with ABSOLUTELY NO WARRANTY.\n", progname);
		fprintf(stderr, "This is free software, and you are welcome to redistribute it\n");
		fprintf(stderr, "under certain conditions.  ");
		fprintf(stderr, "See the file 'COPYING', which you \n");
		fprintf(stderr, "should have received with this program, for details regarding\n");
		fprintf(stderr, "the absence of warranty and the conditions for redistribution.\n\n");
		exit(0);
	}
}

static struct option longopts[] = {
	{ "persnickety", 0, NULL, 'p'},
	{ "quiet", 0, NULL, 'q'}, /* doesn't do anything anymore. */
	{ "try-unknown-devices", 0, NULL, 'u'},
	{ "version", 0, NULL, 'v'},
	{ "exhaustive", 0, NULL, 'x'},
	{ "smart", 0, NULL, 's'},
	{ "copyright", 0, NULL, 'C'}, /* opposite of -q */
	{ "verbose", 0, NULL, 'V'},
	{ NULL, 0, NULL, 0},
};

int main(int argc, char *argv[])
{
	int i, opt;

	do {

		opt = getopt_long(argc, argv, "dpqusvVxC", longopts, NULL );
		switch (opt) {
			case 'd': debug = 1;
				continue;
			case 'p': persnickety = 1;
				continue;
			case 'q': be_quiet = 1; /* default now */
				continue;
			case 's': check_smart_data = 1;
				continue;
			case 'u': try_unknown_devices = 1;
				continue;
			case 'C': be_quiet = 0;
				intro();
				continue;
			case 'v': fprintf(stderr, "%s version %s%s\n", progname,
					VERSION_NUMBER,
#ifdef HAVE_SCSI_SG_H
					""
#else
					"(without support for MSA1000)"
#endif
					);
				  exit(0);
			case 'x' : exhaustive_search = 1;
					/* exhaustive search doesn't really do anything anymore. */
				continue;
			case 'V' : verbose = 1;
				continue;
			case '?' :
			case ':' :
				usage(); /* usage calls exit(), so no fall thru */
			case -1 :
				break;
		}
	} while (opt != -1);

	if ((argc - optind) < 1)
		usage();

	intro();

	for (i = optind; i < argc; i++)
		cciss_status(argv[i]);
	free_device_node_cache();
	exit((everything_hunky_dory != 1));
}

