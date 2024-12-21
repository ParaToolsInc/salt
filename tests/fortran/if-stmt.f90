      subroutine print_message()
          print *, "i was 1"
      end subroutine print_message

      subroutine if_with_return(i)
          integer, intent (in)  :: i
          if (i == 0) return
          print *, "i was not zero"
          if (i == 1) call print_message
      end subroutine if_with_return

      program main
          implicit none
          call if_with_return(0)
          call if_with_return(1)
          call if_with_return(2)
      end program main
