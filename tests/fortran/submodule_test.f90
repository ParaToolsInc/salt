! Exercises Fortran 2008 submodules: module declares interfaces for
! "module" procedures, the body lives in a submodule.  Issue #33 asks
! whether SALT instruments the bodies (in the submodule) without
! attempting to inject TAU calls into the bodyless interface
! declarations in the parent module.
!
! Standard-conforming F2008+. Same source file, compiled top-to-bottom:
! parent module first, submodule next, program last.

module geometry
  implicit none
  private
  public :: area_circle, print_area

  interface
    module function area_circle(r) result(a)
      real, intent(in) :: r
      real :: a
    end function area_circle

    module subroutine print_area(r)
      real, intent(in) :: r
    end subroutine print_area
  end interface
end module geometry


submodule (geometry) geometry_impl
  implicit none
  real, parameter :: pi = 3.14159265
contains
  module function area_circle(r) result(a)
    real, intent(in) :: r
    real :: a
    a = pi * r * r
  end function area_circle

  module subroutine print_area(r)
    real, intent(in) :: r
    print *, "Area for r =", r, " is ", area_circle(r)
  end subroutine print_area
end submodule geometry_impl


program test_submodule
  use geometry, only : print_area
  implicit none
  call print_area(2.0)
end program test_submodule
