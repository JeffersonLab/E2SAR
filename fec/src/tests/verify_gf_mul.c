#include <stdio.h>
#include <stdlib.h>
#include "../common/ejfat_rs.h"

int main() {
    // Verify the GF multiplications from the debug output
    uint8_t matrix[8] = {0xF, 0xC, 0xF, 0x2, 0xE, 0x2, 0xD, 0xC};
    uint8_t vector[8] = {0x0, 0x2, 0x4, 0x1, 0x8, 0xA, 0xC, 0xE};
    uint8_t expected_products[8] = {0x0, 0xB, 0x9, 0x2, 0x9, 0x7, 0x3, 0x4};
    
    printf("Manual GF(16) multiplication verification:\n");
    uint8_t result = 0;
    int all_match = 1;
    for (int i = 0; i < 8; i++) {
        uint8_t prod = gf_mul(matrix[i], vector[i]);
        char *match = (prod == expected_products[i]) ? "OK" : "MISMATCH";
        printf("[%d] gf_mul(0x%X, 0x%X) = 0x%X (debug showed: 0x%X) %s\n", 
               i, matrix[i], vector[i], prod, expected_products[i], match);
        if (prod != expected_products[i]) all_match = 0;
        result ^= prod;
    }
    
    printf("\nXOR of all products: 0x%X\n", result);
    printf("Debug output showed: 0x9\n");
    printf("Expected result (to recover 0x6): 0x6\n");
    printf("\nAll products match debug output: %s\n", all_match ? "YES" : "NO");
    
    if (result != 0x6) {
        printf("\nCONCLUSION: The inverse matrix for position 3 is INCORRECT!\n");
        printf("The gf_matrix_invert() function has a bug.\n");
    } else {
        printf("\nWEIRD: Manual calculation gives correct result but decoder doesn't!\n");
    }
    
    return 0;
}
