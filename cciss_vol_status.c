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

#include <libgen.h>
#include <sys/ioctl.h>

#ifdef HAVE_SCSI_SG_H
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <netinet/in.h> /* for htons() */
#endif

#include <inttypes.h>
#include <getopt.h>

#ifdef HAVE_LINUX_CCISS_IOCTL_H
/* Some versions of cciss_ioctl.h contain a "__user" attribute which 
   needs masking out to compile outside the kernel. */
#define __user
#include <linux/cciss_ioctl.h>
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
	{ 0xe0110e11, "HP MSA500",		1, 0}, /* aka Smart Array CL */
	{ 0xe0200e11, "HP MSA500 G2",		1, 0},
#ifdef HAVE_SCSI_SG_H
	{ MSA1000_ID, "MSA1000",	1,},
#else
#warning Since <scsi/sg.h> is not around, MSA1000 support will not be compiled.
#endif
	{ 0xFFFFFFFF, "Unknown Smart Array",	0,},
};

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

/* See the following documents for information about the hardware 
 * specific structures used in this program:
 *
 *	fwspecwww.doc from http://sourceforge.net/progjects/cpqarray
 *	Open_CISS_Spec.pdf from http://sourceforge.net/progjects/cciss
 *	and see also the ciss.h header file from the freebsd ciss driver
 */

/* Structure returned by command to get logical drive status (0x12) */
#pragma pack(1)
typedef struct
{
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
} ida_id_lstatus;
#pragma pack()

const char *spare_drive_status_msg[] = {
		/* Corresponds to bits in spare_status field, above */
		/* bit 0 */  "At least one spare drive",
		/* bit 1 */  "At least one spare drive activated and currently rebuilding",
		/* bit 2 */  "At least one spare drive has failed",
		/* bit 3 */  "At least one spare drive activated",
		/* bit 4 */  "At least one spare drive remains available",
};
#define NSPARE_MSGS (sizeof(spare_drive_status_msg) / sizeof(spare_drive_status_msg[0]))

#pragma pack(1)
/* Structure returned by Identify Controller command (0x11) */
typedef struct {
	unsigned char num_logical_drives;
	uint32_t signature;
	uint32_t running_firm_rev;
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

} id_ctlr;
#pragma pack()

#pragma pack(1)
typedef struct id_logical_drive_t {
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
} id_logical_drive;
#define ID_LOGICAL_DRIVE 0x10
#pragma pack()

char *progname = "cciss_vol_status";

void usage()
{
	fprintf(stderr, "%s: usage: %s [-p] [-q] [-v] [-u] [-x] /dev/cciss/c*d0 /dev/sg*\n", progname, progname);
	fprintf(stderr, " -p  --persnickety          Complain about device nodes which can't be opened.\n");
	fprintf(stderr, " -u  --try-unknown-devices  Allow interrogation of even unrecognized controllers\n");
	fprintf(stderr, "                            (useful for brand new hardware.)\n");
	fprintf(stderr, " -v  --version              Print program version and exit.\n");
	fprintf(stderr, " -C  --copyright            Print copyright notice first.\n");
	exit(-1);
}

int bitisset(unsigned char bitstring[], int bit, int bitstringlength)
{
	int element;
	int offset;
	unsigned char or_val;

	element = (bit / 8);
	offset  = bit % 8;
	or_val = (unsigned char) (1 << offset);

	return ((bitstring[element] & or_val) != 0);
}

int check_ioctl_results(char *filename, char *command,
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
		char *cdb, int cdblen, 
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

int do_report_luns(char *filename, int fd, int *count, unsigned char *lun, int physical)
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
		for (i=8;i<(*count+1) * 8;i++) {
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

int do_bmic_id_logical_drive(char *filename, int fd, 
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

int lookup_controller(uint32_t board_id);
int id_ctlr_fd(char * filename, int fd, unsigned char *lun, id_ctlr *id)
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

void show_disk_map(char *title,
		id_ctlr *id,
		unsigned char bigmap[],
		uint32_t smallmap
		)
{
	int bus, target, lastbus;
	int i, count;
	int big_map_support;
	int ndisks=0;
	int drives_per_scsi_bus;

	lastbus = -1;

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

	for (i=0;i < (big_map_support ? 128 : 16);i++)  { /* FIXME magic 128,16 */
		bus    = i / drives_per_scsi_bus;
		target = i % drives_per_scsi_bus;

	  	if ( (big_map_support &&
			bitisset((unsigned char *) bigmap, i, 128 / 8)) ||
			( (! big_map_support) &&
			bitisset((unsigned char *) &smallmap, i, sizeof(smallmap)))) {
			if (lastbus == -1)
				printf("%s", title);
			if (bus != lastbus) {
				lastbus = bus;
				count=0;
				printf("\n// bus %d:\n//    ", bus);
			}

			if (count != 0)
				printf(", ");

			lastbus = bus;
			printf("b%dt%d", bus, target);
			count++; ndisks++;

			if (count > 8)  {  /* reasonable for 80 column display */
				count=0;
				printf("\n//    ");
			}
		}
	}
	if (lastbus != -1)
		printf("\n");
}

char *decode_status[] = {
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

#define MAX_DECODE_STATUS (sizeof(decode_status) / sizeof(decode_status[0]))

void print_volume_status(char *file, int ctlrtype, int volume_number, 
	ida_id_lstatus *vs, id_ctlr *id, int tolerance_type, int certain)
{
	int spare_bit;
	unsigned char raid_level[100];

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
	
	for (spare_bit=0; spare_bit < NSPARE_MSGS; spare_bit++)
		if ((vs->spare_status >> spare_bit) & 0x01)
			printf("  %s.", spare_drive_status_msg[spare_bit]);
#if 0
	This message just makes people ask questions, without actually helping them. 
	They can just go look at the LEDs to see which disks are failed.

	if (vs->status != 0 && !smartarray_id[ctlrtype].can_decode_drive_map)
		printf("%s: Don't know how to decode drive map data for %s controllers.\n",
			progname, smartarray_id[ctlrtype].board_name);
#endif

	if (smartarray_id[ctlrtype].can_decode_drive_map) {
		show_disk_map("  Failed drives:", id,
				(unsigned char *) vs->big_failure_map, vs->drive_failure_map);
		show_disk_map("  'Replacement' drives:", id,
				(unsigned char *) vs->big_replace_map, vs->replacement_drive_map);
		show_disk_map("  Drives currently substituted for by spares:", id,
				(unsigned char *) vs->big_spare_map, vs->active_spare_map);
	}

	printf("\n");
}

int lookup_controller(uint32_t board_id)
{
	int i;

	for (i=0;smartarray_id[i].board_id != 0xFFFFFFFF; i++)
		if (smartarray_id[i].board_id == board_id)
			return i;
	if (try_unknown_devices)
		return i; /* Must be some fancy new controller we don't know yet. */
	return -1;
}


#ifdef HAVE_SCSI_SG_H

void setup_sgio(sg_io_hdr_t *sgio,
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

int do_sg_io(int fd, unsigned char *cdb, unsigned char cdblen, unsigned char *buffer,
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

int do_inquiry(int fd,
               unsigned char inquiry_page,
               unsigned char* inquiry_buf,
               unsigned char buf_size)
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

int msa1000_passthru_ioctl (int fd, int cmd, void *buffer, int size, uint log_unit)
{ 
	sg_io_hdr_t sgio, sgio2;
	unsigned char sensebuffer[64];
	int direction = SG_DXFER_FROM_DEV;
	unsigned char cdb[16];
	int cdblen, nbytes;
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

int is_msa1000(int fd)
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

void msa1000_logical_drive_status(char *file, int fd, 
	unsigned int logical_drive, id_ctlr *id)
{
	int rc, ctlrtype;
	ida_id_lstatus ldstatus;
	id_logical_drive drive_id;
	int tolerance_type = -1;

	rc = msa1000_passthru_ioctl(fd, ID_LOGICAL_DRIVE, &drive_id,
		sizeof(drive_id), logical_drive);
	if (rc == 0)
		tolerance_type = drive_id.tolerance_type;

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
	print_volume_status(file, ctlrtype, logical_drive, &ldstatus, id, tolerance_type, 1);
}

#endif /* ifdef HAVE_SCSI_SG_H */

int all_same(unsigned char *buf, unsigned int bufsize, unsigned char value) 
{
	int rc, i;

	rc = 0;
	for (i=0;i<bufsize;i++) {
		if (buf[i] != value)
			return 0;
	}
	return 1;
}

static int do_cciss_inquiry(char *file, int fd, 
		unsigned char *lun, 
		unsigned char inquiry_page,
               unsigned char* inquiry_buf,
               unsigned char buf_size)
{
	IOCTL_Command_struct cmd;
	unsigned char cdb[16];
	int status;

	memset(&cmd, 0, sizeof(cmd));
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
	
***************************************8***************************/ 

static int init_cciss_to_bmic(char *file, int fd)
{

	/* Create a mapping between 8-byte luns from REPORT_LOGICAL_LUNS
	   and 8-byte controller LUN address + BMIC drive number tuples.
	   This mapping is stored in the cciss_to_bmic global array.  */

#pragma pack(1)
	id_logical_drive id_logical_drive_data;
#pragma pack()
	unsigned long long lunlist[1025];
	unsigned int luncount = 1024;
	unsigned long long physlunlist[1025];
	unsigned char buf[256];
	struct bmic_addr_t missed_drive[1025];
	int nmissed = 0;
	int nguessed = 0;
	int rc;
	int i, j, k, m;
	
	memset(&cciss_to_bmic, 0, sizeof(cciss_to_bmic));

	/* Do report LOGICAL LUNs to get a list of all logical drives */
	rc = do_report_luns(file, fd, &luncount, (unsigned char *) lunlist, 0);
	if (rc != 0) {
		fprintf(stderr, "do_report_luns(logical) failed.\n");
		everything_hunky_dory = 0;
		return -1;
	}

	cciss_to_bmic.naddrs = luncount;

	/* For each logical drive... */
	for (i=0;i<cciss_to_bmic.naddrs;i++) {
		memcpy(cciss_to_bmic.addr[i].logical_lun, &lunlist[i+1], 8);
		if (debug) {
			unsigned char *x = (unsigned char *) &lunlist[i+1];
			fprintf(stderr, "%d: ", i);
			for (j=0;j<8;j++)
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

	for (i=0;i<luncount;i++) { /* For each physical LUN... */

		id_ctlr id_ctlr_data;
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
			for (m=16;m<36;m++)
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
		for (j=0;j<max_possible_drives;j++) {  
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
				for (m=0;m<16;m++)
					fprintf(stderr, "%02x", id_logical_drive_data.unique_volume_id[m]);
				fprintf(stderr, "\nSearching....\n");
			}

			/* Search through list of previously found page 0x83 data for a match */
			for (k=0;k<cciss_to_bmic.naddrs;k++) {
				int m;
				/* Check if results of id_logical_drive match 
					cciss_to_bmic.addr[i].inq_pg_0x83_data */


				if (debug) {	
					fprintf(stderr, "    ");
					for (m=0;m<16;m++)
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
		/* MOST times this will be correct. */ 

		for (m=0;m<nmissed;m++) {
			for (k=0;k<cciss_to_bmic.naddrs;k++) {
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
		for (i=0;i<cciss_to_bmic.naddrs;i++) {
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

int cciss_status(char *file)
{

	int fd, status, i, rc;
	IOCTL_Command_struct c;
	ida_id_lstatus ldstatus;
	id_ctlr id;
	struct stat statbuf;
	int ctlrtype;
	int numluns;
	int bus;
	int foundluns, maxluns, luncount;

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
		/* It's a SCSI device... maybe it's an MSA1000. */
		if (is_msa1000(fd)) {
			rc = msa1000_passthru_ioctl(fd, ID_CTLR, &id, sizeof(id), 0);
			if (rc < 0) {
				fprintf(stderr, "%s: %s: Can't identify controller, id = 0x%08x.\n",
					progname, file, id.board_id);
				if (persnickety)
					everything_hunky_dory = 0;
				return -1;
			}
			numluns = id.num_logical_drives;
			for (i=0;i<numluns;i++) /* We know msa1000 supports only 32 logical drives */
				msa1000_logical_drive_status(file, fd, i, &id);
			close(fd);
			return 0;
		}
		fprintf(stderr, "%s: %s: Unknown SCSI device.\n", progname, file);
		if (persnickety)
			everything_hunky_dory = 0;
		close(fd);
		return -1;
	}
#endif

	/* Stat the file to see if it's a block device file */
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

	/* Not a SCSI device, see if it's a smartarray of some kind... */
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

	/* Construct mapping of CISS LUN addresses to "the BMIC way" */
	rc = init_cciss_to_bmic(file, fd);
	if (rc != 0) {
		fprintf(stderr, "%s: Internal error, could not construct CISS to BMIC mapping.\n",
				progname);
		everything_hunky_dory = 0;
		return -1;
	}

	for (i=0;i<cciss_to_bmic.naddrs;i++) {
		/* Construct command to get logical drive status */
		memset(&c, 0, sizeof(c));
		memcpy(&c.LUN_info, cciss_to_bmic.addr[i].controller_lun, 8);
		c.Request.CDBLen = 10;
		c.Request.Type.Type = TYPE_CMD;
		c.Request.Type.Attribute = ATTR_SIMPLE;
		c.Request.Type.Direction = XFER_READ;
		c.Request.Timeout = 0;
		c.Request.CDB[0] = 0x26; /* 0x26 means "CCISS READ" */
		c.Request.CDB[1] = cciss_to_bmic.addr[i].bmic_drive_number & 0xff; 
		c.Request.CDB[6] = ID_LSTATUS;
		c.buf_size = sizeof(ldstatus);
		c.Request.CDB[7] = (c.buf_size >> 8) & 0xff;
		c.Request.CDB[8] = c.buf_size  & 0xff;
		c.Request.CDB[9] = (cciss_to_bmic.addr[i].bmic_drive_number >> 8) & 0x0ff;
		c.buf = (unsigned char *) &ldstatus;

		if (debug)
			fprintf(stderr, "Getting logical drive status for drive %d\n", i);
		/* Get logical drive status */
		status = ioctl(fd, CCISS_PASSTHRU, &c);
		if (status != 0) {
			fprintf(stderr, "%s: %s: ioctl: logical drive: %d %s\n",
				progname, file, i, strerror(errno));
			/* This really should not ever happen. */
			everything_hunky_dory = 0;
			continue;
		}
		if (c.error_info.CommandStatus != 0) {
			fprintf(stderr, "Error getting status for %s "
				"volume %d: Commandstatus is %d\n", 
				file, i, c.error_info.CommandStatus);
			everything_hunky_dory = 0;
			continue;
		} else {
			if (debug)
				fprintf(stderr, "Status for logical drive %d seems to be gettable.\n", i);
		}
		print_volume_status(file, ctlrtype, i, &ldstatus, &id, 
			cciss_to_bmic.addr[i].tolerance_type,
			cciss_to_bmic.addr[i].certain);
	}
	close(fd);
	return 0;
}

void intro()
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

struct option longopts[] = { 
	{ "persnickety", 0, NULL, 'p'},
	{ "quiet", 0, NULL, 'q'}, /* doesn't do anything anymore. */
	{ "try-unknown-devices", 0, NULL, 'u'},
	{ "version", 0, NULL, 'v'},
	{ "exhaustive", 0, NULL, 'x'},
	{ "copyright", 0, NULL, 'C'}, /* opposite of -q */
	{ NULL, 0, NULL, 0},
};

int main(int argc, char *argv[])
{
	int i, opt;

	do {

		opt = getopt_long(argc, argv, "dpquvxC", longopts, NULL );
		switch (opt) {
			case 'd': debug = 1;
				continue; 
			case 'p': persnickety = 1;
				continue;
			case 'q': be_quiet = 1; /* default now */
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

	for (i=optind;i<argc;i++)
		cciss_status(argv[i]);
	exit((everything_hunky_dory != 1));
}

