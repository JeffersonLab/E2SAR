test : test.c ejfat_rs.h ../prototype/python/rs_model.h
	gcc -o test test.c -O3

test_neon : test_neon.c ejfat_rs_neon.h
	gcc -o test_neon test_neon.c -O3

test_enc_dec : test_enc_dec.c ejfat_rs.h ejfat_rs_decoder.h ../prototype/python/rs_model.h
	gcc -o test_enc_dec test_enc_dec.c -O3

test_avx2 : test_avx2.c ejfat_rs_avx2.h
	@echo "Building AVX2 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o test_avx2 test_avx2.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx2"; fi)

test_avx512 : test_avx512.c ejfat_rs_avx512.h
	@echo "Building AVX-512 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o test_avx512 test_avx512.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx512f -mavx512bw"; fi)

