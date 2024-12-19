module mod_w_interfaces
    use iso_fortran_env ! Check that we can import an intrinsic module
    implicit none
    interface add
        module procedure add_int
        module procedure add_real
        module procedure add_complex
        function add_r_mat(a, b) result(c)
            real, dimension(:,:), intent(in) :: a, b
            real, dimension(size(a,1), size(a,2)) :: c
        end function
        function add_r_vec(a, b) result(c)
            real, dimension(:), intent(in) :: a, b
            real, dimension(size(a)) :: c
        end function add_r_vec
    end interface add

contains

    function add_int(a, b) result(c)
        integer, intent(in) :: a, b
        integer :: c
        c = a + b
    end function add_int
    function add_complex(a, b) result(c)
        complex, intent(in) :: a, b
        complex :: c
        c = a + b
    end function

    function add_real(a, b) result(c)
        real, intent(in) :: a, b
        real :: c
        c = a + b
    end function add_real
end module mod_w_interfaces

program test_add_overloaded
    use mod_w_interfaces, only: add
    use iso_fortran_env, only: output_unit
    implicit none

    integer, parameter :: ai = 1, bi = 2
    real, parameter :: ar = 1.0, br = 2.0
    complex, parameter :: ac = (1.0, 0.0), bc = (2.0, 1.0)
    integer :: ci
    real :: cr
    complex :: cc

    print *, "Adding integers"
    write(*, *) "testing write statement on output_unit"
    write(output_unit, *) "testing write statement on output_unit"
    ci = add(ai, bi)
    cr = add(ar, br)
    cc = add(ac, bc)

    contains
        function test_add(a,b) result(c)
            use iso_fortran_env, only: output_unit
            class(*), intent(in) :: a, b
            class(*), allocatable :: c

            interface other_interface
                function times_vec(a, b) result(c)
                    implicit none
                    real, dimension(:), intent(in) :: a, b
                    real, dimension(size(a)) :: c
                end function
                function multiply(a, b) result(c)
                    implicit none
                    class(*), intent(in) :: a, b
                    class(*), allocatable :: c
                end function multiply
            end interface other_interface

            write(output_unit, '(A)') "adding a and b"
            if ( .not. same_type_as(a, b) ) then
                print *, "Error: arguments must be of the same type"
                return
            else
                allocate(c, mold=a)
                select type(a)
                type is (integer)
                    write(output_unit, *) "a and b are integers"
                    select type(b)
                    type is (integer)
                        write(output_unit, *) "a + b: ", a, '+', b
                        c = add(a, b)
                    end select
                type is (real)
                    write(output_unit, *) "a and b are reals"
                    select type(b)
                    type is (real)
                        write(output_unit, *) "a + b: ", a, '+', b
                        c = add(a, b)
                    end select
                type is (complex)
                    write(output_unit, *) "a and b are complex"
                    select type(b)
                    type is (complex)
                        write(output_unit, *) "a + b: ", a, '+', b
                        c = add(a, b)
                    end select
                class default
                    print *, "Error: unsupported type"
                end select
            end if
        end function test_add

end program test_add_overloaded
