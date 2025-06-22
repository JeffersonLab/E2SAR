 static const char _ejfat_rs_gf_log_seq[16] = { 1,2,4,8,3,6,12,11,5,10,7,14,15,13,9,0 }; 
 static const char _ejfat_rs_gf_exp_seq[16] = { 15,0,1,4,2,8,5,10,3,14,9,7,6,13,11,12 }; 


 static const int _ejfat_rs_n = 8; // data words 
 static const int _ejfat_rs_p = 6; // parity words  
 static const int _ejfat_rs_k = 14; // message words = data+parity 

 static const char _ejfat_rs_G[8][14] = {
    {1,0,0,0,0,0,0,0,15,1,13,7,5,13},
    {0,1,0,0,0,0,0,0,11,11,13,3,10,7},
    {0,0,1,0,0,0,0,0,3,2,3,8,4,7},
    {0,0,0,1,0,0,0,0,3,10,10,6,15,9},
    {0,0,0,0,1,0,0,0,5,11,1,5,15,11},
    {0,0,0,0,0,1,0,0,2,11,10,7,14,8},
    {0,0,0,0,0,0,1,0,15,9,5,8,15,2},
    {0,0,0,0,0,0,0,1,7,9,3,12,10,12} 
 };

