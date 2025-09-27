#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "todo.h"

#define TODO_FILE ".myth_todo"
#define MAX_TASK 1024

char* get_todo_path() {
    static char path[1024];
    char *home = getenv("HOME");
    snprintf(path, sizeof(path), "%s/%s", home, TODO_FILE);
    return path;
}

void todo_add(const char *task) {
    FILE *f = fopen(get_todo_path(), "a");
    if (!f) {
        perror("mysh: could not open todo file");
        return;
    }
    fprintf(f, "%s\n", task);
    fclose(f);
}

void todo_list() {
    FILE *f = fopen(get_todo_path(), "r");
    if (!f) {
        printf("No tasks yet!\n");
        return;
    }

    char line[MAX_TASK];
    int i = 1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0; // remove newline
        printf("[%d] %s\n", i++, line);
    }
    fclose(f);
}

void todo_done(int index) {
    FILE *f = fopen(get_todo_path(), "r");
    if (!f) {
        printf("No tasks to complete!\n");
        return;
    }

    char **tasks = NULL;
    size_t count = 0;
    char line[MAX_TASK];

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        char **temp = realloc(tasks, (count + 1) * sizeof(char*));
        if (temp == NULL) {
            perror("realloc failed");
            // Clean up existing tasks
            for (size_t i = 0; i < count; i++) {
                free(tasks[i]);
            }
            free(tasks);
            fclose(f);
            return;
        }
        tasks = temp;
        tasks[count] = strdup(line);
        if (tasks[count] == NULL) {
            perror("strdup failed");
            // Clean up existing tasks
            for (size_t i = 0; i < count; i++) {
                free(tasks[i]);
            }
            free(tasks);
            fclose(f);
            return;
        }
        count++;
    }
    fclose(f);

    if (index < 1 || (size_t)index > count) {
        printf("Invalid task number.\n");
    } else {
        free(tasks[index-1]);
        for (size_t i = index - 1; i < count - 1; i++) {
            tasks[i] = tasks[i+1];
        }
        count--;
    }

    f = fopen(get_todo_path(), "w");
    for (size_t i = 0; i < count; i++) {
        fprintf(f, "%s\n", tasks[i]);
        free(tasks[i]);
    }
    fclose(f);
    free(tasks);
}