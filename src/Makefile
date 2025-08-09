test : test.c ejfat_rs.h ../prototype/python/rs_model.h
	gcc -o test test.c -O3

test_enc_dec : test_enc_dec.c ejfat_rs.h ejfat_rs_decoder.h ../prototype/python/rs_model.h
	gcc -o test_enc_dec test_enc_dec.c -O3

test_neon_dec : test_neon_dec.c ejfat_rs_neon_decoder.h
	gcc -o test_neon_dec test_neon_dec.c -O3

test_neon_enc : test_neon_enc.c ejfat_rs_neon_encoder.h
	gcc -o test_neon_enc test_neon_enc.c -O3

