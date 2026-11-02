#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokenise.h"
#include "variable-table.h"

VariableTable varTable = {
    .head = NULL,
    .tail = NULL,
};

static VarNode* createNode(char* str, int pos, int size) {
    VarNode* node = malloc(sizeof(VarNode));
    assert(node != NULL);

    node->next = NULL;
    node->pos = pos;
    node->size = size;
    node->str = str;

    return node;
}

void freeVarTable(VariableTable varTable) {
    while (varTable.head != NULL) {
        VarNode *nextnode = varTable.head->next;
        free(varTable.head);
        varTable.head = nextnode; 
    }
}

static bool seen(char* str) {
    for (VarNode* curr = varTable.head; curr != NULL; curr = curr->next) {
        if (STREQ(str, curr->str)) {
            return true;
        }
    }
    return false;
}


static void appendNode(VarNode* node) {
    if (varTable.tail == NULL) {
        varTable.head = node;
        varTable.tail = node;
    } else {
        varTable.tail->next = node;
        varTable.tail = node;
    }
}

// Walks the parsed program (recursing into nested blocks) and assigns
// cells to every declared variable. Arrays take ARR_STRIDE cells per
// element: the window/padding cells plus the element itself.
static void addDecls(C_Block* block, int* pos) {
    for (C_Block* curr = block; curr != NULL; curr = curr->next) {
        C_Stmt* stmt = curr->stmt;
        switch (stmt->type) {
            case C_STMT_DECLARE:
                if (seen(stmt->operands[0].atom->string)) break;
                appendNode(createNode(stmt->operands[0].atom->string, *pos, 0));
                *pos += 1;
                break;
            case C_STMT_DECLAREARR: {
                int size = (unsigned char)stmt->operands[1].atom->val;
                if (size == 0) {
                    fprintf(stderr, "Array %s must have a positive size\n",
                        stmt->operands[0].atom->string);
                    exit(1);
                }
                if (seen(stmt->operands[0].atom->string)) break;
                appendNode(createNode(stmt->operands[0].atom->string, *pos, size));
                *pos += ARR_STRIDE * size;
                break;
            }
            default:
                for (size_t i = 0; i < stmt->numOperands; i++) {
                    if (stmt->optypes[i] == BLOCK) {
                        addDecls(stmt->operands[i].block, pos);
                    }
                }
        }
    }
}

void createVarTable(C_Block* block) {
    int pos = INITIALPOS;
    addDecls(block, &pos);
}

// given a variable, returns the position in brainfuck array of cells
int varToPos(char* str) {
    for (VarNode* curr = varTable.head; curr != NULL; curr = curr->next) {
        if (STREQ(str, curr->str)) {
            return curr->pos;
        }
    }

    fprintf(stderr, "Variable with name: %s does not exist in table\n", str);
    exit(1);
}

// given a variable, returns its number of elements (0 if not an array)
int varSize(char* str) {
    for (VarNode* curr = varTable.head; curr != NULL; curr = curr->next) {
        if (STREQ(str, curr->str)) {
            return curr->size;
        }
    }

    fprintf(stderr, "Variable with name: %s does not exist in table\n", str);
    exit(1);
}

void printVarTable() {
    printf("Variable Table: \n");
    for (VarNode* curr = varTable.head; curr != NULL; curr = curr->next) {
        if (curr->size == 0) {
            printf("Variable: %s -> %d\n", curr->str, curr->pos);
        } else {
            printf("Array: %s[%d] -> %d\n", curr->str, curr->size, curr->pos);
        }
    }
}