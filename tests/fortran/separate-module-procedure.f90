! F2018 separate module procedures, body form (R1538):
! `module procedure foo ... end procedure foo`.  Companion fixture
! submodule_test.f90 covers the `module function foo ... end function
! foo` body form; the two parse to different parse-tree node types
! and so need separate coverage.

module mp_iface
  implicit none
  interface
    module function squared(x) result(r)
      integer, intent(in) :: x
      integer :: r
    end function squared
    module subroutine print_squared(x)
      integer, intent(in) :: x
    end subroutine print_squared
    module function bumped_sum(n) result(r)
      integer, intent(in) :: n
      integer :: r
    end function bumped_sum
    module subroutine label_terminated(x)
      integer, intent(in) :: x
    end subroutine label_terminated
  end interface
end module mp_iface


submodule (mp_iface) mp_impl
  implicit none
contains
  module procedure squared
    r = x * x
  end procedure squared

  module procedure print_squared
    print *, "x*x = ", squared(x)
  end procedure print_squared

  ! Stress: labeled CONTINUE just before `contains`, in a
  ! separate-module-procedure that itself hosts an internal helper.
  module procedure bumped_sum
    integer :: i
    r = 0
    do i = 1, n
      r = r + bump(i)
    end do
10  continue
  contains
    function bump(x) result(b)
      integer, intent(in) :: x
      integer :: b
      b = x + 1
    end function bump
  end procedure bumped_sum

  ! Stress: labeled CONTINUE immediately before `end procedure`,
  ! no internal-subprogram section.
  module procedure label_terminated
    integer :: y
    y = x + 1
    print *, "label_terminated y=", y
20  continue
  end procedure label_terminated
end submodule mp_impl


program test_separate_module_procedure
  use mp_iface, only : print_squared, bumped_sum, label_terminated
  implicit none
  call print_squared(7)
  print *, "bumped_sum(4)=", bumped_sum(4)
  call label_terminated(3)
end program test_separate_module_procedure
