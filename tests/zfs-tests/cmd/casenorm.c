#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static char name_c_orig[] = {
	0x46, 0xc3, 0xaf, 0x4c, 0xc3, 0xab, 0x4e, 0xc3,
	0x84, 0x6d, 0xc3, 0xab, 0x00,
};
static char name_c_upper[] = {
	0x46, 0xc3, 0x8f, 0x4c, 0xc3, 0x8b, 0x4e, 0xc3,
	0x84, 0x4d, 0xc3, 0x8b, 0x00,
};
static char name_c_lower[] = {
	0x66, 0xc3, 0xaf, 0x6c, 0xc3, 0xab, 0x6e, 0xc3,
	0xa4, 0x6d, 0xc3, 0xab, 0x00
};

static char name_d_orig[] = {
	0x46, 0x69, 0xcc, 0x88, 0x4c, 0x65, 0xcc, 0x88,
	0x4e, 0x41, 0xcc, 0x88, 0x6d, 0x65, 0xcc, 0x88,
	0x00,
};
static char name_d_upper[] = {
	0x46, 0x49, 0xcc, 0x88, 0x4c, 0x45, 0xcc, 0x88,
	0x4e, 0x41, 0xcc, 0x88, 0x4d, 0x45, 0xcc, 0x88,
	0x00,
};
static char name_d_lower[] = {
	0x66, 0x69, 0xcc, 0x88, 0x6c, 0x65, 0xcc, 0x88,
	0x6e, 0x61, 0xcc, 0x88, 0x6d, 0x65, 0xcc, 0x88,
	0x00,
};

static void
report_ok(const char *op, char form, char case_, const char *filename)
{
	fprintf(stderr, "OK: %s [form=%c case=%c] filename=%s\n",
	    op, form, case_, filename);
}

static void
report_err(const char *op, char form, char case_, const char *filename, int err)
{
	fprintf(stderr, "ERR: %s [form=%c case=%c] filename=%s: [%d] %s\n",
	    op, form, case_, filename, err, strerror(err));
}

int
main(int argc, char **argv)
{
	if (argc < 4) {
		printf(
		    "usage: casenorm <op> <form+case> [dir]\n"
		    "    op: [c]reate, [l]ookup, [d]elete\n"
		    "  form: c, d\n"
		    "  case: [o]rig, [u]pper, [l]ower\n"
		    "   dir: dir to operate on\n");
		exit(1);
	}

	if (chdir(argv[3]) < 0) {
		perror("chdir");
		exit(1);
	}

	char op = argv[1][0];
	char form = argv[2][0];
	char *casep = strchr(argv[2], '+');
	if (casep == NULL)
		casep = (char *) "o";
	else
		casep++;

	char *filename = NULL;
	switch (form) {
	case 'c': case 'C': {
		switch (*casep) {
		case 'o':
			filename = name_c_orig;
			break;
		case 'u':
			filename = name_c_upper;
			break;
		case 'l':
			filename = name_c_lower;
			break;
		}
		break;
	}
	case 'd': case 'D': {
		switch (*casep) {
		case 'o':
			filename = name_d_orig;
			break;
		case 'u':
			filename = name_d_upper;
			break;
		case 'l':
			filename = name_d_lower;
			break;
		}
		break;
	}
	}

	if (filename == NULL) {
		fprintf(stderr, "invalid form or case\n");
		exit(1);
	}

	int ret = 0;
	switch(op) {
	case 'c': {
		ret = open(filename, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
		if (ret < 0) {
			report_err("create[open]",
			    form, *casep, filename, errno);
		} else {
			close(ret);
			ret = 0;
			report_ok("create", form, *casep, filename);
		}
		break;
	}
	case 'l': {
		struct stat st;
		ret = stat(filename, &st);
		if (ret < 0) {
			report_err("lookup[stat]",
			    form, *casep, filename, errno);
		} else {
			report_ok("lookup", form, *casep, filename);
		}
		break;
	}
	case 'd': {
		ret = unlink(filename);
		if (ret < 0) {
			report_err("delete[unlink]",
			    form, *casep, filename, errno);
		} else {
			report_ok("delete", form, *casep, filename);
		}
		break;
	}
	default:
		fprintf(stderr, "invalid op\n");
		exit(1);
	}

	return (ret ? 2 : 0);
}
