#include "parser.h"
#include "variable-table.h"
#include "emitter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESULT_INDEX 3
#define GOTO_INDEX 9
#define GOTO_TEMP 8
#define TEMP_INDEX 6

// GREATER/LESS THAN
#define TEMP0 1
#define TEMP1 2
#define X 0
#define Y 4
#define Z 3

// PLUS/MINUS
#define EXPR_TEMP_INDEX 5

// NOT
#define NOT_X 7
#define NOT_Y 8

// ARRAYS
// The x, y, z window starts at the array's base and marches right one
// element (ARR_STRIDE cells) at a time, carrying the index in x (decremented
// each step), a copy of the index in y and the value to write in z.
// PRE: pointer at x, x == y == index
// POST: pointer at x of the window just before arr[index], x == 0
#define ARR_MARCH_RIGHT "[-[->>>>+<<<<]>[->>>>+<<<<]>[->>>>+<<<<]>>]"
// Marches the window back to the base using y as the counter, carrying x
// PRE: pointer at y
// POST: pointer at y of the window at the base, y == 0
#define ARR_MARCH_LEFT "[-<[-<<<<+>>>>]>[-<<<<+>>>>]<<<<]"
// copy arr[index] into x (using z as temp), then go to y
#define ARR_READ_ELEM ">>>[-<<<+>>+>]<[->+<]<"
// overwrite arr[index] with z, then go to y
#define ARR_WRITE_ELEM ">>>[-]<[->+<]<"

// how deeply nested the array index expression being evaluated is,
// e.g. in a[b[i]], i is evaluated at depth 2 and b[i] at depth 1
static int arr_depth = 0;
// which array atom has been fetched into each slot at the current depth
static C_Atom *slot_owner[MAX_ARR_DEPTH][ARR_SLOTS_PER_DEPTH];

void goto_index(FILE *out, int *dataptr, int index) {
    // move right until *dataptr == index
    while (*dataptr < index) {
        fputc('>', out);
        (*dataptr)++;
    }
    // move left until *dataptr == index
    while (*dataptr > index) {
        fputc('<', out);
        (*dataptr)--;
    }
}



// PRE:  
// POST: M[dataptr] == val
void set_equals_nat(FILE *out, int dest, int val, int *dataptr) {
    goto_index(out, dataptr, dest);
    fputs("[-]", out);
    if (val >= 0) {
        for (int i = 0; i < val; i++) fputc('+', out);
    } else {
        for (int i = 0; i < -val; i++) fputc('-', out);
    }
}



// PRE:  
// POST: M[dest] == original M[src], M[src] restored
// Uses TEMP_INDEX as temp
// 1. Sets dest to 0
// 2. Copy src to dest and temp, emptying src
// 3. Copy temp to src, emptying temp
void set_equals_var(FILE *out, int dest, int src, int *dataptr) {
    if (dest == src) return;
    // 1. Sets dest to 0
    set_equals_nat(out, TEMP_INDEX, 0, dataptr);
    set_equals_nat(out, dest, 0, dataptr);
    // 2. Copy src to dest and temp, emptying src
    goto_index(out, dataptr, src);
    fputc('[', out);              // while (M[src] > 0)
    fputc('-', out);              //   M[src]--
    goto_index(out, dataptr, dest);
    fputc('+', out);              //   M[dest]++
    goto_index(out, dataptr, TEMP_INDEX);
    fputc('+', out);              //   M[temp]++
    goto_index(out, dataptr, src);
    fputc(']', out);
    // 3. Copy temp to src, emptying temp
    goto_index(out, dataptr, TEMP_INDEX);
    fputc('[', out);              // while (M[temp] > 0)
    fputc('-', out);              //   M[temp]--
    goto_index(out, dataptr, src);
    fputc('+', out);              //   M[src]++
    goto_index(out, dataptr, TEMP_INDEX);
    fputc(']', out);
}


// returns the cell of a scalar variable, fails if it is an array
static int scalar_pos(char *name) {
    if (varSize(name) != 0) {
        fprintf(stderr, "Array %s used without an index\n", name);
        exit(1);
    }
    return varToPos(name);
}

// returns the base cell of an array variable, fails if it is a scalar
static int array_pos(char *name) {
    if (varSize(name) == 0) {
        fprintf(stderr, "Variable %s is not an array\n", name);
        exit(1);
    }
    return varToPos(name);
}

// PRE:  M[idx_cell] == index, base is the array's window
// POST: M[dest] == arr[index], arr and M[idx_cell] unchanged (unless
//       dest == idx_cell), window cells all 0
void emit_arr_read(FILE *out, int base, int idx_cell, int dest, int *dataptr) {
    set_equals_var(out, base, idx_cell, dataptr);
    set_equals_var(out, base + 1, idx_cell, dataptr);
    goto_index(out, dataptr, base);
    fputs(ARR_MARCH_RIGHT ARR_READ_ELEM ARR_MARCH_LEFT, out);
    // the march ends at a known cell no matter what the index was
    *dataptr = base + 1;
    // move x into dest
    set_equals_nat(out, dest, 0, dataptr);
    goto_index(out, dataptr, base);
    fputs("[-", out);
        goto_index(out, dataptr, dest);
        fputc('+', out);
        goto_index(out, dataptr, base);
    fputc(']', out);
}

// PRE:  M[idx_cell] == index, M[val_cell] == value
// POST: arr[index] == value, window cells all 0
void emit_arr_write(FILE *out, int base, int idx_cell, int val_cell, int *dataptr) {
    set_equals_var(out, base, idx_cell, dataptr);
    set_equals_var(out, base + 1, idx_cell, dataptr);
    set_equals_var(out, base + 2, val_cell, dataptr);
    goto_index(out, dataptr, base);
    fputs(ARR_MARCH_RIGHT ARR_WRITE_ELEM ARR_MARCH_LEFT, out);
    *dataptr = base + 1;
}

// POST: M[dest] == value of the index expression of atom
void emit_index(FILE *out, C_Atom *atom, int dest, int *dataptr) {
    arr_depth++;
    emit_expr(out, atom->offset, dataptr);
    arr_depth--;
    set_equals_var(out, dest, RESULT_INDEX, dataptr);
}

// If atom is an array access, evaluates it into slot k of the current depth
// so that set_equals can later copy it like a normal variable
void prefetch_arr(FILE *out, C_Atom *atom, int k, int *dataptr) {
    if (atom == NULL || atom->type != C_IDENT || atom->offset == NULL) return;
    if (arr_depth >= MAX_ARR_DEPTH) {
        fail("Array accesses nested too deeply");
    }
    int base = array_pos(atom->string);
    int slot = ARR_SLOT(arr_depth, k);
    emit_index(out, atom, slot, dataptr);
    emit_arr_read(out, base, slot, slot, dataptr);
    slot_owner[arr_depth][k] = atom;
}

// returns the slot that atom was prefetched into
static int fetched_slot(C_Atom *atom) {
    for (int k = 0; k < ARR_SLOTS_PER_DEPTH; k++) {
        if (slot_owner[arr_depth][k] == atom) return ARR_SLOT(arr_depth, k);
    }
    fail("Array access was not prefetched");
}

// arr[<expr>] = M[RESULT_INDEX]
void emit_arr_assign(FILE *out, C_Atom *lhs, int *dataptr) {
    int base = array_pos(lhs->string);
    int val = ARR_SLOT(arr_depth, 0);
    int idx = ARR_SLOT(arr_depth, 1);
    set_equals_var(out, val, RESULT_INDEX, dataptr);
    emit_index(out, lhs, idx, dataptr);
    emit_arr_write(out, base, idx, val, dataptr);
}

// arr[<expr>]++ or arr[<expr>]--
void emit_arr_incdec(FILE *out, C_Atom *atom, char op, int *dataptr) {
    int base = array_pos(atom->string);
    int val = ARR_SLOT(arr_depth, 0);
    int idx = ARR_SLOT(arr_depth, 1);
    emit_index(out, atom, idx, dataptr);
    emit_arr_read(out, base, idx, val, dataptr);
    goto_index(out, dataptr, val);
    fputc(op, out);
    emit_arr_write(out, base, idx, val, dataptr);
}


// PRE:  M[TEMP_INDEX] == 0
// POST: M[dest] == original M[src], M[src] restored, 
//       M[TEMP_INDEX] == 0
void set_equals(FILE *out, int dest, C_Atom *src_atom, int *dataptr) {
    if (src_atom->type == C_IDENT) {
        if (src_atom->offset == NULL) {
            set_equals_var(out, dest, scalar_pos(src_atom->string), dataptr);
        } else {
            // array accesses were already read by prefetch_arr
            set_equals_var(out, dest, fetched_slot(src_atom), dataptr);
        }
    } else if (src_atom->type == C_VALUE) {
        set_equals_nat(out, dest, src_atom->val, dataptr);
    }
}





// PRE: the two items being compared are at X and Y
// temp0[-]temp1[-]z[-]
// x[ temp0+
//        y[- temp0[-] temp1+ y]
//    temp0[- z+ temp0]
//    temp1[- y+ temp1]
//    y- x- ]
void emit_greater_than(FILE *out, int *dataptr) {
    set_equals_nat(out, TEMP0, 0, dataptr);
    set_equals_nat(out, TEMP1, 0, dataptr);
    set_equals_nat(out, Z, 0, dataptr);

    goto_index(out, dataptr, X);
    fputc('[', out);
        goto_index(out, dataptr, TEMP0);
        fputc('+', out);
        goto_index(out, dataptr, Y);
        fprintf(out, "[-");
            goto_index(out, dataptr, TEMP0);
                fprintf(out, "[-]");
            goto_index(out, dataptr, TEMP1);
            fputc('+', out);
            goto_index(out, dataptr, Y);
        fputc(']', out);
        goto_index(out, dataptr, TEMP0);
        fprintf(out, "[-");
            goto_index(out, dataptr, Z);
            fputc('+', out);
            goto_index(out, dataptr, TEMP0);
        fputc(']', out);
        goto_index(out, dataptr, TEMP1);
        fprintf(out, "[-");
            goto_index(out, dataptr, Y);
            fputc('+', out);
            goto_index(out, dataptr, TEMP1);
        fputc(']', out);
        goto_index(out, dataptr, Y);
        fputc('-', out);
        goto_index(out, dataptr, X);
        fputc('-', out);
    fputc(']', out);
}


// PRE: x is at X, y is at Y
void emit_equals(FILE *out, int *dataptr) {
    set_equals_nat(out, TEMP0, 1, dataptr);
    set_equals_nat(out, RESULT_INDEX, 0, dataptr);
    goto_index(out, dataptr, X);
    fputc('[', out);
        fputc('-', out);
        goto_index(out, dataptr, Y);
        fputc('-', out);
        goto_index(out, dataptr, X);
    fputc(']', out);
    // THIS LOOP SHOULD NEVER RUN, checks if X = 0
    fputc('[', out);
        goto_index(out, dataptr, TEMP0);
        set_equals_nat(out, TEMP0, 0, dataptr);
        goto_index(out, dataptr, X);
        set_equals_nat(out, X, 0, dataptr);
    fputc(']', out);
    goto_index(out, dataptr, Y);
    // checks if Y = 0, if not, X != Y
    fputc('[', out);
        goto_index(out, dataptr, TEMP0);
        set_equals_nat(out, TEMP0, 0, dataptr);
        goto_index(out, dataptr, Y);
        set_equals_nat(out, Y, 0, dataptr);
    fputc(']', out);
    goto_index(out, dataptr, TEMP0);
    fputc('[', out);
        goto_index(out, dataptr, RESULT_INDEX);
        fputc('+', out);
        goto_index(out, dataptr, TEMP0);
        fputc('-', out);
    fputc(']', out);
    goto_index(out, dataptr, RESULT_INDEX);
}

// PRE: NOT_X is the value to be notted
// POST: M[RESULT_INDEX] == !NOT_X
// y <- 1
// x [y - x [-]] 
// x <- y
// res <- x
void emit_not_helper(FILE *out, int *dataptr) {
    set_equals_nat(out, NOT_Y, 1, dataptr);
    goto_index(out, dataptr, NOT_X);
    fputc('[', out);
        goto_index(out, dataptr, NOT_Y);
        fputc('-', out);
        goto_index(out, dataptr, NOT_X);
        fprintf(out, "[-]");
    fputc(']', out);
    set_equals_var(out, NOT_X, NOT_Y, dataptr);
    set_equals_var(out, RESULT_INDEX, NOT_X, dataptr);
    set_equals_nat(out, NOT_X, 0, dataptr);
    set_equals_nat(out, NOT_Y, 0, dataptr);
}


// POST: M[RESULT_INDEX] == !M[dest]
// emit_not_cell directly reaches for the flag instead of taking in an atom
void emit_notflag(FILE *out, int dest, int *dataptr) {
    set_equals_var(out, NOT_X, dest, dataptr);
    emit_not_helper(out, dataptr);

}

// PRE: ATOM = M[IDENT] | NAT
// POST: M[RESULT_INDEX] == !IDENT | M[RESULT_INDEX] == !NAT
void emit_not(FILE *out, C_Atom *atom, int *dataptr) {
    set_equals(out, NOT_X, atom, dataptr);
    emit_not_helper(out, dataptr);
}

// PRE: r <- x, t <- y
// POST: r == x + y, t == 0
void emit_plus(FILE *out, int r, int t, int *dataptr) {
    goto_index(out, dataptr, t);
    fprintf(out, "[-");
        goto_index(out, dataptr, r);
        fputc('+', out);
        goto_index(out, dataptr, t);
    fputc(']', out);
    goto_index(out, dataptr, r);
}

// PRE: M[LHS_INDEX] and M[RHS_INDEX] == 0
// POST: dataptr is at RESULT_INDEX
void emit_expr(FILE *out, C_Expr *expr, int *dataptr) {
    set_equals_nat(out, RESULT_INDEX, 0, dataptr);
    if (expr->type == C_EXPR_INC || expr->type == C_EXPR_DEC) {
        if (expr->atom1->offset != NULL) {
            emit_arr_incdec(out, expr->atom1,
                expr->type == C_EXPR_INC ? '+' : '-', dataptr);
            return;
        }
    } else {
        // array reads must happen before any temps are set up, since
        // evaluating the index expression uses the same temps
        prefetch_arr(out, expr->atom1, 0, dataptr);
        prefetch_arr(out, expr->atom2, 1, dataptr);
        // the index expressions leave their value in RESULT_INDEX
        set_equals_nat(out, RESULT_INDEX, 0, dataptr);
    }
    switch(expr->type) {
        case C_EXPR_EQUALS:
            set_equals(out, X, expr->atom1, dataptr);
            set_equals(out, Y, expr->atom2, dataptr);

            emit_equals(out, dataptr);
            break;
        case C_EXPR_GREATER:
            set_equals(out, X, expr->atom1, dataptr);
            set_equals(out, Y, expr->atom2, dataptr);
            
            emit_greater_than(out, dataptr);
            goto_index(out, dataptr, RESULT_INDEX);
            break;
        case C_EXPR_SMALLER:
        // Just C_EXPR_GREATER but swapped
            set_equals(out, X, expr->atom2, dataptr);
            set_equals(out, Y, expr->atom1, dataptr);

            emit_greater_than(out, dataptr);
            goto_index(out, dataptr, RESULT_INDEX);
            break;
        case C_EXPR_PLUS:
        // IN PSEUDO-BF:
        // r, t <- 0
        // r <- a
        // t <- b
        // t [- r + t]
            set_equals_nat(out, RESULT_INDEX, 0, dataptr);
            set_equals_nat(out, EXPR_TEMP_INDEX, 0, dataptr);
            set_equals(out, RESULT_INDEX, expr->atom1, dataptr);
            set_equals(out, EXPR_TEMP_INDEX, expr->atom2, dataptr);
            
            emit_plus(out, RESULT_INDEX, EXPR_TEMP_INDEX, dataptr);
            break;
        case C_EXPR_NOT:
            emit_not(out, expr->atom1, dataptr);
            goto_index(out, dataptr, RESULT_INDEX);
            break; 
        case C_EXPR_SINGLE:
            if ( expr->atom1->type == C_GETCHAR) { // x = getchar();
                goto_index(out, dataptr, RESULT_INDEX);
                fputc(',', out);
            } else {
                set_equals(out, RESULT_INDEX, expr->atom1, dataptr);
            }
            break;
        case C_EXPR_MINUS:
            set_equals_nat(out, RESULT_INDEX, 0, dataptr);
            set_equals_nat(out, EXPR_TEMP_INDEX, 0, dataptr);
            set_equals(out, RESULT_INDEX, expr->atom1, dataptr);
            set_equals(out, EXPR_TEMP_INDEX, expr->atom2, dataptr);
            goto_index(out, dataptr, EXPR_TEMP_INDEX);
            fprintf(out, "[-");
                goto_index(out, dataptr, RESULT_INDEX);
                fputc('-', out);
                goto_index(out, dataptr, EXPR_TEMP_INDEX);
            fputc(']', out);
            goto_index(out, dataptr, RESULT_INDEX);
            break;
        case C_EXPR_INC:
            goto_index(out, dataptr, scalar_pos(expr->atom1->string));
            fputc('+', out);
            break;
        case C_EXPR_DEC:
            goto_index(out, dataptr, scalar_pos(expr->atom1->string));
            fputc('-', out);
            break;
        case C_EXPR_MULT:
            // x and y are working copies of atom1 and atom2
            // temp1 <- y
            // x[-
                // r <- r + y 
                // restore y 
                // x
            // ]
            set_equals(out, X, expr->atom1, dataptr);
            set_equals(out, TEMP1, expr->atom2, dataptr);
            set_equals(out, Y, expr->atom2, dataptr);
            goto_index(out, dataptr, X);

            fputc('[', out);
                fputc('-', out);
                emit_plus(out, RESULT_INDEX, Y, dataptr);
                set_equals_var(out, Y, TEMP1, dataptr);
                goto_index(out, dataptr, X);
            fputc(']', out);
            break;
        default:
            fail("Expr not recognized in emit_expr");
    }

}

void emit_putchar(FILE *out, C_Expr *expr, int *dataptr) {
    C_Atom *atom = expr->atom1;
    if (expr->type != C_EXPR_SINGLE) { // eg putchar(x + 1);
        emit_expr(out, expr, dataptr);
        goto_index(out, dataptr, RESULT_INDEX);
        fputc('.', out);
        return;
    }
    switch (atom->type) {
        case C_VALUE: // immediate value, eg putchar(5);
            set_equals_nat(out, RESULT_INDEX, atom->val, dataptr);
            fputc('.', out);
            break;
        case C_IDENT: {
            if (atom->offset != NULL) { // putchar(arr[i]);
                emit_expr(out, expr, dataptr);
                goto_index(out, dataptr, RESULT_INDEX);
                fputc('.', out);
                break;
            }
            int index = scalar_pos(atom->string);
            goto_index(out, dataptr, index);
            fputc('.', out);
            break;
        }
        default:
            fail("Putchar cannot handle this type of atom: ");
    }
}

#define IF_INDEX 3

// emit(out, block, &dataptr) for top level call
void emit_block(FILE *out, C_Block *block, int *dataptr) {
    C_Block *curr = block;

    while (curr != NULL) {
        switch (curr->stmt->type) {
            case C_STMT_DECLARE:
                break;
            case C_STMT_DECLAREARR:
                break;
            case C_STMT_SETEQUALS: {// CAN ONLY HANDLE SINGLETONS ON RHS
                C_Atom *lhs = curr->stmt->operands[0].atom;
                if (lhs->offset != NULL) {
                    emit_expr(out, curr->stmt->operands[1].expr, dataptr);
                    emit_arr_assign(out, lhs, dataptr);
                    break;
                }
                int index = scalar_pos(lhs->string);
                goto_index(out, dataptr, index);
                emit_expr(out, curr->stmt->operands[1].expr, dataptr);
                set_equals_var(out, index, RESULT_INDEX, dataptr);
                break;
            }
            case C_STMT_IF:
                emit_expr(out, curr->stmt->operands[0].expr, dataptr);
                // if M[IF_INDEX] == 0: block is skipped
                // if M[IF_INDEX] != 0: block is run 
                goto_index(out, dataptr, IF_INDEX);
                fputc('[', out);
                emit_block(out, curr->stmt->operands[1].block, dataptr);
                goto_index(out, dataptr, IF_INDEX);
                fprintf(out, "[-]]");
                break;
            case C_STMT_IFELSE:
                emit_expr(out, curr->stmt->operands[0].expr, dataptr);
                // if M[IF_INDEX] == 0: block is skipped
                // if M[IF_INDEX] != 0: block is run 

                // TRUE BLOCK
                goto_index(out, dataptr, IF_INDEX);
                fputc('[', out);
                emit_block(out, curr->stmt->operands[1].block, dataptr);
                goto_index(out, dataptr, IF_INDEX);
                fprintf(out, "[-]]");
                
                // ELSE BLOCK
                // recomputes the condition, not ideal but see if can optimize
                emit_expr(out, curr->stmt->operands[0].expr, dataptr);
                emit_notflag(out, IF_INDEX, dataptr);
                goto_index(out, dataptr, IF_INDEX);
                fputc('[', out);
                emit_block(out, curr->stmt->operands[2].block, dataptr);
                goto_index(out, dataptr, IF_INDEX);
                fprintf(out, "[-]]");
                break;
            case C_STMT_WHILE:
                emit_expr(out, curr->stmt->operands[0].expr, dataptr);
                goto_index(out, dataptr, RESULT_INDEX);
                fputc('[', out);
                emit_block(out, curr->stmt->operands[1].block, dataptr);
                emit_expr(out, curr->stmt->operands[0].expr, dataptr);
                goto_index(out, dataptr, RESULT_INDEX);
                fputc(']', out);
                break;
            case C_STMT_FOR:
                // evaluate x
                // x[
                //  code
                //  evaluate x again
                //  x
                // ]
                // for (INIT; COND; UPDATE) { body }
                // 
                // 
                // INIT
                // while (cond)  {
                //      BODY;
                //      UPDATE;
                // }      
                if (curr->stmt->operands[0].block != NULL) 
                    emit_block(out, curr->stmt->operands[0].block, dataptr);
                if (curr->stmt->operands[1].expr != NULL)
                    emit_expr(out, curr->stmt->operands[1].expr, dataptr);
                goto_index(out, dataptr, RESULT_INDEX);
                fputc('[', out);
                // body
                emit_block(out, curr->stmt->operands[3].block, dataptr); 
                // update
                emit_block(out, curr->stmt->operands[2].block, dataptr); 
                if (curr->stmt->operands[1].expr != NULL) 
                    emit_expr(out, curr->stmt->operands[1].expr, dataptr);
                goto_index(out, dataptr, RESULT_INDEX);
                fputc(']', out);
                break;
            case C_STMT_PUTCHAR:
                emit_putchar(out, curr->stmt->operands[0].expr, dataptr);
                break;
            case C_STMT_EXPR:
                emit_expr(out, curr->stmt->operands[0].expr, dataptr);
                break;
            default:
                fail("Stmt not recognized");
        }
        curr = curr->next;
    }
}

void emit(FILE *out, C_Block *block) {
    static int dataptr = 0;
    emit_block(out, block, &dataptr);
}