#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path) {
  static char buf[DIRSIZ + 1];
  char *p;

  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if (strlen(p) >= DIRSIZ) return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), '\0', DIRSIZ - strlen(p));
  //buf[strlen(buf) + strlen(p)]='\0';
  return buf;
}

void find(char *path, char *target) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }


  char *filename;
  filename = fmtname(path);
  // printf("%s\n", path);
  // printf("%s\n", filename);
  // printf("%s\n", target);
  //printf("%d\n",strcmp(filename, target));
  if (strcmp(filename, target) == 0) // WHY32?
        printf("%s\n", path); 
        
  switch (st.type) {

    case T_FILE:
      close(fd);
      break; 

    case T_DIR:
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("find: path too long\n");
        break;
      }
      memset(buf, 0, sizeof(buf));
      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0||strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
          continue;
        memcpy(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

      find(buf, target);
      }
      break; 
  }

  close(fd);
}



int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(2, "format: find <path> <name>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}



