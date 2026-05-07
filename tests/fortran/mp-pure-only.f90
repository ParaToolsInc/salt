! Pure separate module procedure: PURE attribute lives on the
! interface declaration in the parent module; the body in the
! submodule is a bare `module procedure foo`.  SALT does not yet
! cross-reference the interface to detect pureness here, so the
! body currently DOES receive TAU instrumentation that gfortran
! will reject (SAVE in pure procedure).  The companion
! check_no_tau_mp-pure-only.f90 ctest is wired with WILL_FAIL=TRUE
! so this gap is tracked in CI without going red.  See issue #36.

module mp_pure_only_iface
  implicit none
  interface
    pure module function squared(x) result(r)
      integer, intent(in) :: x
      integer :: r
    end function squared
  end interface
end module mp_pure_only_iface


submodule (mp_pure_only_iface) mp_pure_only_impl
  implicit none
contains
  module procedure squared
    r = x * x
  end procedure squared
end submodule mp_pure_only_impl
