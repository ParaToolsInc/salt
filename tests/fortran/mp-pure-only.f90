! Pure separate module procedure: PURE attribute lives on the
! interface declaration in the parent module; the body in the
! submodule is a bare `module procedure foo`.  The plugin recovers
! the attribute from the body's resolved Symbol (flang propagates
! the interface's attrs into the body symbol during semantics, also
! across .mod-file boundaries) and skips instrumentation.

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
