test : test.c ejfat_rs.h ../prototype/python/rs_model.h
	gcc -o test test.c -O3

test_enc_dec : test_enc_dec.c ejfat_rs.h ejfat_rs_decoder.h ../prototype/python/rs_model.h
	gcc -o test_enc_dec test_enc_dec.c -O3

test_neon_dec : test_neon_dec.c ejfat_rs_neon_decoder.h ejfat_rs_neon_common.h
	gcc -o test_neon_dec test_neon_dec.c -O3

test_neon_enc : test_neon_enc.c ejfat_rs_neon_encoder.h ejfat_rs_neon_common.h
	gcc -o test_neon_enc test_neon_enc.c -O3

test_neon_enc_dec : test_neon_enc_dec.c ejfat_rs_neon_encoder.h ejfat_rs_neon_decoder.h ejfat_rs_neon_common.h
	gcc -o test_neon_enc_dec test_neon_enc_dec.c -O3

test_avx512_enc : test_avx512_enc.c ejfat_rs_avx512_encoder.h ejfat_rs_neon_common.h
	@echo "Building AVX-512 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o test_avx512_enc test_avx512_enc.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx512f -mavx512bw"; fi)

test_avx2_enc : test_avx2_enc.c ejfat_rs_avx2_encoder.h ejfat_rs_common.h
	@echo "Building AVX2 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o test_avx2_enc test_avx2_enc.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx2"; fi)

