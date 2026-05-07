! F2018 separate module procedures: parent module declares the
! interface and the submodule defines the body using the
! `module procedure foo ... end procedure foo` form (R1538).  This
! parses as Fortran::parser::SeparateModuleSubprogram, distinct from
! the `module function foo ... end function foo` form already
! exercised by submodule_test.f90.  The plugin must instrument the
! body just like any other subprogram.
!
! Standards-conforming F2018.

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

  ! Separate module procedure that has its own `contains` section
  ! with an internal helper, *and* whose execution part ends in a
  ! labeled CONTINUE just before `contains`.  Stresses both the
  ! SeparateModuleSubprogram handler and the contains-bound branch
  ! of captureBodyEndLines.
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

  ! Separate module procedure whose execution part ends in a
  ! labeled CONTINUE immediately before the end-procedure
  ! statement, with no `contains`.  Stresses the end-stmt-bound
  ! branch on a SeparateModuleSubprogram.
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
