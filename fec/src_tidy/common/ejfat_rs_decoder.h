#ifndef __ejfat_rs_decoder_h
#define __ejfat_rs_decoder_h

#include "ejfat_rs.h"
#include <arm_neon.h>

// --------------------------------------------------------------------------
// Reed-Solomon Decoder Implementation
// --------------------------------------------------------------------------

// Galois field division: a / b
char gf_div(char a, char b) {
  if (a == 0 || b == 0) {
    return 0;
  }
  
  char exp_a = _ejfat_rs_gf_exp_seq[a];
  char exp_b = _ejfat_rs_gf_exp_seq[b];
  char diff = (exp_a - exp_b + 15) % 15;  // Add 15 to handle negative values
  
  return _ejfat_rs_gf_log_seq[diff];
}

// Matrix inversion using Gauss-Jordan elimination over GF(16)
int poly_matrix_invert(rs_poly_matrix *m, rs_poly_matrix **m_inv) {
  if (m->rows != m->cols) {
    printf("Error: Matrix must be square for inversion\n");
    return -1;
  }
  
  int n = m->rows;
  
  // Create augmented matrix [M | I]
  rs_poly_matrix *aug = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + n * sizeof(rs_poly_vector *));
  aug->rows = n;
  aug->cols = 2 * n;
  
  // Allocate memory for augmented matrix rows
  for (int i = 0; i < n; i++) {
    aug->val[i] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
    aug->val[i]->len = 2 * n;
    
    // Copy original matrix
    for (int j = 0; j < n; j++) {
      aug->val[i]->val[j] = m->val[i]->val[j];
    }
    
    // Add identity matrix
    for (int j = 0; j < n; j++) {
      aug->val[i]->val[n + j] = (i == j) ? 1 : 0;
    }
  }
  
  // Gauss-Jordan elimination
  for (int i = 0; i < n; i++) {
    // Find pivot
    int pivot_row = i;
    for (int k = i + 1; k < n; k++) {
      if (aug->val[k]->val[i] != 0) {
        pivot_row = k;
        break;
      }
    }
    
    // Check for singular matrix
    if (aug->val[pivot_row]->val[i] == 0) {
      printf("Error: Matrix is singular, cannot invert\n");
      // Free allocated memory
      for (int k = 0; k < n; k++) {
        free(aug->val[k]);
      }
      free(aug);
      return -1;
    }
    
    // Swap rows if needed
    if (pivot_row != i) {
      rs_poly_vector *temp = aug->val[i];
      aug->val[i] = aug->val[pivot_row];
      aug->val[pivot_row] = temp;
    }
    
    // Scale pivot row
    char pivot = aug->val[i]->val[i];
    for (int j = 0; j < 2 * n; j++) {
      aug->val[i]->val[j] = gf_div(aug->val[i]->val[j], pivot);
    }
    
    // Eliminate column
    for (int k = 0; k < n; k++) {
      if (k != i && aug->val[k]->val[i] != 0) {
        char factor = aug->val[k]->val[i];
        for (int j = 0; j < 2 * n; j++) {
          aug->val[k]->val[j] = gf_sum(aug->val[k]->val[j], 
                                      gf_mul(factor, aug->val[i]->val[j]));
        }
      }
    }
  }
  
  // Extract inverse matrix from right half
  *m_inv = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + n * sizeof(rs_poly_vector *));
  (*m_inv)->rows = n;
  (*m_inv)->cols = n;
  for (int i = 0; i < n; i++) {
    (*m_inv)->val[i] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
    (*m_inv)->val[i]->len = n;
    for (int j = 0; j < n; j++) {
      (*m_inv)->val[i]->val[j] = aug->val[i]->val[n + j];
    }
  }
  
  // Free augmented matrix memory
  for (int i = 0; i < n; i++) {
    free(aug->val[i]);
  }
  free(aug);
  
  return 0;
}

// Free inverse matrix memory
void free_poly_matrix(rs_poly_matrix *m) {
  for (int i = 0; i < m->rows; i++) {
    free(m->val[i]);
  }
}

// Decode with known erasure locations (up to 2*t erasures)
int rs_decode_erasures(rs_model *rs, rs_poly_vector *received, 
                       int *erasure_locations, int num_erasures, 
                       rs_poly_vector *decoded) {
  
  if (num_erasures > rs->p) {
    printf("Error: Too many erasures (%d), can only correct up to %d\n", 
           num_erasures, rs->p);
    return -1;
  }
  
  // Method 1: Create G* by removing erased rows, then invert
  if (num_erasures > 0) {
    // Create G* matrix by removing erased symbol rows
    rs_poly_matrix *g_star = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + rs->n * sizeof(rs_poly_vector *));
    g_star->rows = rs->n;
    g_star->cols = rs->n;
    
    int valid_row = 0;
    for (int i = 0; i < rs->n; i++) {
      // Check if this row is not erased
      int is_erased = 0;
      for (int j = 0; j < num_erasures; j++) {
        if (i == erasure_locations[j]) {
          is_erased = 1;
          break;
        }
      }
      
      if (!is_erased) {
        g_star->val[valid_row] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
        g_star->val[valid_row]->len = rs->n;
        // Use identity matrix for data rows
        for (int col = 0; col < rs->n; col++) {
          g_star->val[valid_row]->val[col] = (i == col) ? 1 : 0;
        }
        valid_row++;
      }
    }
    
    // Add parity constraints to make it square
    for (int i = 0; i < num_erasures; i++) {
      g_star->val[valid_row] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
      g_star->val[valid_row]->len = rs->n;
      for (int col = 0; col < rs->n; col++) {
        g_star->val[valid_row]->val[col] = rs->Genc->val[i]->val[col];
      }
      valid_row++;
    }
    
    // Invert G*
    rs_poly_matrix *g_inv;
    if (poly_matrix_invert(g_star, &g_inv) != 0) {
      printf("Error: Could not invert G* matrix\n");
      free_poly_matrix(g_star);
      free(g_star);
      return -1;
    }
    
    // Create received vector without erased symbols, adding parity
    rs_poly_vector rx_reduced;
    rx_reduced.len = rs->n;
    valid_row = 0;
    
    for (int i = 0; i < rs->n; i++) {
      int is_erased = 0;
      for (int j = 0; j < num_erasures; j++) {
        if (i == erasure_locations[j]) {
          is_erased = 1;
          break;
        }
      }
      
      if (!is_erased) {
        rx_reduced.val[valid_row] = received->val[i];
        valid_row++;
      }
    }
    
    // Add parity symbols
    for (int i = 0; i < num_erasures; i++) {
      rx_reduced.val[valid_row] = received->val[rs->n + i];
      valid_row++;
    }
    
    // Decode: decoded = G_inv * rx_reduced
    poly_matrix_vector_mul(g_inv, &rx_reduced, decoded);
    
    // Clean up
    free_poly_matrix(g_star);
    free(g_star);
    free_poly_matrix(g_inv);
    free(g_inv);
    
  } else {
    // No erasures, just copy data symbols
    decoded->len = rs->n;
    for (int i = 0; i < rs->n; i++) {
      decoded->val[i] = received->val[i];
    }
  }
  
  return 0;
}

// Simple decode for the case where we substitute parity for erased data
int rs_decode_substitute(rs_model *rs, rs_poly_vector *received, 
                        int *erasure_locations, int num_erasures, 
                        rs_poly_vector *decoded) {
  
  if (num_erasures > rs->p) {
    printf("Error: Too many erasures (%d), can only correct up to %d\n", 
           num_erasures, rs->p);
    return -1;
  }
  
  // Copy received data symbols
  decoded->len = rs->n;
  for (int i = 0; i < rs->n; i++) {
    decoded->val[i] = received->val[i];
  }
  
  if (num_erasures > 0) {
    // Replace erased data symbols with parity symbols
    for (int i = 0; i < num_erasures; i++) {
      if (erasure_locations[i] < rs->n) {
        decoded->val[erasure_locations[i]] = received->val[rs->n + i];
      }
    }
    
    // Create modified G matrix (replace erased rows with parity rows)
    rs_poly_matrix *g_mod = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + rs->n * sizeof(rs_poly_vector *));
    g_mod->rows = rs->n;
    g_mod->cols = rs->n;
    
    for (int i = 0; i < rs->n; i++) {
      g_mod->val[i] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
      g_mod->val[i]->len = rs->n;
      
      // Check if this row should be replaced with parity
      int replace_idx = -1;
      for (int j = 0; j < num_erasures; j++) {
        if (i == erasure_locations[j]) {
          replace_idx = j;
          break;
        }
      }
      
      if (replace_idx >= 0) {
        // Use parity constraint
        for (int col = 0; col < rs->n; col++) {
          g_mod->val[i]->val[col] = rs->Genc->val[replace_idx]->val[col];
        }
      } else {
        // Use identity matrix for data row
        for (int col = 0; col < rs->n; col++) {
          g_mod->val[i]->val[col] = (i == col) ? 1 : 0;
        }
      }
    }
    
    // Invert modified G matrix
    rs_poly_matrix *g_inv;
    if (poly_matrix_invert(g_mod, &g_inv) != 0) {
      printf("Error: Could not invert modified G matrix\n");
      free_poly_matrix(g_mod);
      free(g_mod);
      return -1;
    }
    
    // Decode: result = G_inv * decoded
    rs_poly_vector temp = *decoded;
    poly_matrix_vector_mul(g_inv, &temp, decoded);
    
    // Clean up
    free_poly_matrix(g_mod);
    free(g_mod);
    free_poly_matrix(g_inv);
    free(g_inv);
  }
  
  return 0;
}

// Table-based decoder for specific erasure patterns (optimized version)
typedef struct {
  int erasure_pattern[2];  // Up to 2 erasures for RS(10,8)
  int num_erasures;        // Number of erasures in this pattern
  char inv_matrix[8][8];   // Pre-computed 8x8 inverse matrix
  int valid;               // 1 if this entry is valid, 0 otherwise
} rs_decode_table_entry;

// Decoder table structure
typedef struct {
  rs_decode_table_entry *entries;  // Dynamic array of table entries
  int size;                        // Number of entries in table
  int capacity;                    // Allocated capacity
} rs_decode_table;

// Initialize decoder table with all possible erasure patterns
int init_rs_decode_table(rs_model *rs, rs_decode_table *table) {
  printf("Initializing RS decoder table...\n");
  
  // Calculate total number of possible erasure patterns
  // For RS(10,8): no erasures (1) + single erasures (8) + double erasures (8*7/2 = 28)
  int max_patterns = 1 + rs->n + (rs->n * (rs->n - 1)) / 2;
  
  table->entries = malloc(max_patterns * sizeof(rs_decode_table_entry));
  if (!table->entries) {
    printf("Error: Could not allocate memory for decode table\n");
    return -1;
  }
  
  table->capacity = max_patterns;
  table->size = 0;
  
  // Pattern 0: No erasures (identity - just copy data)
  rs_decode_table_entry *entry = &table->entries[table->size];
  entry->num_erasures = 0;
  entry->erasure_pattern[0] = -1;
  entry->erasure_pattern[1] = -1;
  entry->valid = 1;
  
  // Identity matrix for no erasures
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      entry->inv_matrix[i][j] = (i == j) ? 1 : 0;
    }
  }
  table->size++;
  
  // Generate all single erasure patterns
  for (int e1 = 0; e1 < rs->n; e1++) {
    entry = &table->entries[table->size];
    entry->num_erasures = 1;
    entry->erasure_pattern[0] = e1;
    entry->erasure_pattern[1] = -1;
    entry->valid = 0;  // Will be computed below
    
    // Create modified G matrix for this erasure pattern
    rs_poly_matrix *g_mod = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + rs->n * sizeof(rs_poly_vector *));
    g_mod->rows = rs->n;
    g_mod->cols = rs->n;
    
    for (int i = 0; i < rs->n; i++) {
      g_mod->val[i] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
      g_mod->val[i]->len = rs->n;
      
      if (i == e1) {
        // Replace erased row with parity constraint (use Genc transpose)
        for (int j = 0; j < rs->n; j++) {
          g_mod->val[i]->val[j] = rs->Genc->val[0]->val[j];
        }
      } else {
        // Use original data row (only the identity part)
        for (int j = 0; j < rs->n; j++) {
          g_mod->val[i]->val[j] = (i == j) ? 1 : 0;
        }
      }
    }
    
    // Compute inverse matrix
    rs_poly_matrix *g_inv;
    if (poly_matrix_invert(g_mod, &g_inv) == 0) {
      // Copy inverse to table
      for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
          entry->inv_matrix[i][j] = g_inv->val[i]->val[j];
        }
      }
      entry->valid = 1;
      free_poly_matrix(g_inv);
      free(g_inv);
    }
    
    free_poly_matrix(g_mod);
    free(g_mod);
    table->size++;
  }
  
  // Generate all double erasure patterns
  for (int e1 = 0; e1 < rs->n - 1; e1++) {
    for (int e2 = e1 + 1; e2 < rs->n; e2++) {
      entry = &table->entries[table->size];
      entry->num_erasures = 2;
      entry->erasure_pattern[0] = e1;
      entry->erasure_pattern[1] = e2;
      entry->valid = 0;
      
      // Create modified G matrix for this erasure pattern
      rs_poly_matrix *g_mod = (rs_poly_matrix *) malloc(sizeof(rs_poly_matrix) + rs->n * sizeof(rs_poly_vector *));
      g_mod->rows = rs->n;
      g_mod->cols = rs->n;
      
      for (int i = 0; i < rs->n; i++) {
        g_mod->val[i] = (rs_poly_vector *) malloc(sizeof(rs_poly_vector));
        g_mod->val[i]->len = rs->n;
        
        if (i == e1) {
          // Replace first erased row with first parity constraint
          for (int j = 0; j < rs->n; j++) {
            g_mod->val[i]->val[j] = rs->Genc->val[0]->val[j];
          }
        } else if (i == e2) {
          // Replace second erased row with second parity constraint
          for (int j = 0; j < rs->n; j++) {
            g_mod->val[i]->val[j] = rs->Genc->val[1]->val[j];
          }
        } else {
          // Use original data row (identity part)
          for (int j = 0; j < rs->n; j++) {
            g_mod->val[i]->val[j] = (i == j) ? 1 : 0;
          }
        }
      }
      
      // Compute inverse matrix
      rs_poly_matrix *g_inv;
      if (poly_matrix_invert(g_mod, &g_inv) == 0) {
        // Copy inverse to table
        for (int i = 0; i < 8; i++) {
          for (int j = 0; j < 8; j++) {
            entry->inv_matrix[i][j] = g_inv->val[i]->val[j];
          }
        }
        entry->valid = 1;
        free_poly_matrix(g_inv);
        free(g_inv);
      }
      
      free_poly_matrix(g_mod);
      free(g_mod);
      table->size++;
    }
  }
  
  printf("Decoder table initialized with %d patterns\n", table->size);
  
  // Count valid patterns
  int valid_count = 0;
  for (int i = 0; i < table->size; i++) {
    if (table->entries[i].valid) valid_count++;
  }
  printf("Valid patterns: %d/%d\n", valid_count, table->size);
  
  return 0;
}

// Free decoder table memory
void free_rs_decode_table(rs_decode_table *table) {
  if (table->entries) {
    free(table->entries);
    table->entries = NULL;
  }
  table->size = 0;
  table->capacity = 0;
}

// Fast table-based decoder (when table is available)
int rs_decode_table_lookup(rs_decode_table *table, rs_poly_vector *received,
                          int *erasure_locations, int num_erasures,
                          rs_poly_vector *decoded) {
  
  if (num_erasures > 2) {
    printf("Error: Table-based decoder supports up to 2 erasures\n");
    return -1;
  }
  
  // Look up erasure pattern in table
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry *entry = &table->entries[t];
    
    if (!entry->valid || entry->num_erasures != num_erasures) {
      continue;
    }
    
    // Check if erasure pattern matches
    int match = 1;
    if (num_erasures == 0) {
      match = 1;  // No erasures to match
    } else if (num_erasures == 1) {
      match = (entry->erasure_pattern[0] == erasure_locations[0]);
    } else if (num_erasures == 2) {
      // Check both orderings for double erasures
      match = ((entry->erasure_pattern[0] == erasure_locations[0] && 
                entry->erasure_pattern[1] == erasure_locations[1]) ||
               (entry->erasure_pattern[0] == erasure_locations[1] && 
                entry->erasure_pattern[1] == erasure_locations[0]));
    }
    
    if (match) {
      // Found matching pattern, use pre-computed inverse
      rs_poly_vector rx_modified;
      rx_modified.len = 8;
      
      // Create received vector with parity substitutions
      for (int i = 0; i < 8; i++) {
        rx_modified.val[i] = received->val[i];
      }
      
      // Replace erased symbols with parity symbols
      for (int i = 0; i < num_erasures; i++) {
        if (erasure_locations[i] < 8) {
          rx_modified.val[erasure_locations[i]] = received->val[8 + i];
        }
      }
      
      // Apply pre-computed inverse matrix
      decoded->len = 8;
      for (int i = 0; i < 8; i++) {
        decoded->val[i] = 0;
        for (int j = 0; j < 8; j++) {
          decoded->val[i] = gf_sum(decoded->val[i], 
                                   gf_mul(entry->inv_matrix[i][j], 
                                          rx_modified.val[j]));
        }
      }
      
      return 0;
    }
  }
  
  printf("Warning: Erasure pattern not found in table\n");
  return -1;
}

// NEON-optimized table-based decoder for maximum performance
int neon_rs_decode_table_lookup(rs_decode_table *table, rs_poly_vector *received,
                                int *erasure_locations, int num_erasures,
                                rs_poly_vector *decoded) {
  
  // --- For speed reasons, we do not check assumptions. But the following must be true:
  //     received must be exactly 10 symbols (8 data + 2 parity)
  //     decoded must be exactly 8 data symbols  
  //     num_erasures must be <= 2
  // ------------------------------------------------------------------------------------
  
  if (num_erasures > 2) {
    return -1;  // Fast failure for too many erasures
  }
  
  // Load GF lookup tables into NEON registers
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;  
  
  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);   // t[0] to t[7]
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);   // t[8] to t[15]

  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);   // t[0] to t[7]
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);   // t[8] to t[15]
  
  // Look up erasure pattern in table (optimized linear search)
  rs_decode_table_entry *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry *candidate = &table->entries[t];
    
    if (!candidate->valid || candidate->num_erasures != num_erasures) {
      continue;
    }
    
    // Fast pattern matching
    int match = 0;
    if (num_erasures == 0) {
      match = 1;
    } else if (num_erasures == 1) {
      match = (candidate->erasure_pattern[0] == erasure_locations[0]);
    } else if (num_erasures == 2) {
      match = ((candidate->erasure_pattern[0] == erasure_locations[0] && 
                candidate->erasure_pattern[1] == erasure_locations[1]) ||
               (candidate->erasure_pattern[0] == erasure_locations[1] && 
                candidate->erasure_pattern[1] == erasure_locations[0]));
    }
    
    if (match) {
      entry = candidate;
      break;
    }
  }
  
  if (!entry) {
    return -1;  // Pattern not found
  }
  
  // Create received vector with parity substitutions
  uint8_t rx_modified[8] __attribute__((aligned(16)));
  for (int i = 0; i < 8; i++) {
    rx_modified[i] = received->val[i];
  }
  
  // Replace erased symbols with parity symbols
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      rx_modified[erasure_locations[i]] = received->val[8 + i];
    }
  }
  
  // Load received vector into NEON register
  uint8x8_t rx_vec = vld1_u8(rx_modified);
  
  // Apply pre-computed inverse matrix using NEON operations
  decoded->len = 8;
  
  // Process all 8 output symbols
  for (int i = 0; i < 8; i++) {
    // Load matrix row into NEON register
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    
    // Convert matrix elements to exponent space for GF multiplication
    uint8x8_t matrix_row_exp = vtbl2_u8(exp_table, matrix_row);
    
    // Convert received vector to exponent space  
    uint8x8_t rx_exp = vtbl2_u8(exp_table, rx_vec);
    
    // Perform GF multiplication in exponent space: exp(a*b) = exp(a) + exp(b) mod 15
    uint8x8_t prod_exp = vadd_u8(matrix_row_exp, rx_exp);
    
    // Handle modulo 15 operation
    uint8x8_t mod = vdup_n_u8(15);
    uint8x8_t mask = vcge_u8(prod_exp, mod);  // mask where prod_exp >= 15
    uint8x8_t mod15 = vand_u8(mod, mask);     // 15 where needed, 0 elsewhere
    prod_exp = vsub_u8(prod_exp, mod15);      // subtract 15 where needed
    
    // Convert back to normal space: log(exp(x)) = x
    uint8x8_t prod_normal = vtbl2_u8(log_table, prod_exp);
    
    // Handle zero elements (where matrix_row[j] == 0 or rx_vec[j] == 0)
    uint8x8_t zero_vec = vdup_n_u8(0);
    uint8x8_t matrix_zero_mask = vceq_u8(matrix_row, zero_vec);
    uint8x8_t rx_zero_mask = vceq_u8(rx_vec, zero_vec);
    uint8x8_t zero_mask = vorr_u8(matrix_zero_mask, rx_zero_mask);
    
    // Set result to 0 where either operand was 0
    prod_normal = vbic_u8(prod_normal, zero_mask);
    
    // Sum all elements using horizontal add (XOR for GF addition)
    uint8_t result_array[8];
    vst1_u8(result_array, prod_normal);
    
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= result_array[j];  // GF addition is XOR
    }
    
    decoded->val[i] = result;
  }
  
  return 0;
}

// Vectorized GF multiplication for 8 elements at once
static inline uint8x8_t neon_gf_mul_vec(uint8x8_t a, uint8x8_t b, 
                                        uint8x8x2_t exp_table, uint8x8x2_t log_table) {
  // Handle zero case
  uint8x8_t zero_vec = vdup_n_u8(0);
  uint8x8_t a_zero_mask = vceq_u8(a, zero_vec);
  uint8x8_t b_zero_mask = vceq_u8(b, zero_vec);
  uint8x8_t zero_mask = vorr_u8(a_zero_mask, b_zero_mask);
  
  // Convert to exponent space
  uint8x8_t a_exp = vtbl2_u8(exp_table, a);
  uint8x8_t b_exp = vtbl2_u8(exp_table, b);
  
  // Add exponents (mod 15)
  uint8x8_t sum_exp = vadd_u8(a_exp, b_exp);
  uint8x8_t mod = vdup_n_u8(15);
  uint8x8_t mask = vcge_u8(sum_exp, mod);
  uint8x8_t mod15 = vand_u8(mod, mask);
  sum_exp = vsub_u8(sum_exp, mod15);
  
  // Convert back to normal space
  uint8x8_t result = vtbl2_u8(log_table, sum_exp);
  
  // Apply zero mask
  result = vbic_u8(result, zero_mask);
  
  return result;
}

// Optimized NEON decoder with full vectorization (experimental)
int neon_rs_decode_table_lookup_v2(rs_decode_table *table, rs_poly_vector *received,
                                   int *erasure_locations, int num_erasures,
                                   rs_poly_vector *decoded) {
  
  if (num_erasures > 2) {
    return -1;
  }
  
  // Load GF lookup tables
  uint8x8x2_t exp_table;
  uint8x8x2_t log_table;
  exp_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[0]);
  exp_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_exp_seq[8]);
  log_table.val[0] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[0]);
  log_table.val[1] = vld1_u8((unsigned const char *) &_ejfat_rs_gf_log_seq[8]);
  
  // Find matching table entry (same as before)
  rs_decode_table_entry *entry = NULL;
  for (int t = 0; t < table->size; t++) {
    rs_decode_table_entry *candidate = &table->entries[t];
    
    if (!candidate->valid || candidate->num_erasures != num_erasures) {
      continue;
    }
    
    int match = 0;
    if (num_erasures == 0) {
      match = 1;
    } else if (num_erasures == 1) {
      match = (candidate->erasure_pattern[0] == erasure_locations[0]);
    } else if (num_erasures == 2) {
      match = ((candidate->erasure_pattern[0] == erasure_locations[0] && 
                candidate->erasure_pattern[1] == erasure_locations[1]) ||
               (candidate->erasure_pattern[0] == erasure_locations[1] && 
                candidate->erasure_pattern[1] == erasure_locations[0]));
    }
    
    if (match) {
      entry = candidate;
      break;
    }
  }
  
  if (!entry) {
    return -1;
  }
  
  // Prepare received vector with substitutions
  uint8_t rx_modified[8] __attribute__((aligned(16)));
  for (int i = 0; i < 8; i++) {
    rx_modified[i] = received->val[i];
  }
  for (int i = 0; i < num_erasures; i++) {
    if (erasure_locations[i] < 8) {
      rx_modified[erasure_locations[i]] = received->val[8 + i];
    }
  }
  
  uint8x8_t rx_vec = vld1_u8(rx_modified);
  
  // Vectorized matrix-vector multiplication
  decoded->len = 8;
  for (int i = 0; i < 8; i++) {
    uint8x8_t matrix_row = vld1_u8((uint8_t *)entry->inv_matrix[i]);
    uint8x8_t prod_vec = neon_gf_mul_vec(matrix_row, rx_vec, exp_table, log_table);
    
    // Horizontal XOR reduction
    uint8_t temp[8];
    vst1_u8(temp, prod_vec);
    char result = 0;
    for (int j = 0; j < 8; j++) {
      result ^= temp[j];
    }
    
    decoded->val[i] = result;
  }
  
  return 0;
}

#endif