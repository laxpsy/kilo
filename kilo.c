// include section
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>

// define section
#define CTRL_KEY(x) ((x) & (0x1f))
#define KILO_VERSION "0.01"

// terminal section

struct editorConfig
{
  int cx, cy;
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
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
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
  if (c == CTRL_KEY('q'))
  {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
  }
}

// append buffer
struct abuf
{
  char *b;
  int len;
};

void abAppend(struct abuf *ab, const char *s, int len)
{
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab)
{
  free(ab->b);
}

#define ABUF_INIT {NULL, 0};

// output section

void editorDrawRows(struct abuf *ab)
{
  for (int y = 0; y < E.screenRows; y++)
  {
    if (y == E.screenRows / 2)
    {
      char welcome[80];
      int welcome_len = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
      if (welcome_len > E.screenCols)
        welcome_len = E.screenCols;
      int padding = (E.screenCols - welcome_len) / 2;
      if (padding)
      {
        abAppend(ab, "~", 1);
      }
      while (padding--)
      {
        abAppend(ab, " ", 1);
      }
      abAppend(ab, strcat(welcome, "\x1b[K\r\n"), welcome_len + 5);
    }
    else if (y != E.screenRows - 1)
      abAppend(ab, "~\x1b[K\r\n", 6);
    else
      abAppend(ab, "~\x1b[K", 4);
  }
}

void editorRefreshScreen()
{
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  char buff[32];
  int buff_len = snprintf(buff, sizeof(buff), "\x1b[%d;%dH", E.cx + 1, E.cy + 1);
  abAppend(&ab, buff, buff_len);
  abAppend(&ab, "\x1b[?25h", 6);

  if (write(STDOUT_FILENO, ab.b, ab.len) != ab.len)
  {
    die("editorRefreshScreen::write()");
  }
  abFree(&ab);
}

// main section

void initEditor()
{
  E.cx = 0;
  E.cy = 0;
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