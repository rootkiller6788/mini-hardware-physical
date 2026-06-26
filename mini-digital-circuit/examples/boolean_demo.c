#include <stdio.h>
#include "boolean_algebra.h"
#include "logic_gate.h"

int main(void) {
    printf("===== Boolean Expression Demo =====\n\n");
    BoolExpr* a = bool_expr_var(0);
    BoolExpr* b = bool_expr_var(1);
    BoolExpr* c = bool_expr_var(2);
    BoolExpr* not_a = bool_expr_create(BOOL_NOT, a, NULL);
    BoolExpr* a_and_b = bool_expr_create(BOOL_AND, a, b);
    BoolExpr* expr = bool_expr_create(BOOL_OR, a_and_b, not_a);
    printf("Expression: "); bool_expr_print(expr); printf("\n");
    printf("Nodes: %d, Depth: %d\n\n", bool_expr_node_count(expr), bool_expr_depth(expr));
    bool values[3];
    printf("A B C | Result\n------|-------\n");
    for (int i = 0; i < 8; i++) {
        values[0] = (i >> 2) & 1; values[1] = (i >> 1) & 1; values[2] = i & 1;
        printf("%d %d %d |   %d\n", values[0], values[1], values[2], bool_expr_eval(expr, values));
    }
    printf("\n===== K-Map Demo =====\n");
    KMap km = kmap_create(3);
    kmap_set_cell(&km, 0, false); kmap_set_cell(&km, 1, false);
    kmap_set_cell(&km, 2, true);  kmap_set_cell(&km, 3, true);
    kmap_set_cell(&km, 4, false); kmap_set_cell(&km, 5, true);
    kmap_set_cell(&km, 6, true);  kmap_set_cell(&km, 7, true);
    kmap_print(&km);
    SOP simplified = kmap_simplify(&km);
    printf("Simplified SOP: "); sop_print(&simplified);
    printf("\n===== Shannon Expansion =====\n");
    SOP s = sop_create(2);
    sop_add_term(&s, 3, 1); sop_add_term(&s, 3, 2);
    printf("Shannon verified: %s\n", shannon_verify(&s, 0) ? "YES" : "NO");
    printf("\nBoolean algebra demo complete.\n");
    return 0;
}
