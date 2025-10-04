# RS Decoder Comprehensive Test Suite

A modular test suite for the Reed-Solomon decoder implementation, providing comprehensive coverage of functionality, error handling, and edge cases.

## Test Organization

### Test Modules

1. **test_decoder_basic.c** - Basic recovery functionality
   - No loss scenario (all 10 packets)
   - Recovery threshold tests (exactly 8, 9 packets)
   - Below minimum tests (should fail)
   - Maximum recoverable loss (2 packets)

2. **test_decoder_patterns.c** - Erasure pattern variations
   - Data packet loss only
   - Parity packet loss
   - Mixed loss patterns
   - Alternating patterns (even/odd positions)
   - First/last N packets lost
   - Random loss patterns (50 trials)

3. **test_decoder_data.c** - Data pattern variations
   - Boundary values (all zeros, all 0x0F)
   - Sequential patterns [0,1,2,3,...]
   - Alternating patterns [15,0,15,0,...]
   - Incremental patterns
   - Random data (100 trials)
   - Edge cases (single non-zero symbol)

4. **test_decoder_errors.c** - Error handling and edge cases
   - Insufficient data (3-7 erasures, all should fail)
   - Exactly at threshold (2 erasures)
   - Zero erasures (trivial case)
   - All combinations (28 combinations of 2 erasures)
   - Decoder consistency (all implementations agree)
   - Single erasure at each position

5. **test_decoder_roundtrip.c** - Encoder/decoder integration
   - Single round-trip encode/decode
   - Multiple round-trips (100 cycles)
   - Re-encode verification
   - Systematic encoding property
   - Encoder compatibility (rs_encode, fast_rs_encode, neon_rs_encode)
   - Stress test (1000 random cycles)

### Common Utilities

**decoder_test_common.h** - Shared test infrastructure
- Test result tracking and reporting
- Data verification functions
- Random test data generation
- Helper functions for codeword manipulation

## Building the Tests

### Build All Tests
```bash
cd src
make all_decoder_tests
```

### Build Individual Test Modules
```bash
cd src

# Build specific test module
make test_decoder_basic
make test_decoder_patterns
make test_decoder_data
make test_decoder_errors
make test_decoder_roundtrip
```

### Build Comprehensive Suite
```bash
cd src
make test_decoder_suite
```

### Clean Test Executables
```bash
cd src
make clean
```

## Running the Tests

### Run Comprehensive Suite (All Tests)
```bash
cd src
./tests/test_decoder_suite
```

### Run Comprehensive Suite (Selective)
```bash
# Run specific test categories
./tests/test_decoder_suite --basic
./tests/test_decoder_suite --patterns
./tests/test_decoder_suite --data
./tests/test_decoder_suite --errors
./tests/test_decoder_suite --roundtrip

# Run multiple categories
./tests/test_decoder_suite --basic --patterns

# Show help
./tests/test_decoder_suite --help
```

### Run Individual Standalone Tests
```bash
cd src

# Run individual test modules (faster, focused testing)
./tests/test_decoder_basic
./tests/test_decoder_patterns
./tests/test_decoder_data
./tests/test_decoder_errors
./tests/test_decoder_roundtrip
```

## Test Output

Each test provides clear pass/fail status:
```
========== Basic Recovery Tests ==========
  [PASS] No loss - all 10 packets received
  [PASS] Minimum threshold - exactly 8 packets
  [PASS] Above minimum - 9 packets available
  [PASS] Below minimum - 7 packets (should fail)
  [PASS] Maximum loss - 2 packets (multiple patterns)

========== Basic Recovery Tests Summary ==========
Total:  5
Passed: 5
Failed: 0
Success rate: 100.0%
=====================================
```

## Test Coverage

### What is Tested

✅ **Recovery Capabilities**
- No loss (trivial case)
- 1 erasure recovery
- 2 erasures recovery (maximum)
- Failure with >2 erasures

✅ **Loss Patterns**
- Data packet loss only
- Parity packet loss
- Mixed patterns
- First/last packets
- Alternating patterns
- Random patterns

✅ **Data Patterns**
- Boundary values (0x00, 0x0F)
- Sequential data
- Alternating patterns
- Random data
- Edge cases

✅ **Error Handling**
- Insufficient packets
- Invalid erasure counts
- All possible 2-erasure combinations (28 patterns)

✅ **Implementation Consistency**
- rs_decode_erasures()
- rs_decode_table_lookup()
- neon_rs_decode_table_lookup()

✅ **Encoder/Decoder Integration**
- Round-trip verification
- Re-encoding consistency
- Cross-encoder compatibility (rs_encode, fast_rs_encode, neon_rs_encode)
- Stress testing (1000+ cycles)

### Test Statistics

- **Basic Tests**: 5 tests
- **Pattern Tests**: 7 tests (including 50 random trials)
- **Data Tests**: 7 tests (including 100 random trials)
- **Error Tests**: 6 tests (including 28 combinations)
- **Round-Trip Tests**: 6 tests (including 1000 stress cycles)

**Total**: 31+ distinct test scenarios, 1000+ random verification cycles

## Usage Examples

### Quick Smoke Test
```bash
# Just basic functionality
make test_decoder_basic && ./tests/test_decoder_basic
```

### Comprehensive Validation
```bash
# All tests
make test_decoder_suite && ./tests/test_decoder_suite
```

### Performance-Focused Testing
```bash
# Run stress test only
make test_decoder_roundtrip && ./tests/test_decoder_roundtrip
```

### Development Workflow
```bash
# Run only the test category you're working on
make test_decoder_errors && ./tests/test_decoder_errors
```

## Adding New Tests

1. Add test function to appropriate module:
```c
void test_my_new_feature(rs_model *rs, rs_decode_table *table, test_results *results) {
    // Test implementation
    int passed = /* verification */;
    record_test(results, passed, "My new test description");
}
```

2. Call from module's `run_*_tests()` function:
```c
int run_error_tests(rs_model *rs, rs_decode_table *table) {
    // ...
    test_my_new_feature(rs, table, &results);
    // ...
}
```

3. Rebuild and run:
```bash
make test_decoder_errors && ./tests/test_decoder_errors
```

## Notes

- All tests use RS(10,8) configuration (8 data + 2 parity symbols, GF(16))
- Random tests use seeded RNG for reproducibility
- Tests verify both successful decoding AND correct data recovery
- Modular design allows running specific test categories independently
- No external dependencies beyond standard C library
