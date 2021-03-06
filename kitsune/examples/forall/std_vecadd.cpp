// Very simple example of using forall for a parallel vector sum.  
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <kitsune.h>

using namespace std;

const size_t VEC_SIZE = 1024 * 1024 * 256;

int main (int argc, char* argv[]) {

  vector<float> A(VEC_SIZE);
  vector<float> B(VEC_SIZE);
  vector<float> C(VEC_SIZE);

  for(auto i : A) {
    A[i] = rand() / (float)RAND_MAX;
    B[i] = rand() / (float)RAND_MAX;    
  }

  forall(auto i : C) {
    C[i] = A[i] + B[i];
  }

  size_t ti = 0;
  for(; ti < VEC_SIZE; ++ti) {
    float sum = A[ti] + B[ti];
    if (fabs(C[ti] - sum) > 1e-7f) 
      break; // whoops...
  }
 
  fprintf(stdout, "Result = %s (%ld, %ld)\n",
	  (ti == VEC_SIZE) ? "PASS" : "FAIL",
	  ti, VEC_SIZE);
  
  return 0;
}

