
#include "gpiod-test.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <libgen.h>
#include <libkmod.h>
#include <libudev.h>
#include <pthread.h>
#include <regex.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signalfd.h>

static const unsigned int min_kern_major = 4;
static const unsigned int min_kern_minor = 13;
static const unsigned int min_kern_release = 0;


static bool devpath_is_mockup(const char *devpath);
static int chipcmp(const void *c1, const void *c2);

enum {
	CNORM = 0,
	CGREEN,
	CRED,
	CREDBOLD,
	CYELLOW,
};

static const char *const term_colors[] = {
	"\033[0m",
	"\033[32m",
	"\033[31m",
	"\033[1m\033[31m",
	"\033[33m",
};

static void set_color(int color)
{
	fprintf(stderr, "%s", term_colors[color]);
}

static void reset_color(void)
{
	fprintf(stderr, "%s", term_colors[CNORM]);
}

static void pr_raw_v(const char *fmt, va_list va)
{
	vfprintf(stderr, fmt, va);
}

static TEST_PRINTF(1, 2) void pr_raw(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	pr_raw_v(fmt, va);
	va_end(va);
}

static void print_header(const char *hdr, int color)
{
	char buf[9];

	snprintf(buf, sizeof(buf), "[%s]", hdr);

	set_color(color);
	pr_raw("%-8s", buf);
	reset_color();
}

static void vmsgn(const char *hdr, int hdr_clr, const char *fmt, va_list va)
{
	print_header(hdr, hdr_clr);
	pr_raw_v(fmt, va);
}

static void vmsg(const char *hdr, int hdr_clr, const char *fmt, va_list va)
{
	vmsgn(hdr, hdr_clr, fmt, va);
	pr_raw("\n");
}

TEST_PRINTF(1, 2) void msg(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vmsg("INFO", CGREEN, fmt, va);
	va_end(va);
}

static TEST_PRINTF(1, 2) void err(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vmsg("ERROR", CRED, fmt, va);
	va_end(va);
}

TEST_PRINTF(1, 2) NORETURN void die(const char *fmt, ...)
{
	va_list va;

	//die_test_cleanup();

	va_start(va, fmt);
	vmsg("FATAL", CRED, fmt, va);
	va_end(va);

	exit(EXIT_FAILURE);
}

static MALLOC void *xmalloc(size_t size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		die("out of memory");

	return ptr;
}

static MALLOC void *xzalloc(size_t size)
{
	void *ptr;

	ptr = xmalloc(size);
	memset(ptr, 0, size);

	return ptr;
}

static MALLOC void *xcalloc(size_t nmemb, size_t size)
{
	void *ptr;

	ptr = calloc(nmemb, size);
	if (!ptr)
		die("out of memory");

	return ptr;
}

static MALLOC char *xstrdup(const char *str)
{
	char *ret;

	ret = strdup(str);
	if (!ret)
		die("out of memory");

	return ret;
}


static TEST_PRINTF(1, 2) NORETURN void die_perr(const char *fmt, ...)
{
	va_list va;

	//die_test_cleanup();

	va_start(va, fmt);
	vmsgn("FATAL", CRED, fmt, va);
	pr_raw(": %s\n", strerror(errno));
	va_end(va);

	exit(EXIT_FAILURE);
}


void check_kernel(void)
{
	unsigned int major, minor, release;
	struct utsname un;
	int rv;

	msg("checking the linux kernel version");

	rv = uname(&un);
	if (rv)
		die_perr("uname");

	rv = sscanf(un.release, "%u.%u.%u", &major, &minor, &release);
	if (rv != 3)
		die("error reading kernel release version");

	if (major < min_kern_major) {
		goto bad_version;
	} else if (major > min_kern_major) {
		goto good_version;
	} else {
		if (minor < min_kern_minor) {
			goto bad_version;
		} else if (minor > min_kern_minor) {
			goto good_version;
		} else {
			if (release < min_kern_release)
				goto bad_version;
			else
				goto good_version;
		}
	}

good_version:
	msg("kernel release is v%u.%u.%u - ok to run tests",
	    major, minor, release);

	return;

bad_version:
	die("linux kernel version must be at least v%u.%u.%u - got v%u.%u.%u",
	    min_kern_major, min_kern_minor, min_kern_release,
	    major, minor, release);
}


void check_gpio_mockup(void)
{
	const char *modpath;
	int status;

	msg("checking gpio-mockup availability");

	globals.module_ctx = kmod_new(NULL, NULL);
	if (!globals.module_ctx)
		die_perr("error creating kernel module context");

	status = kmod_module_new_from_name(globals.module_ctx,
					   "gpio-mockup", &globals.module);
	if (status)
		die_perr("error allocating module info");

	// First see if we can find the module. 
	modpath = kmod_module_get_path(globals.module);
	if (!modpath)
		die("the gpio-mockup module does not exist in the system or is built into the kernel");

	// Then see if we can freely load and unload it. 
	status = kmod_module_probe_insert_module(globals.module, 0,
						 "gpio_mockup_ranges=-1,4",
						 NULL, NULL, NULL);
	if (status)
		die_perr("unable to load gpio-mockup");

	status = kmod_module_remove_module(globals.module, 0);
	if (status)
		die_perr("unable to remove gpio-mockup");

	msg("gpio-mockup ok");
}


static void prepare_test(struct _test_chip_descr *descr)
{
	const char *devpath, *devnode, *sysname;
	struct udev_monitor *monitor;
	unsigned int detected = 0;
	struct test_context *ctx;
	struct mockup_chip *chip;
	struct udev_device *dev;
	struct udev *udev_ctx;
	struct pollfd pfd;
	int status;

	ctx = &globals.test_ctx;
	memset(ctx, 0, sizeof(*ctx));
	ctx->num_chips = descr->num_chips;
	ctx->chips = xcalloc(ctx->num_chips, sizeof(*ctx->chips));
	//pthread_mutex_init(&ctx->event.lock, NULL);
	//pthread_cond_init(&ctx->event.cond, NULL);

	/*
	 * We'll setup the udev monitor, insert the module and wait for the
	 * mockup gpiochips to appear.
	 */

	udev_ctx = udev_new();
	if (!udev_ctx)
		die_perr("error creating udev context");

	monitor = udev_monitor_new_from_netlink(udev_ctx, "udev");
	if (!monitor)
		die_perr("error creating udev monitor");

	status = udev_monitor_filter_add_match_subsystem_devtype(monitor,
								 "gpio", NULL);
	if (status < 0)
		die_perr("error adding udev filters");

	status = udev_monitor_enable_receiving(monitor);
	if (status < 0)
		die_perr("error enabling udev event receiving");

	load_module(descr);

	pfd.fd = udev_monitor_get_fd(monitor);
	pfd.events = POLLIN | POLLPRI;

	while (ctx->num_chips > detected) {
		status = poll(&pfd, 1, 5000);
		if (status < 0)
			die_perr("error polling for uevents");
		else if (status == 0)
			die("timeout waiting for gpiochips");

		dev = udev_monitor_receive_device(monitor);
		if (!dev)
			die_perr("error reading device info");

		devpath = udev_device_get_devpath(dev);
		devnode = udev_device_get_devnode(dev);
		sysname = udev_device_get_sysname(dev);

		if (!devpath || !devnode || !sysname ||
		    !devpath_is_mockup(devpath)) {
			udev_device_unref(dev);
			continue;
		}

		chip = xzalloc(sizeof(*chip));
		chip->name = xstrdup(sysname);
		chip->path = xstrdup(devnode);
		status = sscanf(sysname, "gpiochip%u", &chip->number);
		if (status != 1)
			die("unable to determine chip number");

		ctx->chips[detected++] = chip;
		udev_device_unref(dev);
	}

	udev_monitor_unref(monitor);
	udev_unref(udev_ctx);

	/*
	 * We can't assume that the order in which the mockup gpiochip
	 * devices are created will be deterministic, yet we want the
	 * index passed to the test_chip_*() functions to correspond with the
	 * order in which the chips were defined in the TEST_DEFINE()
	 * macro.
	 *
	 * Once all gpiochips are there, sort them by chip number.
	 */
	qsort(ctx->chips, ctx->num_chips, sizeof(*ctx->chips), chipcmp);
}


static bool devpath_is_mockup(const char *devpath)
{
	static const char mockup_devpath[] = "/devices/platform/gpio-mockup";

	return !strncmp(devpath, mockup_devpath, sizeof(mockup_devpath) - 1);
}

static int chipcmp(const void *c1, const void *c2)
{
	const struct mockup_chip *chip1 = *(const struct mockup_chip **)c1;
	const struct mockup_chip *chip2 = *(const struct mockup_chip **)c2;

	return chip1->number > chip2->number;
}