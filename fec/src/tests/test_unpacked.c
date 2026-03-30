#include <stdio.h>
#include <stdlib.h>

#include "../common/ejfat_rs.h"
#include "../common/ejfat_rs_decoder.h"

void test_unpacked_encode() {
    printf("\n=============== Testing Unpacked Encoder ===============\n");

    rs_model *rs = init_rs();
    if (!rs) {
        printf("Failed to initialize RS model\n");
        return;
    }

    // Test data symbols: d0=1, d1=2, d2=3, d3=4, d4=5, d5=6, d6=7, d7=8
    char d0, d1, d2, d3, d4, d5, d6, d7, p0, p1;

    printf("Encoding data: 1 2 3 4 5 6 7 8\n");

    // Test basic encoder
    d0=1; d1=2; d2=3; d3=4; d4=5; d5=6; d6=7; d7=8;
    rs_encode_unpacked(rs, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1);
    printf("rs_encode_unpacked:      p0=%d, p1=%d\n", p0, p1);

    // Test fast encoder
    d0=1; d1=2; d2=3; d3=4; d4=5; d5=6; d6=7; d7=8;
    fast_rs_encode_unpacked(rs, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1);
    printf("fast_rs_encode_unpacked: p0=%d, p1=%d\n", p0, p1);

    // Test NEON encoder
    d0=1; d1=2; d2=3; d3=4; d4=5; d5=6; d6=7; d7=8;
    neon_rs_encode_unpacked(rs, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1);
    printf("neon_rs_encode_unpacked: p0=%d, p1=%d\n", p0, p1);

    free_rs(rs);
    printf("=============== Unpacked Encoder Test Complete ===============\n");
}

void test_unpacked_decode() {
    printf("\n=============== Testing Unpacked Decoder ===============\n");

    rs_model *rs = init_rs();
    if (!rs) {
        printf("Failed to initialize RS model\n");
        return;
    }

    // Initialize decoder table
    rs_decode_table decode_table;
    if (init_rs_decode_table(rs, &decode_table) != 0) {
        printf("Failed to initialize decoder table\n");
        free_rs(rs);
        return;
    }

    // Encode data to get parity symbols
    char d0, d1, d2, d3, d4, d5, d6, d7, p0, p1;
    d0=1; d1=2; d2=3; d3=4; d4=5; d5=6; d6=7; d7=8;
    neon_rs_encode_unpacked(rs, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1);

    printf("Original data:   1 2 3 4 5 6 7 8\n");
    printf("Parity symbols:  p0=%d, p1=%d\n", p0, p1);

    // Test 1: No erasures
    printf("\n--- Test 1: No erasures ---\n");
    d0=1; d1=2; d2=3; d3=4; d4=5; d5=6; d6=7; d7=8;
    int erasures[] = {};

    if (rs_decode_erasures_unpacked(rs, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1,
                                     erasures, 0) == 0) {
        printf("Decoded: %d %d %d %d %d %d %d %d\n",
               d0, d1, d2, d3, d4, d5, d6, d7);

        if (d0==1 && d1==2 && d2==3 && d3==4 &&
            d4==5 && d5==6 && d6==7 && d7==8) {
            printf("Decoding PASSED\n");
        } else {
            printf("Decoding FAILED\n");
        }
    }

    // Test 2: Single erasure at position 3
    printf("\n--- Test 2: Single erasure at position 3 ---\n");
    d0=1; d1=2; d2=3; d3=0; d4=5; d5=6; d6=7; d7=8;
    int erasures2[] = { 3 };

    if (rs_decode_table_lookup_unpacked(&decode_table, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1,
                                         erasures2, 1) == 0) {
        printf("Decoded: %d %d %d %d %d %d %d %d\n",
               d0, d1, d2, d3, d4, d5, d6, d7);

        if (d0==1 && d1==2 && d2==3 && d3==4 &&
            d4==5 && d5==6 && d6==7 && d7==8) {
            printf("Table-based decoding PASSED\n");
        } else {
            printf("Table-based decoding FAILED\n");
        }
    }

    // Test 3: Two erasures using NEON decoder
    printf("\n--- Test 3: Two erasures (positions 1, 5) using NEON ---\n");
    d0=1; d1=0; d2=3; d3=4; d4=5; d5=0; d6=7; d7=8;
    int erasures3[] = { 1, 5 };

    if (neon_rs_decode_table_lookup_unpacked(&decode_table, &d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &p0, &p1,
                                              erasures3, 2) == 0) {
        printf("Decoded: %d %d %d %d %d %d %d %d\n",
               d0, d1, d2, d3, d4, d5, d6, d7);

        if (d0==1 && d1==2 && d2==3 && d3==4 &&
            d4==5 && d5==6 && d6==7 && d7==8) {
            printf("NEON table-based decoding PASSED\n");
        } else {
            printf("NEON table-based decoding FAILED\n");
        }
    }

    free_rs_decode_table(&decode_table);
    free_rs(rs);
    printf("=============== Unpacked Decoder Test Complete ===============\n");
}

int main() {
    printf("Reed-Solomon Unpacked API Test Program\n");
    printf("========================================\n");

    test_unpacked_encode();
    test_unpacked_decode();

    return 0;
}
