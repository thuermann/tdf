/*
 *  $Id: tdf.c,v 1.10 2014/09/18 13:32:01 urs Exp $
 *
 *  A text file differencer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [-bie] [-r num] file1 file2\n", name);
}

#define MAXLINE 100

typedef struct line LINE;
struct line {
	LINE *next;
	char text[MAXLINE];
};

typedef struct {
	unsigned line_count;
	LINE *root;
	LINE *at;
	FILE *fp;
} FD;

static void diff(const char *oldfilename, const char *newfilename);
static void resync(LINE *first, LINE *second);
static int match(LINE *a, LINE *b);
static int equal(const LINE *a, const LINE *b);
static void discard(FD *file, const LINE *line);
static LINE *nextline(FD *file);
static LINE *fgetline(FD *file);
static void report(const LINE *del, const LINE *add);
static void deleted(const LINE *line);
static void added(const LINE *line);

static FD oldfile = { 0, NULL, NULL, NULL };
static FD newfile = { 0, NULL, NULL, NULL };

static int no_blanks  = 0;
static int no_case    = 0;
static int sed_script = 0;

static int re_sync    = 2;
static int lookahead  = 200;

int main(int argc, char **argv)
{
	char *oldfilename = NULL, *newfilename = NULL;
	int errflag = 0;
	int opt;

	while ((opt = getopt(argc, argv, "bier:l:")) != -1) {
		switch (opt) {
		case 'b':
			no_blanks = 1;
			break;
		case 'i':
			no_case = 1;
			break;
		case 'e':
			sed_script = 1;
			break;
		case 'r':
			re_sync = atoi(optarg);
			break;
		case 'l':
			lookahead = atoi(optarg);
			break;
		default:
			errflag = 1;
			break;
		}
	}
	if (errflag || argc - optind != 2) {
		usage(argv[0]);
		exit(1);
	}

	oldfilename = argv[optind++];
	newfilename = argv[optind++];

	diff(oldfilename, newfilename);

	return 0;
}

static void diff(const char *oldfilename, const char *newfilename)
{
	LINE *first, *second;

	if (!(oldfile.fp = fopen(oldfilename, "r"))) {
		perror(oldfilename);
		exit(1);
	}
	if (!(newfile.fp = fopen(newfilename, "r"))) {
		perror(newfilename);
		exit(1);
	}

	while (1) {
		first  = nextline(&oldfile);
		second = nextline(&newfile);

		discard(&oldfile, first);
		discard(&newfile, second);

		if (!first || !second) {
			report(NULL, NULL);
			break;
		}

		if (!equal(first, second))
			resync(first, second);
	}
}

static void resync(LINE *first, LINE *second)
{
	LINE *file1, *file2, *ahead1, *ahead2;
	int i, j;

	ahead1 = first;
	ahead2 = second;

	for (i = 0; i < lookahead; i++) {
		if (!ahead1 && !ahead2)
			goto eof;

		oldfile.at = first;
		newfile.at = second;

		for (j = 0; j < i; j++) {
			file1 = oldfile.at;
			file2 = ahead2;
			if (match(file1, file2))
				goto synced;
			file1 = ahead1;
			file2 = newfile.at;
			if (match(file1, file2))
				goto synced;

			nextline(&oldfile);
			nextline(&newfile);
		}
		file1 = oldfile.at;
		file2 = newfile.at;
		if (match(file1, file2))
			goto synced;

		ahead1 = nextline(&oldfile);
		ahead2 = nextline(&newfile);
	}
	fprintf(stderr, "Error: lost sync.\n");
	exit(1);

eof:
	file1 = ahead1;
	file2 = ahead2;

synced:
	report(file1, file2);

	oldfile.at = file1;
	newfile.at = file2;
}

static int match(LINE *a, LINE *b)
{
	LINE *x, *y;
	int i, ret = 1;

	x = oldfile.at;
	y = newfile.at;

	oldfile.at = a;
	newfile.at = b;

	for (i = 0; i < re_sync; i++) {
		if (!equal(a, b)) {
			ret = 0;
			break;
		}
		a = nextline(&oldfile);
		b = nextline(&newfile);
	}

	oldfile.at = x;
	newfile.at = y;

	return ret;
}

static int equal(const LINE *a, const LINE *b)
{
	if (a == NULL || b == NULL)
		return 0;
	if (no_case)
		return !strcasecmp(a->text, b->text);
	else
		return !strcmp(a->text, b->text);
}

static void discard(FD *file, const LINE *line)
{
	LINE *temp, *next;

	for (temp = file->root; temp != line; temp = next) {
		next = temp->next;
		free(temp);
		file->line_count++;
	}
	file->root = file->at = temp;
}

static LINE *nextline(FD *file)
{
	if (file->at) {
		if (!file->at->next)
			file->at->next = fgetline(file);
		return file->at = file->at->next;
	} else if (!file->root) {
		file->root = file->at = fgetline(file);
		return file->at;
	} else
		return NULL;
}

static LINE *fgetline(FD *file)
{
	LINE *line;

	if (!(line = malloc(sizeof(LINE)))) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	if (!fgets(line->text, MAXLINE, file->fp)) {
		free(line);
		return NULL;
	}
	line->next = NULL;

	return line;
}

static void report(const LINE *del, const LINE *add)
{
	int ndel = 0, nadd = 0;

	for (oldfile.at = oldfile.root; oldfile.at != del; nextline(&oldfile))
		ndel++;
	for (newfile.at = newfile.root; newfile.at != add; nextline(&newfile))
		nadd++;

	if (ndel != 0 && nadd != 0)
		printf("%u,%uc%u,%u\n",
		       oldfile.line_count + 1, oldfile.line_count + ndel,
		       newfile.line_count + 1, newfile.line_count + nadd);
	else if (ndel != 0 && nadd == 0)
		printf("%u,%ud%u\n",
		       oldfile.line_count + 1, oldfile.line_count + ndel,
		       newfile.line_count);
	else if (ndel == 0 && nadd != 0)
		printf("%ua%u,%u\n",
		       oldfile.line_count,
		       newfile.line_count + 1, newfile.line_count + nadd);

	deleted(del);
	if (ndel != 0 && nadd != 0)
		printf("---\n");
	added(add);
}

static void deleted(const LINE *line)
{
	LINE *temp;

	for (temp = oldfile.root; temp != line; temp = temp->next)
		printf("< %s", temp->text);
}

static void added(const LINE *line)
{
	LINE *temp;

	for (temp = newfile.root; temp != line; temp = temp->next)
		printf("> %s", temp->text);
}
