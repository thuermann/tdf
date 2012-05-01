/*
 *  $Id: tdf.c,v 1.3 2012/05/01 06:04:27 urs Exp $
 *
 *  A text file differencer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXLINE 100

#define SIZE (10 * 1024L)

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

void diff(char *oldfilename, char *newfilename);
void resync(LINE *first, LINE *second);
int match(LINE *a, LINE *b);
int equal(LINE *a, LINE *b);
void discard(FD *file, LINE *line);
LINE *nextline(FD *file);
LINE *getline(FD *file);
void report(LINE *del, LINE *add);
void deleted(LINE *line);
void added(LINE *line);

FD oldfile = { 0, NULL, NULL, NULL };
FD newfile = { 0, NULL, NULL, NULL };

int no_blanks  = 0;
int no_case    = 0;
int sed_script = 0;

int re_sync    = 2;
int lookahead  = 200;

int main(int argc, char **argv)
{
	char *oldfilename = NULL, *newfilename = NULL;

	while (++argv, --argc) {
		if (**argv == '-')
			switch (tolower(*++*argv)) {
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
				re_sync = atoi(*argv + 1);
				break;
			case 'l':
				lookahead = atoi(*argv + 1);
				break;
			default:
				break;
			}
		else if (newfilename) {
			fprintf(stderr, "Usage: diff [-b -i -e -rnum] oldfile newfile\n");
			exit(1);
		} else if (oldfilename)
			newfilename = *argv;
		else
			oldfilename = *argv;
	}

	diff(oldfilename, newfilename);

	return 0;
}

void diff(char *oldfilename, char *newfilename)
{
	LINE *first, *second;

	if (!(oldfile.fp = fopen(oldfilename, "r"))) {
		fprintf(stderr, "Can't open %s\n", oldfilename);
		exit(1);
	}
	if (!(newfile.fp = fopen(newfilename, "r"))) {
		fprintf(stderr, "Can't open %s\n", newfilename);
		exit(1);
	}
	setvbuf(oldfile.fp, NULL, _IOFBF, SIZE);
	setvbuf(newfile.fp, NULL, _IOFBF, SIZE);

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

void resync(LINE *first, LINE *second)
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

int match(LINE *a, LINE *b)
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

int equal(LINE *a, LINE *b)
{
	if (a == NULL || b == NULL)
		return 0;
	if (no_case)
		return !stricmp(a->text, b->text);
	else
		return !strcmp(a->text, b->text);
}

void discard(FD *file, LINE *line)
{
	LINE *temp, *next;

	for (temp = file->root; temp != line; temp = next) {
		next = temp->next;
		free(temp);
		file->line_count++;
	}
	file->root = file->at = temp;
}

LINE *nextline(FD *file)
{
	if (file->at) {
		if (!file->at->next)
			file->at->next = getline(file);
		return file->at = file->at->next;
	} else if (!file->root) {
		file->root = file->at = getline(file);
		return file->at;
	} else
		return NULL;
}

LINE *getline(FD *file)
{
	LINE *line;

	if (!(line = malloc(sizeof(LINE)))) {
		fprintf(stderr, "Out of memory.\n");
		exit(2);
	}
	if (!fgets(line->text, MAXLINE, file->fp)) {
		free(line);
		return NULL;
	}
	line->next = NULL;

	return line;
}

void report(LINE *del, LINE *add)
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

void deleted(LINE *line)
{
	LINE *temp;

	for (temp = oldfile.root; temp != line; temp = temp->next)
		printf("< %s", temp->text);
}

void added(LINE *line)
{
	LINE *temp;

	for (temp = newfile.root; temp != line; temp = temp->next)
		printf("> %s", temp->text);
}
