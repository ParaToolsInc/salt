#include <Profile/Profiler.h>

#line 1 "hello.c"
#include <stdio.h>
#include <stdlib.h>

void foo() {

#line 4

TAU_PROFILE_TIMER(tautimer, "void foo() C [{hello.c} {4,1}-{13,1}]", " ", TAU_USER);
	TAU_PROFILE_START(tautimer);


#line 4
{
	printf("42\n");
	int x = 2;
	if (x == 2) {
		exit(0);
	}
	else {
		
#line 11
{  TAU_PROFILE_STOP(tautimer); return; }

#line 11

	}

#line 13

}
	
	TAU_PROFILE_STOP(tautimer);


#line 13
}

int main(int argc, char* argv[]) {

#line 15

TAU_PROFILE_TIMER(tautimer, "int main(int, char **) C [{hello.c} {15,1}-{19,1}]", " ", TAU_DEFAULT);
  TAU_INIT(&argc, &argv); 
#ifndef TAU_MPI
#ifndef TAU_SHMEM
  TAU_PROFILE_SET_NODE(0);
#endif /* TAU_SHMEM */
#endif /* TAU_MPI */
	TAU_PROFILE_START(tautimer);


#line 15
{
	printf("hello world\n");
	foo();
	
#line 18
{ int tau_ret_val =  0;  TAU_PROFILE_STOP(tautimer); return (tau_ret_val); }

#line 18


#line 19

}
	
	TAU_PROFILE_STOP(tautimer);


#line 19
}
