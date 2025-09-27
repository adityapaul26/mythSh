#ifndef TODO_H
#define TODO_H

#define MAX_TASK 1024  // maximum length of a single task

// function declarations
void todo_add(const char *task);
void todo_list();
void todo_done(int index);

// helper: returns the path to ~/.myth_todo
char *get_todo_path();

#endif

