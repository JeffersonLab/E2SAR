#include <stdio.h>

static const char _ejfat_rs_gf_exp_seq[16] = { 15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12 };
static const char _ejfat_rs_G[8][10] = {
    {1,0,0,0,0,0,0,0,14,5},
    {0,1,0,0,0,0,0,0,6,9},
    {0,0,1,0,0,0,0,0,14,4},
    {0,0,0,1,0,0,0,0,9,13},
    {0,0,0,0,1,0,0,0,7,8},
    {0,0,0,0,0,1,0,0,1,1},
    {0,0,0,0,0,0,1,0,15,5},
    {0,0,0,0,0,0,0,1,6,8}
};

int main() {
    printf("Genc_exp should be:\n");

    // Parity row 0 (column 8 of G)
    printf("Row 0: { ");
    for (int row = 0; row < 8; row++) {
        printf("%d", _ejfat_rs_gf_exp_seq[_ejfat_rs_G[row][8]]);
        if (row < 7) printf(", ");
    }
    printf(" }\n");

    // Parity row 1 (column 9 of G)
    printf("Row 1: { ");
    for (int row = 0; row < 8; row++) {
        printf("%d", _ejfat_rs_gf_exp_seq[_ejfat_rs_G[row][9]]);
        if (row < 7) printf(", ");
    }
    printf(" }\n");

    return 0;
}
