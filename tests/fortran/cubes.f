      program sum_of_cubes 
      implicit none
      integer :: h, t, u 
      ! this program prints all 3-digit numbers that 
      ! equal the sum of the cubes of their digits. 
      do h = 1, 9 
        do t = 0, 9 
          do u = 0, 9 
          if (100*h + 10*t + u == h**3 + t**3 + u**3) then
             print "(3I1)", h, t, u 
          endif
          end do 
        end do 
      end do 
      end program sum_of_cubes
