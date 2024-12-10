// include section
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#define CTRL_KEY(x) ((x) & (0x1f))

// terminal section

struct editorConfig
{
  int screenRows;
  int screenCols;
  struct termios orig_termios;
};

struct editorConfig E;

void die(const char *s)
{
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode()
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
  {
    die("disableRawMode::tcsetattr()");
  }
}

void enableRawMode()
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
  {
    die("enableRawMode::tcgetattr()");
  }
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 10;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
  {
    die("enableRawMode::tcsetattr()");
  }
  atexit(disableRawMode);
}

char editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {
    if (nread == -1)
      die("editorReadKey::read()");
  }
  return c;
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1)
  {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  printf("\r\n&buf[1]:'%s'\r\n", &buf[1]);
  editorReadKey();
  return -1;
}

int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
  {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  }
  else
  {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

// input section

void editorProcessKey()
{
  char c = editorReadKey();
  if (iscntrl(c))
  {
    printf("ASCII Value: %d\r\n", c);
  }
  else
  {
    printf("ASCII Value: %d, Character: %c\r\n", c, c);
  }
  if (c == CTRL_KEY('q'))
  {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
  }
}

// output section

void editorDrawRows()
{
  for (int y = 0; y < E.screenRows; y++)
  {
    write(STDOUT_FILENO, "~\r\n", 3);
  }
}

void editorRefreshScreen()
{
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3);
}

// main section

void initEditor()
{
  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("initEditor::getWindowSize()");
}

int main()
{
  enableRawMode();
  initEditor();
  while (1)
  {
    editorRefreshScreen();
    editorProcessKey();
  }
  return 0;
}