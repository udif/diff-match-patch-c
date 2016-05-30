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

#define DMP_MAX_FILE_SIZE 100000000

#define DMP_GFS_NO_FILE 1
#define DMP_GFS_FILE_TOO_LARGE 2
#define DMP_GFS_FILENAME_TOO_LONG 3
#define DMP_GFS_MMAP_FAILED 4
#define DMP_GFS_STAT_FAILED 5

//static const bool debug = true;
static const bool debug = false;

typedef enum {
  DMPDIFF_NON_WS = 0,
  DMPDIFF_WS = 1,
  DMPDIFF_NL = 2
} dmpdiff_ctype_t;

typedef struct {
  off_t len;
  int fd;
  char *m;
} diff_file;

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

static void mark(dmp_operation_t op, dmpdiff_ctype_t ctype)
{
  if (debug) printf("op:%d ctype:%d\n", op, ctype);
  switch (ctype) {
    case DMPDIFF_NON_WS :
      switch(op) {
        case DMP_DIFF_INSERT:
          fwrite("\033[32m", 5, 1, stdout);
          break;
      
        case DMP_DIFF_DELETE:
          fwrite("\033[9;31m", 7, 1, stdout);
          break;
    
        default:
          break;
      }
      break;

    case DMPDIFF_WS :
      switch(op) {
        case DMP_DIFF_INSERT:
          fwrite("\033[42m", 5, 1, stdout);
          break;
      
        case DMP_DIFF_DELETE:
          fwrite("\033[9;41m", 8, 1, stdout);
          break;
    
        default:
          break;
      }
      break;

    case DMPDIFF_NL :
      switch(op) {
        case DMP_DIFF_INSERT:
          fwrite("\033[42m\\n\033[0m\n", 12, 1, stdout);
          break;
    
        case DMP_DIFF_DELETE:
          fwrite("\033[41m\\n\033[0m\n", 12, 1, stdout);
          break;
      
        default:
          break;
      }
      break;
  }
}

static void unmark(void)
{
  if (debug) printf("-");
  fwrite("\033[0m", 4, 1, stdout);
}

static int cb(void *cb_ref, dmp_operation_t op, const void *data, uint32_t len)
{
  uint32_t i, j;
  uint32_t last_i;
  dmpdiff_ctype_t last_ct, ct;

  if (debug) printf("CB:%c %d\n", 'b' + op, len);
  if (op) {
    // first character MUST have a mark
    last_ct = my_ctype(((char *)data)[0]);
    if (debug) fwrite("!", 1, 1, stdout);
    for (last_i = 0, i = 1; i <= len; i++) {
      ct = my_ctype(((char *)data)[i]);
      if (debug) fwrite("+", 1, 1, stdout);
      // if we just completed this insert/delete segment
      // or we just changed mode from white-space to non-white-space
      // and vice versa
      if ((i == len) || (last_ct != ct)) {
        if (debug) printf("*");
        if (last_ct == DMPDIFF_NL) {
          for (j = last_i; j < i; j++) {
            mark(op, last_ct);
          }
        } else {
          mark(op, last_ct);
          fwrite((char *)data + last_i, i - last_i, 1, stdout);
          unmark();
        }
        last_i = i;
        last_ct = ct;
//      } else if (((i == len) || (ct == '\n')) && (last_ct == '\n')) {
//        if (debug) printf("?");
//        mark(op, ct);
//        fwrite((char *)data + last_i, i - last_i - 1, 1, stdout);
//        unmark();
//        mark_cr(op);
//        last_i = i;
      }
    }
    // do also last fragment
//    mark(op, ws);
//    fwrite((char *)data + last_i, i - last_i, 1, stdout);
//    unmark();
  } else {
    fwrite(data, len, 1, stdout);
  }
  return 0;
}

int main(int argc, char *argv[])
{
  diff_file f1, f2;
  int r1, r2;
  dmp_diff *diff;
  dmp_options opts;
  /*
   * Sanity checking
   */
  printf("%d %d %d\n", isspace(' '), isspace('\t'), isspace('\n'));
  if (argc != 3) {
    printf("Usage %s file1 file2\n", argv[0]);
    exit(64);
  }
  if ((r1 = get_file_size(&f1, argv[1])) || (r2 = get_file_size(&f2, argv[2]))) {
    printf("r1=%d r2=%d!\n", r1, r2);
    exit(66);
  }
  dmp_options_init(&opts);
  opts.timeout = 5.0F;
  dmp_diff_new(&diff, &opts, f1.m, (uint32_t)f1.len, f2.m, (uint32_t)f2.len);
  //dmp_diff_print_raw(stdout, diff);
  dmp_diff_foreach(diff, cb, 0);
  dmp_diff_free(diff);
  cleanup(&f1);
  cleanup(&f2);
}

