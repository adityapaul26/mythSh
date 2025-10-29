#include "todo.h"
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h> // for MAXHOSTNAMELEN fallback
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef HOST_NAME_MAX
#ifdef MAXHOSTNAMELEN
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 64
#endif
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_INPUTS 1024
#define MAX_ARGS 64
#define MAX_PROMPT 256
#define MAX_HISTORY 1000
#define HISTORY_FILE ".mythsh_history"

struct termios orig_term;

char *history[MAX_HISTORY];
int history_count = 0;
int history_index = 0;

char current_prompt[MAX_PROMPT] = "mythsh> "; // default prompt

/* Simple tokenization on whitespace. Note: this does NOT support quoted args.
   If you want quoting (e.g. "some arg with spaces") you'll need a tokenizer
   that recognizes quotes. */

void load_history() {
  FILE *file = fopen(HISTORY_FILE, "r");
  if (!file)
    return;

  char line[1024];
  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\n")] = '\0'; // Remove newline
    if (strlen(line) > 0 && history_count < MAX_HISTORY)
      history[history_count++] = strdup(line);
  }
  fclose(file);
  history_index = history_count;
}

void save_history() {
  FILE *file = fopen(HISTORY_FILE, "w");
  if (!file)
    return;
  for (int i = 0; i < history_count; i++) {
    fprintf(file, "%s\n", history[i]);
  }
  fclose(file);
}

void parse_input(char *input, char **args) {
  char *token;
  int i = 0;

  token = strtok(input, " \t\n");
  while (token != NULL && i < MAX_ARGS - 1) {
    args[i++] = token;
    token = strtok(NULL, " \t\n");
  }
  args[i] = NULL;
}

/* Build prompt from a template with %u (user), %h (hostname), %d (cwd). */
void build_prompt(const char *input_template, char *output) {
  char temp[MAX_PROMPT];
  int i = 0, j = 0;

  while (input_template[i] != '\0' && j < MAX_PROMPT - 1) {
    if (input_template[i] == '%' && input_template[i + 1] != '\0') {
      i++;
      if (input_template[i] == 'u') {
        struct passwd *pw = getpwuid(getuid());
        const char *name = (pw && pw->pw_name) ? pw->pw_name : "unknown";
        int written = snprintf(&temp[j], MAX_PROMPT - j, "%s", name);
        if (written > 0)
          j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
      } else if (input_template[i] == 'h') {
        char hostname[HOST_NAME_MAX];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
          hostname[sizeof(hostname) - 1] = '\0';
          int written = snprintf(&temp[j], MAX_PROMPT - j, "%s", hostname);
          if (written > 0)
            j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
        } else {
          int written = snprintf(&temp[j], MAX_PROMPT - j, "%s", "host");
          if (written > 0)
            j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
        }
      } else if (input_template[i] == 'd') {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
          int written = snprintf(&temp[j], MAX_PROMPT - j, "%s", cwd);
          if (written > 0)
            j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
        } else {
          int written = snprintf(&temp[j], MAX_PROMPT - j, "%s", ".");
          if (written > 0)
            j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
        }
      } else {
        /* Unknown directive: emit %<char> literally */
        if (j < MAX_PROMPT - 2) {
          temp[j++] = '%';
          temp[j++] = input_template[i];
        } else if (j < MAX_PROMPT - 1) {
          temp[j++] = '%';
        }
      }
    } else {
      temp[j++] = input_template[i];
    }
    i++;
  }
  temp[j] = '\0';
  /* Ensure output is null-terminated and fits */
  strncpy(output, temp, MAX_PROMPT - 1);
  output[MAX_PROMPT - 1] = '\0';
}

/* Replace literal backslash-n sequences ("\\n") with actual newline characters.
   Works in-place; 'str' buffer must be at most size bytes long. */
void replace_escaped_newlines(char *str, size_t size) {
  char buffer[MAX_PROMPT];
  size_t i = 0, j = 0;
  (void)size; // size not used now; we limit by MAX_PROMPT
  while (str[i] != '\0' && j < MAX_PROMPT - 1) {
    if (str[i] == '\\' && str[i + 1] == 'n') {
      buffer[j++] = '\n';
      i += 2;
    } else {
      buffer[j++] = str[i++];
    }
  }
  buffer[j] = '\0';
  /* Copy back safely */
  strncpy(str, buffer, MAX_PROMPT - 1);
  str[MAX_PROMPT - 1] = '\0';
}

int handle_builtin(char **args) {
  if (args[0] == NULL)
    return 0;

  // exit
  if (strcmp(args[0], "exit") == 0) {
    printf("Goodbye!\n");
    exit(0);
  }

  // cd
  if (strcmp(args[0], "cd") == 0) {
    if (args[1] == NULL) {
      fprintf(stderr, "mythsh: expected argument to \"cd\"\n");
    } else {
      if (chdir(args[1]) != 0) {
        perror("mythsh");
      }
    }
    return 1;
  }

  // mood (predefined prompts)
  if (strcmp(args[0], "mood") == 0) {
    if (args[1] == NULL) {
      printf("Usage: mood <hacker|chill|gamer|lofi>\n");
    } else if (strcmp(args[1], "hacker") == 0) {
      strncpy(current_prompt,
              "╭─[👨‍💻 mythsh-hacker]\n"
              "╰─> ",
              MAX_PROMPT - 1);
    } else if (strcmp(args[1], "chill") == 0) {
      strncpy(current_prompt,
              "╭─[😎 mythsh-chill]\n"
              "╰─> ",
              MAX_PROMPT - 1);
    } else if (strcmp(args[1], "gamer") == 0) {
      strncpy(current_prompt,
              "╭─[🎮 mythsh-gamer]\n"
              "╰─> ",
              MAX_PROMPT - 1);
    } else if (strcmp(args[1], "lofi") == 0) {
      strncpy(current_prompt,
              "╭─[🎶 mythsh-lofi]\n"
              "╰─> ",
              MAX_PROMPT - 1);
    } else {
      printf("Unknown mood: %s\n", args[1]);
    }
    current_prompt[MAX_PROMPT - 1] = '\0';
    return 1;
  }

  // todo builtins: add/list/done
  if (strcmp(args[0], "todo") == 0) {
    if (args[1] == NULL) {
      printf("Usage: todo [add|list|done]\n");
    } else if (strcmp(args[1], "add") == 0) {
      if (args[2] == NULL) {
        printf("Usage: todo add <task>\n");
      } else {
        /* join remaining args into one string so tasks can have spaces */
        char task[MAX_INPUTS];
        task[0] = '\0';
        size_t pos = 0;
        for (int k = 2; args[k] != NULL; k++) {
          size_t need = strlen(args[k]);
          if (pos + need + 2 >= sizeof(task))
            break;
          if (pos != 0) {
            task[pos++] = ' ';
          }
          memcpy(&task[pos], args[k], need);
          pos += need;
          task[pos] = '\0';
        }
        todo_add(task);
      }
    } else if (strcmp(args[1], "list") == 0) {
      todo_list();
    } else if (strcmp(args[1], "done") == 0) {
      if (args[2] == NULL) {
        printf("Usage: todo done <id>\n");
      } else {
        todo_done(atoi(args[2]));
      }
    } else {
      printf("Invalid todo command\n");
    }
    return 1;
  }

  // setprompt: all remaining args are joined to form the template
  if (strcmp(args[0], "setprompt") == 0 && args[1] != NULL) {
    char new_prompt[MAX_PROMPT];
    size_t pos = 0;
    new_prompt[0] = '\0';
    for (int k = 1; args[k] != NULL; k++) {
      size_t need = strlen(args[k]);
      if (pos + need + 2 >= sizeof(new_prompt))
        break;
      if (pos != 0)
        new_prompt[pos++] = ' ';
      memcpy(&new_prompt[pos], args[k], need);
      pos += need;
      new_prompt[pos] = '\0';
    }
    new_prompt[MAX_PROMPT - 1] = '\0';
    replace_escaped_newlines(new_prompt, sizeof(new_prompt));
    build_prompt(new_prompt, current_prompt);
    return 1; // handled
  }

  return 0;
}

void load_myshrc() {
  const char *home = getenv("HOME");
  if (!home)
    return;

  char path[1024];
  snprintf(path, sizeof(path), "%s/.mythrc", home);

  FILE *file = fopen(path, "r");
  if (!file)
    return; // no rc file, skip

  char line[MAX_INPUTS];
  char *args[MAX_ARGS];

  while (fgets(line, sizeof(line), file)) {
    /* remove trailing newline */
    line[strcspn(line, "\n")] = 0;
    /* skip comments and empty lines */
    size_t len = strlen(line);
    if (len == 0)
      continue;
    /* trim leading whitespace */
    char *p = line;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r'))
      p++;
    if (*p == '#' || *p == '\0')
      continue;

    /* parse */
    parse_input(p, args);
    if (args[0] != NULL) {
      /* treat rc commands as builtins where appropriate */
      if (!handle_builtin(args)) {
        /* if not builtin, you might want to exec them or ignore; here we ignore
         */
      }
    }
  }
  fclose(file);
}

void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &orig_term); // save original terminal state
  struct termios term = orig_term;
  term.c_lflag &= ~(ICANON | ECHO); // disable canonical mode and echo
  term.c_cc[VMIN] = 1;              // minimum number of characters for read
  term.c_cc[VTIME] = 0;             // timeout in deciseconds
  tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void disable_raw_mode(void) { tcsetattr(STDIN_FILENO, TCSANOW, &orig_term); }
int main(void) {
  load_myshrc();
  enable_raw_mode();
  load_history();

  char input[MAX_INPUTS];
  char *args[MAX_ARGS];
  int pos = 0;

  pid_t pid;
  int status;

  while (1) {
    pos = 0;
    memset(input, 0, sizeof(input));
    printf("%s", current_prompt);
    fflush(stdout);

    while (1) {
      char c = getchar();

      if (c == '\n') { // Enter
        putchar('\n');
        break;
      } else if (c == 127) { // Backspace
        if (pos > 0) {
          pos--;
          input[pos] = '\0';
          printf("\b \b");
          fflush(stdout);
        }
      } else if (c == '\033') { // Arrow keys
        char c2 = getchar();
        if (c2 == '[') {
          char dir = getchar();

          if (dir == 'A') { // UP
            if (history_index > 0) {
                history_index--;
                // Clear current line first
                printf("\r\033[K");
                
                // Count newlines in prompt
                int lines = 0;
                for (int i = 0; current_prompt[i]; i++) {
                    if (current_prompt[i] == '\n') lines++;
                }
                
                // Move up and clear each prompt line
                for (int i = 0; i < lines; i++) {
                    printf("\033[A\033[K");
                }
                
                // Reprint prompt and history
                printf("\r%s%s", current_prompt, history[history_index]);
                fflush(stdout);
                strcpy(input, history[history_index]);
                pos = strlen(input);
            }
        } 
        else if (dir == 'B') { // DOWN
            if (history_index < history_count - 1) {
                history_index++;
                // Clear current line first
                printf("\r\033[K");
                
                // Count newlines in prompt
                int lines = 0;
                for (int i = 0; current_prompt[i]; i++) {
                    if (current_prompt[i] == '\n') lines++;
                }
                
                // Move up and clear each prompt line
                for (int i = 0; i < lines; i++) {
                    printf("\033[A\033[K");
                }
                
                // Reprint prompt and history
                printf("\r%s%s", current_prompt, history[history_index]);
                fflush(stdout);
                strcpy(input, history[history_index]);
                pos = strlen(input);
            } else {
                // Clear current line first
                printf("\r\033[K");
                
                // Count newlines in prompt
                int lines = 0;
                for (int i = 0; current_prompt[i]; i++) {
                    if (current_prompt[i] == '\n') lines++;
                }
                
                // Move up and clear each prompt line
                for (int i = 0; i < lines; i++) {
                    printf("\033[A\033[K");
                }
                
                // Reprint just the prompt
                printf("\r%s", current_prompt);
                fflush(stdout);
                input[0] = '\0';
                pos = 0;
                history_index = history_count;
            }
        }
        }
      } else { // normal character
        input[pos++] = c;
        putchar(c);
        fflush(stdout);
      }
    }

    if (pos > 0) {
      input[pos] = '\0';

      // add to history
      if (history_count == 0 ||
          strcmp(history[history_count - 1], input) != 0) {
        if (history_count < MAX_HISTORY)
          history[history_count++] = strdup(input);
      }
      history_index = history_count;
    }

    if (strcmp(input, "exit") == 0)
      break;

    parse_input(input, args);

    if (args[0] == NULL)
      continue;

    if (handle_builtin(args))
      continue;

    pid = fork();

    if (pid == 0) { // child
      execvp(args[0], args);
      fprintf(stderr, "mythsh: %s: %s\n", args[0], strerror(errno));
      _exit(127);
    } else if (pid > 0) { // parent
      waitpid(pid, &status, 0);
    } else {
      perror("mythsh: fork error");
    }
  }

  disable_raw_mode();
  save_history();

  return 0;
}
