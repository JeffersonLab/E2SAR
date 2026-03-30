test : tests/test.c common/ejfat_rs.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test tests/test.c -O3

test_neon : tests/test_neon.c neon/ejfat_rs_neon.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_neon tests/test_neon.c -O3

test_enc_dec : tests/test_enc_dec.c common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_enc_dec tests/test_enc_dec.c -O3

test_unpacked : tests/test_unpacked.c common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_unpacked tests/test_unpacked.c -O3

test_avx2 : tests/test_avx2.c avx2/ejfat_rs_avx2.h
	@mkdir -p tests/bin
	@echo "Building AVX2 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o tests/bin/test_avx2 tests/test_avx2.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx2"; fi)

test_avx512 : tests/test_avx512.c avx512/ejfat_rs_avx512.h
	@mkdir -p tests/bin
	@echo "Building AVX-512 encoder (will use scalar fallback on non-x86_64 platforms)"
	gcc -o tests/bin/test_avx512 tests/test_avx512.c -O3 $(shell if [ "$$(uname -m)" = "x86_64" ]; then echo "-mavx512f -mavx512bw"; fi)

test_batch : tests/test_batch.c common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_batch tests/test_batch.c -O3

test_performance : tests/test_performance.c common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_performance tests/test_performance.c -O3

# Comprehensive decoder test suite (single compilation unit to avoid duplicate symbols)
test_decoder_suite : tests/test_decoder_all_tests.c tests/test_decoder_basic.c tests/test_decoder_patterns.c tests/test_decoder_data.c tests/test_decoder_errors.c tests/test_decoder_roundtrip.c tests/decoder_test_common.h common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_decoder_suite tests/test_decoder_all_tests.c -O3

# Individual modular decoder tests (can be run standalone)
test_decoder_basic : tests/test_decoder_basic.c tests/decoder_test_common.h common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_decoder_basic tests/test_decoder_basic.c -O3 -DSTANDALONE_BASIC_TEST

test_decoder_patterns : tests/test_decoder_patterns.c tests/decoder_test_common.h common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_decoder_patterns tests/test_decoder_patterns.c -O3 -DSTANDALONE_PATTERN_TEST

test_decoder_data : tests/test_decoder_data.c tests/decoder_test_common.h common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_decoder_data tests/test_decoder_data.c -O3 -DSTANDALONE_DATA_TEST

test_decoder_errors : tests/test_decoder_errors.c tests/decoder_test_common.h common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_decoder_errors tests/test_decoder_errors.c -O3 -DSTANDALONE_ERROR_TEST

test_decoder_roundtrip : tests/test_decoder_roundtrip.c tests/decoder_test_common.h common/ejfat_rs.h common/ejfat_rs_decoder.h ../prototype/python/rs_model.h
	@mkdir -p tests/bin
	gcc -o tests/bin/test_decoder_roundtrip tests/test_decoder_roundtrip.c -O3 -DSTANDALONE_ROUNDTRIP_TEST

# Convenience target to build all decoder tests
all_decoder_tests : test_decoder_suite test_decoder_basic test_decoder_patterns test_decoder_data test_decoder_errors test_decoder_roundtrip

clean :
	rm -rf tests/bin

