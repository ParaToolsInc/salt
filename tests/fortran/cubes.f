cc34567 Cubes program
      PROGRAM SUM_OF_CUBES 
      INTEGER :: H, T, U 
      ! This program prints all 3-digit numbers that 
      ! equal the sum of the cubes of their digits. 
      DO H = 1, 9 
        DO T = 0, 9 
          DO U = 0, 9 
          IF (100*H + 10*T + U == H**3 + T**3 + U**3) THEN
             PRINT "(3I1)", H, T, U 
	  ENDIF
          END DO 
        END DO 
      END DO 
      END PROGRAM SUM_OF_CUBES
