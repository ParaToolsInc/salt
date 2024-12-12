cc hello.f
cc --------
cc-----------------------------------------------------------------------------

      subroutine HELLOWORLD(iVal)
        integer iVal

cc Do something here...
     print *, "Iteration = ", iVal
cc       HelloWorld = iVal
      end

      program main
        integer i


      print *, "test program"

        do 10, i = 1, 10
        call HELLOWORLD(i)
10      continue
      end
