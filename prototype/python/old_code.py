   H_new = poly_matrix_invert(G_error)
   print(" comparing matrix inverse solutions ")
   print("H from numpy")
   print(H)
   print("H from yatish")   
   print(H_new)
   
   m_corrected = poly_matrix_vector_mul(np.array(H_new),m_rx)
   print(f"m      = {np.array(m)}")
   print(f"m_corr = {np.array(m_corrected)}")
   print(f"c_tx   = {np.array(c_matrix)}")
   print(f"c_rx   = {np.array(c_rx)}")

