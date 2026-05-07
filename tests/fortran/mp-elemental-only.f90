! Elemental separate module procedure: ELEMENTAL attribute lives on
! the interface in the parent module; body in the submodule is a
! bare `module procedure foo`.  Same gap as mp-pure-only.f90: SALT
! does not yet cross-reference the interface, so the body receives
! TAU instrumentation today.  Companion check_no_tau_mp-elemental-only.f90
! is wired with WILL_FAIL=TRUE.  See issue #36.

module mp_elemental_only_iface
  implicit none
  interface
    elemental module function negate(x) result(r)
      integer, intent(in) :: x
      integer :: r
    end function negate
  end interface
end module mp_elemental_only_iface


submodule (mp_elemental_only_iface) mp_elemental_only_impl
  implicit none
contains
  module procedure negate
    r = -x
  end procedure negate
end submodule mp_elemental_only_impl
