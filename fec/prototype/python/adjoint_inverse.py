def poly_matrix_invert(M):

   #---------------------------------------------------------------
   # 1. Build the matrix of co-factors.  +/- Det of sub matricies
   # 2. Transpose the matrix of co-factors.  Call it an adjoint
   # 3. Det of original matrix
   # 4. Scale the adjoint by the Det from (3.) above
   #---------------------------------------------------------------   

   det = gf_poly_matrix_determinant(M)
   print(f"det = {det}")

   cofactors = np.zeros_like(M)
   for col in range(len(M)):
      for row in range(len(M)):
         m = M.copy()
         m = np.delete(np.delete(m,row,0),col,1)
         print("m = ")
         print(m)
         cofactors[row][col] = gf_poly_matrix_determinant(m)
         cofactors[row][col] = gf_div(cofactors[row][col],det)
         print("cofactors = ")
         print(cofactors)

   print("cofactors are ")
   print(cofactors)
   adjoint = cofactors.transpose()
   print("adjoint is ")
   print(adjoint)

   return adjoint

