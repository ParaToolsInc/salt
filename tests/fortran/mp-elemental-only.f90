! Elemental separate module procedure: ELEMENTAL attribute lives on
! the interface in the parent module; body in the submodule is a
! bare `module procedure foo`.  The plugin skips instrumentation
! via the body's resolved Symbol (flang propagates the interface's
! attrs into the body symbol during semantics).

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
