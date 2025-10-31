#include "todo.h"
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h> // for MAXHOSTNAMELEN fallback
#include <sys/stat.h>
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
#define MAX_PROMPT 1024
#define MAX_HISTORY 1000
#define HISTORY_FILE ".mythsh_history"
char theme[16] = "mini";

struct termios orig_term;

char *history[MAX_HISTORY];
int history_count = 0;
int history_index = 0;

char current_prompt[MAX_PROMPT] = "mythsh> "; // default prompt
char current_prompt_template[MAX_PROMPT] =
    ""; // empty means no dynamic template
bool has_prompt_template = false;

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

bool is_git_repo() {
  struct stat st;
  return (stat(".git", &st) == 0 && S_ISDIR(st.st_mode));
}

void get_git_branch(char *branch, size_t size) {
  FILE *fp = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
  if (!fp) {
    branch[0] = '\0';
    return;
  }
  if (fgets(branch, size, fp) != NULL)
    branch[strcspn(branch, "\n")] = '\0'; // strip newline
  else
    branch[0] = '\0';
  pclose(fp);
}

static bool is_utf8_locale(void) {
  const char *vars[] = {getenv("LC_ALL"), getenv("LC_CTYPE"), getenv("LANG")};
  for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
    if (vars[i] && (strstr(vars[i], "UTF-8") || strstr(vars[i], "utf8"))) {
      return true;
    }
  }
  return false;
}

/* Build prompt from a template with %u (user), %h (hostname), %d (cwd). */
void build_prompt(const char *input_template, char *output) {
  if (strcmp(theme, "graphic") == 0) {
    char temp[MAX_PROMPT];
    int i = 0, j = 0;

    while (input_template[i] != '\0' && j < MAX_PROMPT - 1) {
      if (input_template[i] == '%' && input_template[i + 1] != '\0') {
        i++;
        if (input_template[i] == 'u') {
          struct passwd *pw = getpwuid(getuid());
          const char *name = (pw && pw->pw_name) ? pw->pw_name : "unknown";
          int written = snprintf(&temp[j], MAX_PROMPT - j,
                                 "\033[48;5;74m\033[38;5;232m %s "
                                 "\033[0m\033[38;5;74m\ue0b0\033[0m",
                                 name);
          if (written > 0)
            j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
        } else if (input_template[i] == 'h') {
          char hostname[HOST_NAME_MAX];
          if (gethostname(hostname, sizeof(hostname)) == 0) {
            hostname[sizeof(hostname) - 1] = '\0';
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[48;5;208m\033[38;5;232m %s "
                                   "\033[0m\033[38;5;208m\ue0b0\033[0m",
                                   hostname);
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          } else {
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[48;5;208m\033[38;5;232m %s "
                                   "\033[0m\033[38;5;208m\ue0b0\033[0m",
                                   "host");
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          }
        } else if (input_template[i] == 'd') {
          char cwd[PATH_MAX];
          if (getcwd(cwd, sizeof(cwd)) != NULL) {
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[48;5;183m\033[38;5;232m %s "
                                   "\033[0m\033[38;5;183m\ue0b0\033[0m",
                                   cwd);
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          } else {
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[48;5;183m\033[38;5;232m %s "
                                   "\033[0m\033[38;5;183m\ue0b0\033[0m",
                                   ".");
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
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
  } else {
    char temp[MAX_PROMPT];
    bool use_utf8 = is_utf8_locale();
    int i = 0, j = 0;

    while (input_template[i] != '\0' && j < MAX_PROMPT - 1) {
      if (input_template[i] == '%' && input_template[i + 1] != '\0') {
        i++;
        if (input_template[i] == 'u') {
          struct passwd *pw = getpwuid(getuid());
          const char *sym = use_utf8 ? "\uf007" : "usr";
          const char *name = (pw && pw->pw_name) ? pw->pw_name : "unknown";
          int written = snprintf(&temp[j], MAX_PROMPT - j,
                                 "\033[1;38;5;81m[\033[0m\033[38;5;117m%s "
                                 "%s\033[0m\033[1;38;5;81m]\033[0m ",
                                 sym, name);
          if (written > 0)
            j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
        } else if (input_template[i] == 'h') {
          char hostname[HOST_NAME_MAX];
          const char *sym = use_utf8 ? "\uf233" : "host";
          if (gethostname(hostname, sizeof(hostname)) == 0) {
            hostname[sizeof(hostname) - 1] = '\0';
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[1;38;5;214m[\033[0m\033[38;5;222m%s "
                                   "%s\033[0m\033[1;38;5;214m]\033[0m",
                                   sym, hostname);
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          } else {
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[1;38;5;214m[\033[0m\033[38;5;222m%s "
                                   "%s\033[0m\033[1;38;5;214m]\033[0m",
                                   sym, "host");
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          }
        } else if (input_template[i] == 'd') {
          char cwd[PATH_MAX];
          const char *sym = use_utf8 ? "\uf07c" : "dir";
          if (getcwd(cwd, sizeof(cwd)) != NULL) {
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[1;38;5;213m[\033[0m\033[38;5;219m%s "
                                   "%s\033[0m\033[1;38;5;213m]\033[0m",
                                   sym, cwd);
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          } else {
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[1;38;5;213m[\033[0m\033[38;5;219m%s "
                                   "%s\033[0m\033[1;38;5;213m]\033[0m",
                                   sym, ".");
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          }
        } else if (input_template[i] == 'g') {
          const char *sym = use_utf8 ? "\ue725" : "git";
          char branch[64] = "";
          if (is_git_repo()) {
            get_git_branch(branch, sizeof(branch));
            int written = snprintf(&temp[j], MAX_PROMPT - j,
                                   "\033[1;38;5;114m[\033[0m\033[38;5;120m%s "
                                   "%s\033[0m\033[1;38;5;114m]\033[0m",
                                   sym, branch);
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
          } else {
            // Optional: visual separator if not in git repo
            int written =
                snprintf(&temp[j], MAX_PROMPT - j,
                         use_utf8 ? "\033[38;5;240m\ue0b0\033[0m" : "-");
            if (written > 0)
              j +=
                  (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
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
    /* Ensure prompt ends with a reset so colors don't bleed even if truncated
     */
    if (j < MAX_PROMPT - 5) {
      int written = snprintf(&temp[j], MAX_PROMPT - j, "\033[0m");
      if (written > 0)
        j += (written < (MAX_PROMPT - j) ? written : (MAX_PROMPT - j - 1));
    }
    temp[j] = '\0';
    /* Ensure output is null-terminated and fits */
    strncpy(output, temp, MAX_PROMPT - 1);
    output[MAX_PROMPT - 1] = '\0';
  }
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
      printf("Usage: mood <hacker|chill|gamer|lofi|ghoul>\n");
    } else if (strcmp(args[1], "hacker") == 0) {
      strncpy(current_prompt,
              "╭─\033[48;5;208m\033[38;5;232m \uf21b "
              "\033[0m\033[38;5;208m\ue0b0\033[0m mythsh-hacker "
              "\033[38;5;208m\ue0b0\033[0m\n"
              "╰─\uf061 ",
              MAX_PROMPT - 1);
      has_prompt_template = false;
    } else if (strcmp(args[1], "chill") == 0) {
      strncpy(current_prompt,
              "╭─\033[48;5;74m\033[38;5;232m \uea85 "
              "\033[0m\033[38;5;74m\ue0b0\033[0m mythsh-chill "
              "\033[38;5;74m\ue0b0\033[0m\n"
              "╰─\uf061 ",
              MAX_PROMPT - 1);
      has_prompt_template = false;
    } else if (strcmp(args[1], "gamer") == 0) {
      strncpy(current_prompt,
              "╭─\033[48;5;170m\033[38;5;232m \uf11b "
              "\033[0m\033[38;5;170m\ue0b0\033[0m mythsh-gamer "
              "\033[38;5;170m\ue0b0\033[0m\n"
              "╰─\uf061 ",
              MAX_PROMPT - 1);
      has_prompt_template = false;
    } else if (strcmp(args[1], "lofi") == 0) {
      strncpy(current_prompt,
              "╭─\033[48;5;183m\033[38;5;232m \uf001 "
              "\033[0m\033[38;5;183m\ue0b0\033[0m mythsh-lofi "
              "\033[38;5;183m\ue0b0\033[0m\n"
              "╰─\uf061 ",
              MAX_PROMPT - 1);
      has_prompt_template = false;
    } else if (strcmp(args[1], "ghoul") == 0) {
      strncpy(current_prompt,
              "╭─\033[48;5;250m\033[38;5;232m \ueefe "
              "\033[0m\033[38;5;250m\ue0b0\033[0m mythsh-ghoul "
              "\033[38;5;250m\ue0b0\033[0m\n"
              "╰─\uf061 ",
              MAX_PROMPT - 1);
      has_prompt_template = false;
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
    strncpy(current_prompt_template, new_prompt, MAX_PROMPT - 1);
    current_prompt_template[MAX_PROMPT - 1] = '\0';
    has_prompt_template = true;
    build_prompt(current_prompt_template, current_prompt);
    return 1; // handled
  }

  if (strcmp(args[0], "theme") == 0 && args[1] != NULL) {
    strncpy(theme, args[1], sizeof(theme) - 1);
    theme[sizeof(theme) - 1] = '\0';
    return 1;
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
        // nah we aint gonna ignore them ... we gonna execute those commands
        id_t pid;
        pid = fork();

        if (pid == 0) {
          // child process
          execvp(args[0], args);
          fprintf(stderr, "mythsh: %s: %s\n", args[0], strerror(errno));
        } else if (pid > 0) {
          wait(NULL);
        } else {
          perror("mythsh: fork error");
        }
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
    if (has_prompt_template) {
      build_prompt(current_prompt_template, current_prompt);
    }
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
                if (current_prompt[i] == '\n')
                  lines++;
              }

              // Move up and clear each prompt line
              for (int i = 0; i < lines; i++) {
                printf("\033[A\033[K");
              }

              // Reprint prompt and history
              printf("\r%s%s", current_prompt, history[history_index]);
              fflush(stdout);
              strncpy(input, history[history_index], sizeof(input) - 1);
              input[sizeof(input) - 1] = '\0';
              pos = (int)strnlen(input, sizeof(input));
            }
          } else if (dir == 'B') { // DOWN
            if (history_index < history_count - 1) {
              history_index++;
              // Clear current line first
              printf("\r\033[K");

              // Count newlines in prompt
              int lines = 0;
              for (int i = 0; current_prompt[i]; i++) {
                if (current_prompt[i] == '\n')
                  lines++;
              }

              // Move up and clear each prompt line
              for (int i = 0; i < lines; i++) {
                printf("\033[A\033[K");
              }

              // Reprint prompt and history
              printf("\r%s%s", current_prompt, history[history_index]);
              fflush(stdout);
              strncpy(input, history[history_index], sizeof(input) - 1);
              input[sizeof(input) - 1] = '\0';
              pos = (int)strnlen(input, sizeof(input));
            } else {
              // Clear current line first
              printf("\r\033[K");

              // Count newlines in prompt
              int lines = 0;
              for (int i = 0; current_prompt[i]; i++) {
                if (current_prompt[i] == '\n')
                  lines++;
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
        if (pos < (int)sizeof(input) - 1) {
          input[pos++] = c;
          putchar(c);
          fflush(stdout);
        } else {
          putchar('\a');
          fflush(stdout);
        }
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
