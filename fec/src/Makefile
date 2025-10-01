# ============================================================================
# EJFAT Reed-Solomon FEC - Makefile for src_tidy
# ============================================================================
# This Makefile builds and runs all test programs for the reorganized codebase
# ============================================================================

CC = gcc
CFLAGS = -O3 -Wall
COMMON_HEADERS = common/ejfat_rs_tables.h common/ejfat_rs_common.h
COMMON_DIR = common
NEON_DIR = neon
AVX2_DIR = avx2
AVX512_DIR = avx512
TESTS_DIR = tests

# Detect architecture for SIMD support
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    AVX2_FLAGS = -mavx2
    AVX512_FLAGS = -mavx512f -mavx512bw
else
    AVX2_FLAGS =
    AVX512_FLAGS =
endif

# Test executables
TESTS = test test_enc_dec \
        test_neon_enc test_neon_dec test_neon_enc_dec \
        test_avx2_enc test_avx2_dec test_avx2_enc_dec test_avx2_optimization \
        test_avx512_enc

# Default target: build all tests
all: $(TESTS)

# ============================================================================
# Basic Tests (common/)
# ============================================================================

test: $(TESTS_DIR)/test.c $(COMMON_DIR)/ejfat_rs.h $(COMMON_HEADERS)
	$(CC) -o $@ $(TESTS_DIR)/test.c $(CFLAGS) -I.

test_enc_dec: $(TESTS_DIR)/test_enc_dec.c $(COMMON_DIR)/ejfat_rs.h $(COMMON_DIR)/ejfat_rs_decoder.h $(COMMON_HEADERS)
	$(CC) -o $@ $(TESTS_DIR)/test_enc_dec.c $(CFLAGS) -I.

# ============================================================================
# ARM NEON Tests (neon/)
# ============================================================================

test_neon_enc: $(TESTS_DIR)/test_neon_enc.c $(NEON_DIR)/ejfat_rs_neon_encoder.h $(NEON_DIR)/ejfat_rs_neon_common.h $(COMMON_HEADERS)
	$(CC) -o $@ $(TESTS_DIR)/test_neon_enc.c $(CFLAGS) -I.

test_neon_dec: $(TESTS_DIR)/test_neon_dec.c $(NEON_DIR)/ejfat_rs_neon_decoder.h $(NEON_DIR)/ejfat_rs_neon_common.h $(COMMON_HEADERS)
	$(CC) -o $@ $(TESTS_DIR)/test_neon_dec.c $(CFLAGS) -I.

test_neon_enc_dec: $(TESTS_DIR)/test_neon_enc_dec.c $(NEON_DIR)/ejfat_rs_neon_encoder.h $(NEON_DIR)/ejfat_rs_neon_decoder.h $(NEON_DIR)/ejfat_rs_neon_common.h $(COMMON_HEADERS)
	$(CC) -o $@ $(TESTS_DIR)/test_neon_enc_dec.c $(CFLAGS) -I.

# ============================================================================
# x86 AVX2 Tests (avx2/)
# ============================================================================

test_avx2_enc: $(TESTS_DIR)/test_avx2_enc.c $(AVX2_DIR)/ejfat_rs_avx2_encoder.h $(COMMON_HEADERS)
	@echo "Building AVX2 encoder (will use scalar fallback on non-x86_64 platforms)"
	$(CC) -o $@ $(TESTS_DIR)/test_avx2_enc.c $(CFLAGS) $(AVX2_FLAGS) -I.

test_avx2_dec: $(TESTS_DIR)/test_avx2_dec.c $(AVX2_DIR)/ejfat_rs_avx2_decoder.h $(COMMON_HEADERS)
	@echo "Building AVX2 decoder (will use scalar fallback on non-x86_64 platforms)"
	$(CC) -o $@ $(TESTS_DIR)/test_avx2_dec.c $(CFLAGS) $(AVX2_FLAGS) -I.

test_avx2_enc_dec: $(TESTS_DIR)/test_avx2_enc_dec.c $(AVX2_DIR)/ejfat_rs_avx2_encoder.h $(AVX2_DIR)/ejfat_rs_avx2_decoder.h $(COMMON_HEADERS)
	@echo "Building AVX2 encoder/decoder integrated test (will use scalar fallback on non-x86_64 platforms)"
	$(CC) -o $@ $(TESTS_DIR)/test_avx2_enc_dec.c $(CFLAGS) $(AVX2_FLAGS) -I.

test_avx2_optimization: $(TESTS_DIR)/test_avx2_optimization.c $(AVX2_DIR)/ejfat_rs_avx2_encoder.h $(COMMON_HEADERS)
	@echo "Building AVX2 encoder optimization comparison test (will use scalar fallback on non-x86_64 platforms)"
	$(CC) -o $@ $(TESTS_DIR)/test_avx2_optimization.c $(CFLAGS) $(AVX2_FLAGS) -I.

# ============================================================================
# x86 AVX512 Tests (avx512/)
# ============================================================================

test_avx512_enc: $(TESTS_DIR)/test_avx512_enc.c $(AVX512_DIR)/ejfat_rs_avx512_encoder.h $(COMMON_HEADERS)
	@echo "Building AVX-512 encoder (will use scalar fallback on non-x86_64 platforms)"
	$(CC) -o $@ $(TESTS_DIR)/test_avx512_enc.c $(CFLAGS) $(AVX512_FLAGS) -I.

# ============================================================================
# Run Targets
# ============================================================================

# Run all tests sequentially
run-all: all
	@echo "========================================"
	@echo "Running all tests..."
	@echo "========================================"
	@echo "\n--- Basic Test ---"
	-./test
	@echo "\n--- Encode/Decode Test ---"
	-./test_enc_dec
	@echo "\n--- NEON Encoder Test ---"
	-./test_neon_enc
	@echo "\n--- NEON Decoder Test ---"
	-./test_neon_dec
	@echo "\n--- NEON Encode/Decode Test ---"
	-./test_neon_enc_dec
	@echo "\n--- AVX2 Encoder Test ---"
	-./test_avx2_enc
	@echo "\n--- AVX2 Decoder Test ---"
	-./test_avx2_dec
	@echo "\n--- AVX2 Encode/Decode Test ---"
	-./test_avx2_enc_dec
	@echo "\n--- AVX2 Optimization Test ---"
	-./test_avx2_optimization
	@echo "\n--- AVX512 Encoder Test ---"
	-./test_avx512_enc
	@echo "\n========================================"
	@echo "All tests completed"
	@echo "========================================"

# Run individual test suites
run-basic: test test_enc_dec
	@echo "Running basic tests..."
	./test
	./test_enc_dec

run-neon: test_neon_enc test_neon_dec test_neon_enc_dec
	@echo "Running NEON tests..."
	./test_neon_enc
	./test_neon_dec
	./test_neon_enc_dec

run-avx2: test_avx2_enc test_avx2_dec test_avx2_enc_dec test_avx2_optimization
	@echo "Running AVX2 tests..."
	./test_avx2_enc
	./test_avx2_dec
	./test_avx2_enc_dec
	./test_avx2_optimization

run-avx512: test_avx512_enc
	@echo "Running AVX512 tests..."
	./test_avx512_enc

# ============================================================================
# Utility Targets
# ============================================================================

clean:
	rm -f $(TESTS) *.o

.PHONY: all clean run-all run-basic run-neon run-avx2 run-avx512
