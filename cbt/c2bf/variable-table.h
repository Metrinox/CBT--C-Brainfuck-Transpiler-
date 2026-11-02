#ifndef VARIABLE_TABLE_H
#define VARIABLE_TABLE_H

#include "tokenise.h"
#include "parser.h"

// cells 0-9 reserved for temp vars used in "if", "while", "switch", etc
// cells 10-17 reserved for array scratch slots (see ARR_SLOT)
#define MAX_ARR_DEPTH 4
#define ARR_SLOTS_PER_DEPTH 2
#define ARR_SLOT_BASE 10
#define ARR_SLOT(depth, k) (ARR_SLOT_BASE + (depth) * ARR_SLOTS_PER_DEPTH + (k))
#define INITIALPOS (ARR_SLOT_BASE + MAX_ARR_DEPTH * ARR_SLOTS_PER_DEPTH)

// Arrays are laid out as:
//   [x] [y] [z] [arr[0]] [pad] [pad] [pad] [arr[1]] [pad] [pad] [pad] ...
// x, y, z form a 3-cell window that marches along the padding to reach an
// element. arr[i] lives at pos + ARR_HEADER + ARR_STRIDE * i
#define ARR_HEADER 3
#define ARR_STRIDE 4

typedef struct VarNode {
    char* str;  // Name of variable
    int pos;        // Position of bf cell (base of window for arrays)
    int size;       // Number of elements, 0 if not an array
    struct VarNode* next; // Next item in linked list
} VarNode;

typedef struct {
    VarNode* head;
    VarNode* tail;
} VariableTable;

extern VariableTable varTable;

void createVarTable(C_Block* block);
void freeVarTable(VariableTable varTable);

int varToPos(char* str);
int varSize(char* str);

// TESTING PURPOSES USED IN MAIN
void printVarTable();
#endif
