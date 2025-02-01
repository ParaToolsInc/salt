program main
  implicit none

  integer :: my_a, my_b
  call get_input(my_a, my_b)
  print*, my_a, ' + ', my_b, ' = ', add(my_a, my_b)
contains
  function add(a, b)
    integer, intent(in) :: a, b
    integer :: add

    add = a + b

    return
  end function add
  subroutine get_input(a, b)
    integer, intent(out) :: a, b
    print*, 'Add a and b.'
    print*, 'First enter a, then enter b:'
    ! for testing set values instead of reading form stdin
    ! read*, my_a, my_b
    a = 7
    b = 6
  end subroutine get_input
end program main
