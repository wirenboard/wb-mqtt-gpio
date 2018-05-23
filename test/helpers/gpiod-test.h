#include "stdlib.h"

#define TEST_PRINTF(fmt, arg)	__attribute__((format(printf, fmt, arg)))
#define NORETURN	__attribute__((noreturn))
#define MALLOC		__attribute__((malloc))

struct mockup_chip {
	char *path;
	char *name;
	unsigned int number;
};

struct _test_chip_descr {
	unsigned int num_chips;
	unsigned int *num_lines;
	int flags;
};

struct gpiotool_proc {
	pid_t pid;
	bool running;
	int stdin_fd;
	int stdout_fd;
	int stderr_fd;
	char *stdout;
	char *stderr;
	int sig_fd;
	int exit_status;
};


struct test_context {
	struct mockup_chip **chips;
	size_t num_chips;
	bool test_failed;
	char *failed_msg;
	char *custom_str;
	//struct event_thread event;
	struct gpiotool_proc tool_proc;
	bool running;
};

static struct {
	struct _test_case *test_list_head;
	struct _test_case *test_list_tail;
	unsigned int num_tests;
	unsigned int tests_failed;
	struct kmod_ctx *module_ctx;
	struct kmod_module *module;
	struct test_context test_ctx;
	pid_t main_pid;
	int pipesize;
	char *pipebuf;
} globals;

void check_kernel(void);
void check_gpio_mockup(void);

TEST_PRINTF(1, 2) void msg(const char *fmt, ...);
static TEST_PRINTF(1, 2) NORETURN void die(const char *fmt, ...);
static TEST_PRINTF(1, 2) NORETURN void die_perr(const char *fmt, ...);
