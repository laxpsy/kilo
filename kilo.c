// needed
#define _POSIX_C_SOURCE 200810L

// include section
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdio.h>

// define section
#define CTRL_KEY(x) ((x) & (0x1f))
#define KILO_VERSION "0.01"

enum editorKey
{
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DELETE,
  HOME,
  END,
  PAGE_UP,
  PAGE_DOWN,
};

// data section

typedef struct erow
{
  int size;
  char *chars;
} erow;
struct editorConfig
{
  int cx, cy;
  int rowOff;
  int colOff;
  int screenRows;
  int screenCols;
  int numRows;
  erow *row;
  struct termios orig_termios;
};

struct editorConfig E;

// terminal section
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

int editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {
    if (nread == -1)
      die("editorReadKey::read()");
  }
  if (c == '\x1b')
  {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[')
    {
      if (seq[1] >= '0' && seq[1] <= '9')
      {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~')
        {
          switch (seq[1])
          {
          case '3':
            return DELETE;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      }
      else
      {
        switch (seq[1])
        {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'F':
          return END;
        case 'H':
          return HOME;
        }
      }
    }
    else if (seq[0] == 'O')
    {
      switch (seq[1])
      {
      case 'F':
        return END;
      case 'H':
        return HOME;
      }
    }
    return '\x1b';
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

// row operations
void editorAppendRow(char *s, size_t len)
{
  E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));
  int at = E.numRows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numRows++;
}

// file section
void editorOpen(char *filename)
{
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("editorOpen::fopen()");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1)
  {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
    {
      linelen--;
      editorAppendRow(line, linelen);
    }
  }
  free(line);
  fclose(fp);
}

// input section

void editorMoveCursor(int key)
{
  erow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];

  switch (key)
  {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    else if (E.cy != 0)
    {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size)
      E.cx++;
    else if (row && E.cy != E.numRows)
    {
      E.cy++;
      E.cx = E.colOff;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numRows)
      E.cy++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  }

  // snap to current row-length if user tries to
  // go to a longer line and then scroll back up
  row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
  {
    E.cx = rowlen;
  }
}

void editorProcessKey()
{
  int c = editorReadKey();
  switch (c)
  {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  case PAGE_UP:
  case PAGE_DOWN:
  {
    int times = E.screenRows;
    while (times--)
    {
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
  }
  break;
  case HOME:
    E.cx = E.colOff;
    break;
  case END:
    if (E.row != NULL)
      E.cx = E.row[E.cy].size;
    break;
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

void editorScroll()
{
  if (E.cy < E.rowOff)
  {
    E.rowOff = E.cy;
  }
  if (E.cy >= E.rowOff + E.screenRows)
  {
    E.rowOff = E.cy - E.screenRows + 1;
  }
  if (E.cx < E.colOff)
  {
    E.colOff = E.cx;
  }
  if (E.cx >= E.colOff + E.screenCols)
  {
    E.colOff = E.cx - E.screenCols + 1;
  }
}

void editorDrawRows(struct abuf *ab)
{
  for (int y = 0; y < E.screenRows; y++)
  {
    // if these rows are not meant to contain
    // text.
    int offset_y = y + E.rowOff;
    if (offset_y >= E.numRows)
    {
      if (E.numRows == 0 && y == E.screenRows / 2)
      {
        char welcome[80];
        int welcome_len = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
        if (welcome_len > E.screenCols)
          welcome_len = E.screenCols;
        int padding = (E.screenCols - welcome_len) / 2;
        if (padding)
        {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
        {
          abAppend(ab, " ", 1);
        }
        abAppend(ab, welcome, welcome_len);
      }
      else
      {
        abAppend(ab, "~", 1);
      }
    }
    else
    {
      int len = E.row[offset_y].size - E.colOff;
      if (len < 0)
        len = 0;
      if (len > E.screenCols)
        len = E.screenCols;
      abAppend(ab, E.row[offset_y].chars + E.colOff, len);
    }
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenRows - 1)
      abAppend(ab, "\r\n", 2);
  }
}

void editorRefreshScreen()
{
  editorScroll();
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  char buff[32];
  int buff_len = snprintf(buff, sizeof(buff), "\x1b[%d;%dH", E.cy - E.rowOff + 1, E.cx - E.colOff + 1);
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
  E.numRows = 0;
  E.rowOff = 0;
  E.colOff = 0;
  E.row = NULL;
  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("initEditor::getWindowSize()");
}

int main(int argc, char **argv)
{
  enableRawMode();
  initEditor();
  if (argc >= 2)
  {
    editorOpen(argv[1]);
  }
  while (1)
  {
    editorRefreshScreen();
    editorProcessKey();
  }
  return 0;
}