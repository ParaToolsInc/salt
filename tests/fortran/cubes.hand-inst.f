!c34567 Cubes program
      program sum_of_cubes 
        integer profiler(2) / 0, 0 /
        save profiler
        integer :: H, T, U 
        call TAU_PROFILE_INIT()
        call TAU_PROFILE_TIMER(profiler, 'program sum_of_cubes')
        call TAU_PROFILE_START(profiler)
        call TAU_PROFILE_SET_NODE(0)
      ! This program prints all 3-digit numbers that 
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
      call TAU_PROFILE_STOP(profiler)
      end program sum_of_cubes
