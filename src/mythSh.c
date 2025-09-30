#include "todo.h"
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_INPUTS 1024
#define MAX_ARGS 64
#define MAX_PROMPT 256

char current_prompt[MAX_PROMPT] = "mythsh> "; // default prompt

void parse_input(char *input, char **args) {
  char *token;
  int i = 0;

  token = strtok(input, " ");
  while (token != NULL && i < MAX_ARGS - 1) {
    args[i++] = token;
    token = strtok(NULL, " ");
  }
  args[i] = NULL;
}

void build_prompt(char *input_template, char *output) {
  char temp[MAX_PROMPT];
  int i = 0, j = 0;

  while (input_template[i] != '\0' && j < MAX_PROMPT - 1) {
    if (input_template[i] == '%' && input_template[i + 1] != '\0') {
      i++;
      if (input_template[i] == 'u') {
        // username
        struct passwd *pw = getpwuid(getuid());
        j += snprintf(&temp[j], MAX_PROMPT - j, "%s", pw->pw_name);
      } else if (input_template[i] == 'h') {
        // hostname
        char hostname[HOST_NAME_MAX];
        gethostname(hostname, sizeof(hostname));
        j += snprintf(&temp[j], MAX_PROMPT - j, "%s", hostname);
      } else if (input_template[i] == 'd') {
        // current directory
        char cwd[PATH_MAX];
        getcwd(cwd, sizeof(cwd));
        j += snprintf(&temp[j], MAX_PROMPT - j, "%s", cwd);
      } else {
        temp[j++] = '%';
        temp[j++] = input_template[i];
      }
    } else {
      temp[j++] = input_template[i];
    }
    i++;
  }
  temp[j] = '\0';
  strncpy(output, temp, MAX_PROMPT);
}

void replace_escaped_newlines(char *str) {
  char buffer[MAX_PROMPT];
  int i=0, j=0;
  while (str[i] != '\0' && j < MAX_PROMPT-1) {
      if (str[i] == '\\' && str[i+1] == 'n') {
          buffer[j++] = '\n';
          i += 2;
      } else {
          buffer[j++] = str[i++];
      }
  }
  buffer[j] = '\0';
  strcpy(str, buffer);
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
  if (strcmp(args[0], "mood") == 0) {
    if (args[1] == NULL) {
      printf("Usage: mood <hacker|chill|gamer|lofi>\n");
    } else if (strcmp(args[1], "hacker") == 0) {
      strcpy(current_prompt, "╭─[👨‍💻 mythsh-hacker] %d\n"
                             "╰─> ");
    } else if (strcmp(args[1], "chill") == 0) {
      strcpy(current_prompt, "╭─[😎 mythsh-chill] %d\n"
                             "╰─> ");
    } else if (strcmp(args[1], "gamer") == 0) {
      strcpy(current_prompt, "╭─[🎮 mythsh-gamer] %d\n"
                             "╰─> ");
    } else if (strcmp(args[1], "lofi") == 0) {
      strcpy(current_prompt, "╭─[🎶 mythsh-lofi] %d\n"
                             "╰─> ");
    } else {
      printf("Unknown mood: %s\n", args[1]);
    }
    return 1;
  }
  if (strcmp(args[0], "todo") == 0) {
    if (args[1] == NULL) {
      printf("Usage: todo [add|list|done]\n");
    } else if (strcmp(args[1], "add") == 0 && args[2]) {
      todo_add(args[2]);
    } else if (strcmp(args[1], "list") == 0) {
      todo_list();
    } else if (strcmp(args[1], "done") == 0 && args[2]) {
      todo_done(atoi(args[2]));
    } else {
      printf("Invalid todo command\n");
    }
    return 1;
  }

  if (strcmp(args[0], "setprompt") == 0 && args[1] != NULL) {
    // join all remaining args as one string
    char new_prompt[MAX_PROMPT] = "";
    for (int k = 1; args[k] != NULL; k++) {
      strcat(new_prompt, args[k]);
      strcat(new_prompt, " ");
    }
    replace_escaped_newlines(new_prompt);  // <-- convert \n to actual newline
    build_prompt(new_prompt, current_prompt);
    return 1; // handled
  }

  return 0;
}

void load_myshrc() {
  char path[1024];
  snprintf(path, sizeof(path), "%s/.mythrc", getenv("HOME"));

  FILE *file = fopen(path, "r");
  if (!file)
    return; // no rc file, skip

  char line[MAX_INPUTS];
  char *args[MAX_ARGS];

  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\n")] = 0; // remove newline
    if (line[0] == '#' || strlen(line) == 0)
      continue; // skip comments/empty

    parse_input(line, args);
    if (args[0] != NULL) {
      handle_builtin(args); // treat rc file lines like user input
    }
  }
  fclose(file);
}

int main() {
  load_myshrc();

  char input[MAX_INPUTS];
  char *args[MAX_ARGS];

  pid_t pid;
  int status;
  while (1) {
    printf("%s", current_prompt);
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL)
      break;
    // exit if ctrl+D

    input[strcspn(input, "\n")] = 0; // remove newline

    if (strcmp(input, "exit") == 0) {
      printf("Goodbye!\n");
      break;
    }

    parse_input(input, args);

    if (args[0] == NULL)
      continue;
    if (handle_builtin(args))
      continue;

    pid = fork();

    if (pid == 0) {
      if (execvp(args[0], args) == -1) {
        perror("mythsh:command not found");
      }
      exit(1);
    } else if (pid > 0) {
      waitpid(pid, &status, 0);
    } else {
      // fork failed
      perror("fork failed");
    }
  }
  return 0;
}