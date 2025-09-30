test : tests/test.c common/ejfat_rs.h ../prototype/python/rs_model.h
	gcc -o tests/test tests/test.c -O3

test_neon : tests/test_neon.c neon/ejfat_rs_neon.h
	gcc -o tests/test_neon tests/test_neon.c -O3

test_enc_dec : tests/test_enc_dec.c common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	gcc -o tests/test_enc_dec tests/test_enc_dec.c -O3

test_avx2 : tests/test_avx2.c avx2/ejfat_rs_avx2.h
	@echo "Building AVX2 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o tests/test_avx2 tests/test_avx2.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx2"; fi)

test_avx512 : tests/test_avx512.c avx512/ejfat_rs_avx512.h
	@echo "Building AVX-512 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o tests/test_avx512 tests/test_avx512.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx512f -mavx512bw"; fi)

