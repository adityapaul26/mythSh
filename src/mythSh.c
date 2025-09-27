#include <stdio.h>     
#include <stdlib.h>    
#include <string.h>    
#include <sys/types.h> 
#include <sys/wait.h>
#include <unistd.h> // for fork, execvp

#define MAX_INPUT 1024
#define MAX_ARGS 64

char *current_prompt = "mysh>"; // default prompt

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
      printf("Usage: mood <hacker|chill>\n");
    } else if (strcmp(args[1], "hacker") == 0) {
      current_prompt = "[👨‍💻 mythsh-hacker]$ ";
    } else if (strcmp(args[1], "chill") == 0) {
      current_prompt = "[😎 mythsh-chill]$ ";
    } else if (strcmp(args[1], "gamer") == 0) {
      current_prompt = "[🎮 mythsh-gamer]$ ";
    } else if (strcmp(args[1], "lofi") == 0) {
      current_prompt = "[🎶 mythsh-lofi]$ ";
    } else {
      printf("Unknown mood: %s\n", args[1]);
    }
    return 1;
  }
  return 0; 
}

void load_myshrc() {
  char path[1024];
  snprintf(path, sizeof(path), "%s/.mythrc", getenv("HOME"));

  FILE *file = fopen(path, "r");
  if (!file) return; // no rc file, skip

  char line[MAX_INPUT];
  char *args[MAX_ARGS];

  while (fgets(line, sizeof(line), file)) {
      line[strcspn(line, "\n")] = 0; // remove newline
      if (line[0] == '#' || strlen(line) == 0) continue; // skip comments/empty

      parse_input(line, args);
      if (args[0] != NULL) {
          handle_builtin(args); // treat rc file lines like user input
      }
  }
  fclose(file);
}

int main() {
  load_myshrc();
  
  char input[MAX_INPUT];
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
        perror("mysh:command not found");
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