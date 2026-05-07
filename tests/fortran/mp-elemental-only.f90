! Elemental separate module procedure (F2018 R1538 body form):
! ELEMENTAL attribute lives on the interface in the parent module;
! the body header `module procedure negate` carries no prefix list
! of its own.  See issue #58.

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
