! Pure separate module procedure (F2018 R1538 body form): PURE
! attribute lives on the interface declaration in the parent module;
! the body header `module procedure squared` carries no prefix list
! of its own.  See issue #58.

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
