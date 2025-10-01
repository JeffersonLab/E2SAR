#include <stdio.h>
#include <stdlib.h>
#include "../common/ejfat_rs.h"

int main() {
    rs_model *rs = init_rs();
    
    // Test upper nibbles from sequential pattern  
    rs_poly_vector upper_data = {.len = 8, .val = {0, 2, 4, 6, 8, 10, 12, 14}};
    rs_poly_vector upper_parity = {.len = 2};
    
    printf("Encoding upper nibbles: ");
    for (int i = 0; i < 8; i++) printf("%X ", upper_data.val[i]);
    printf("\n");
    
    neon_rs_encode(rs, &upper_data, &upper_parity);
    
    printf("Upper parity computed: %X %X\n", upper_parity.val[0], upper_parity.val[1]);
    printf("Expected from dual-nibble encoder: 1 9\n\n");
    
    // Now test lower nibbles
    rs_poly_vector lower_data = {.len = 8, .val = {1, 3, 5, 7, 9, 11, 13, 15}};
    rs_poly_vector lower_parity = {.len = 2};
    
    printf("Encoding lower nibbles: ");
    for (int i = 0; i < 8; i++) printf("%X ", lower_data.val[i]);
    printf("\n");
    
    neon_rs_encode(rs, &lower_data, &lower_parity);
    
    printf("Lower parity computed: %X %X\n", lower_parity.val[0], lower_parity.val[1]);
    printf("Expected from dual-nibble encoder: F D\n");
    
    free_rs(rs);
    return 0;
}
