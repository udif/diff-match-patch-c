#include <stdio.h>
#ifdef WIN32
#include <mman.h>
#include <io.h>
#define  open  _open
#define  close _close
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <dmp.h>
#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "optparse.h"
#define DMP_MAX_FILE_SIZE 100000000

#define DMP_GFS_NO_FILE 1
#define DMP_GFS_FILE_TOO_LARGE 2
#define DMP_GFS_FILENAME_TOO_LONG 3
#define DMP_GFS_MMAP_FAILED 4
#define DMP_GFS_STAT_FAILED 5

static bool debug = false;

typedef enum {
  DMPDIFF_NON_WS = 0,
  DMPDIFF_WS = 1,
  DMPDIFF_NL = 2
} dmpdiff_ctype_t;

typedef enum {
  DFS_EQ,
  DFS_DEL_WS,
  DFS_DEL_NON_WS,
  DFS_DEL_NL,
  DFS_INS_WS,
  DFS_INS_NON_WS,
  DFS_INS_NL
} diff_st;

typedef struct {
  off_t len;
  int fd;
  char *m;
} diff_file;

typedef struct {
  int line; // line number
  dmp_options *opts;
  dmp_operation_t last_op;
  bool first; // true on 1st call
  bool nl; // true when last call ended on a newline
  diff_st st, last_st;
} cb_state;
static cb_state cb_s;

char *strnchr(const char *p, char c, size_t n)
{
	if (!p)
		return (0);

	while (n-- > 0) {
		if (*p == c)
			return ((char *)p);
		p++;
	}
	return (0);
}

void cleanup (diff_file *df)
{
  if (df->m) {
	  munmap(df->m, df->len);
	  df->m = 0;
	  df->len = 0;
  }
  if (df->fd > 0) {
	  close(df->fd);
	  df->fd = 0;
  }
}

// Yes, this is less efficient than isXXX()
// "Premature optimization is the root of all evil..." (D. Knuth, 1974)
static dmpdiff_ctype_t my_ctype(char c)
{
  return
    (c == '\n')             ? DMPDIFF_NL :
    (c == ' ' || c == '\t') ? DMPDIFF_WS :
                              DMPDIFF_NON_WS;
}

int get_file_size(diff_file *df, const char *name)
{
  struct stat st;

  if (strnlen(name, FILENAME_MAX) == FILENAME_MAX) {
    return DMP_GFS_FILENAME_TOO_LONG;
  }
  df->fd = open(name, O_RDONLY);
  if (df->fd < 0)
    return DMP_GFS_NO_FILE;
  if (fstat(df->fd, &st) < 0)
    return DMP_GFS_STAT_FAILED;
  df->len = st.st_size; // lseek(df->fd, (off_t)0, SEEK_END);
  if (df->len > DMP_MAX_FILE_SIZE) {
    return DMP_GFS_FILE_TOO_LARGE;
  };
  df->m = mmap (0, (size_t)df->len, PROT_READ, MAP_PRIVATE, df->fd, 0);
  if (df->m == MAP_FAILED) {
    printf("mmap failed: %s\n", "explain_mmap (0, (size_t)df->len, PROT_READ, 0, df->fd, 0)");
    return DMP_GFS_MMAP_FAILED;
  }
  return 0;
}

static int mark(char *p, diff_st *st, diff_st *last_st)
{
  int l = 0;
  if (*st == *last_st)
    return 0;
  //if (debug) l = sprintf(p, "<%c>", "ED-NIin"[*st-DFS_EQ]);
  *last_st = *st;
  p += l;
  switch (*st) {
    case DFS_EQ :
      strcpy(p, "\033[0m");
      return l + 4;

    case DFS_DEL_WS:
      strcpy(p, "\033[9;41m");
      return l + 8;

    case DFS_DEL_NON_WS:
      strcpy(p, "\033[9;49;31m");
      return l + 10;

    case DFS_DEL_NL:
      strcpy(p, "\033[9;41;39m\\n\033[0m\n");
      return l + 17;

    case DFS_INS_WS:
      strcpy(p, "\033[0;42m");
      return l + 7;

    case DFS_INS_NON_WS:
      strcpy(p, "\033[0;49;32m");
      return l + 10;

    case DFS_INS_NL:
      strcpy(p, "\033[42m\\n\033[0m\n");
      return l + 12;

    default:
      return l;
  }
}

static char buf[4096];
static char *p = &buf[0];

static void flush_buf(void)
{
  fwrite(buf, p - &buf[0], 1, stdout);
  p = &buf[0];
}

static void check_buf(void)
{
  if (p - &buf[0] > (uint32_t)(sizeof(buf)-100u))
    flush_buf();
}

static int cb(void *cb_ref, dmp_operation_t op, const void *data, uint32_t len)
{
  const char *l = data;
  const void *last_data = data+len;
  cb_state *cb_s = cb_ref;
  diff_st st;
  diff_st *last_st = &cb_s->last_st;
  int len2;

  if (debug) p += sprintf(p, "%c:%d\n", "DEI"[op-DMP_DIFF_DELETE] , len);
  switch(op) {
    case DMP_DIFF_EQUAL:
      st = DFS_EQ;
      p += mark(p, &st, last_st);
      do {
        // if already at beginning of line and either we display something anyhow, or we don't skip equal lines
        if (cb_s->nl) {
          cb_s->line++;
          for (len2 = 0; l[len2] != '\n' && (l+len2 != last_data); len2++);
          if (cb_s->opts->skip_equal_lines && l[len2] == '\n') {
            if (l+len2 == last_data)
              break;
            l += len2 + 1;
            cb_s->line++;
          }
          if (cb_s->opts->show_line_numbers)
            p += sprintf(p, "%d: ", cb_s->line);
          cb_s->nl = false;
        }
        if (*l == '\n')
          cb_s->nl = true;
        *p++ = *l++;
        check_buf();
      } while (l !=last_data);
      return 0;

    case DMP_DIFF_DELETE:
      do {
        // if already at beginning of line and either we display something anyhow, or we don't skip equal lines
        if (cb_s->nl) {
          st = DFS_EQ;
          cb_s->line++;
          p += mark(p, &st, last_st);
          if (cb_s->opts->show_line_numbers)
            p += sprintf(p, "%d: ", cb_s->line);
          cb_s->nl = false;
        }
        switch(my_ctype(*l)) {
          case DMPDIFF_WS:
            st = DFS_DEL_WS;
            break;
          case DMPDIFF_NON_WS:
            st = DFS_DEL_NON_WS;
            break;
          case DMPDIFF_NL:
            st = DFS_DEL_NL;
            break;
        }
        p += mark(p, &st, last_st);
        if (*l != '\n')
          *p++ = *l++;
        else {
          l++;
          cb_s->nl = true;
        }
        check_buf();
      } while (l != data + len);
      return 0;

    case DMP_DIFF_INSERT:
      do {
        // if already at beginning of line and either we display something anyhow, or we don't skip equal lines
        if (cb_s->nl) {
          st = DFS_EQ;
          cb_s->line++;
          p += mark(p, &st, last_st);
          if (cb_s->opts->show_line_numbers)
            p += sprintf(p, "%d: ", cb_s->line);
          cb_s->nl = false;
        }
        switch(my_ctype(*l)) {
          case DMPDIFF_WS:
            st = DFS_INS_WS;
            break;
          case DMPDIFF_NON_WS:
            st = DFS_INS_NON_WS;
            break;
          case DMPDIFF_NL:
            st = DFS_INS_NL;
            break;
        }
        p += mark(p, &st, last_st);
        if (*l != '\n')
          *p++ = *l++;
        else {
          l++;
          cb_s->nl = true;
        }
        check_buf();
      } while (l != data + len);
  }
  return 0;
}

int main(int argc, char *argv[])
{
  diff_file f1, f2;
  int r1, r2;
  dmp_diff *diff;
  dmp_options opts;
  int option;
  struct optparse options;
  struct optparse_long longopts[] = {
      {"debug",              'd', OPTPARSE_NONE},
      {"ignore-whitespace",  'w', OPTPARSE_NONE},
      {"skip-equal-lines",   's', OPTPARSE_NONE},
      {"show-line-numbers",  'l', OPTPARSE_NONE},
      {"merge_window", 'm', OPTPARSE_REQUIRED},
      {0}
  };

  /*
   * Sanity checking
   */

  dmp_options_init(&opts);
  opts.merge_window = 0;
  opts.ignore_whitespace = false;
  opts.skip_equal_lines = false;
  opts.show_line_numbers = false;
  optparse_init(&options, argv);
    while ((option = optparse_long(&options, longopts, NULL)) != -1) {
      switch (option) {
      case 'd':
          debug = true;
          break;
      case 'w':
          opts.ignore_whitespace = true;
          break;
      case 's':
          opts.skip_equal_lines = true;
          break;
      case 'l':
          opts.show_line_numbers = true;
          break;
      case 'm':
          opts.merge_window = atoi(options.optarg);
          break;
      case '?':
          fprintf(stderr, "%s: %s\n", argv[0], options.errmsg);
          exit(EXIT_FAILURE);
      }
  }

  /* Print remaining arguments. */
  //while ((arg = optparse_arg(&options)))
  //    printf("%s\n", arg);
  if (options.optind + 2 != argc) {
    printf("Usage %s file1 file2\n", argv[0]);
    exit(64);
  }
  if ((r1 = get_file_size(&f1, optparse_arg(&options))) ||
      (r2 = get_file_size(&f2, optparse_arg(&options)))) {
    printf("r1=%d r2=%d!\n", r1, r2);
    exit(66);
  }
  opts.timeout = 5.0F;
  dmp_diff_new(&diff, &opts, f1.m, (uint32_t)f1.len, f2.m, (uint32_t)f2.len);
  //dmp_diff_print_raw(stdout, diff);
  cb_s.first = true;
  cb_s.line = 0;
  cb_s.opts = &opts;
  cb_s.st = DFS_EQ;
  cb_s.last_st = DFS_EQ;
  cb_s.nl = true;
  dmp_diff_foreach(diff, cb, &cb_s);
  flush_buf();
  dmp_diff_free(diff);
  cleanup(&f1);
  cleanup(&f2);
}

