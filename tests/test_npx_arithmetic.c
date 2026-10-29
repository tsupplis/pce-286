/* Exact independently specified binary values. GPL-2.0-or-later. */
#include <cpu/e8087/arithmetic.h>
#include <stdio.h>
int main(void) {
    e87_real80_t one={UINT64_C(0x8000000000000000),0x3fff};
    e87_real80_t tiny={UINT64_C(0x8000000000000000),0x3fc3},r;
    unsigned flags=0,fail=0;
    fail+=!e87_add80(one,one,80,0,&r,&flags);
    fail+=r.sign_exponent!=0x4000 || r.significand!=one.significand || flags!=0;
    fail+=!e87_add80(one,tiny,80,0,&r,&flags);
    fail+=r.sign_exponent!=0x3fff || r.significand!=UINT64_C(0x8000000000000008) || flags!=0;
    fail+=!e87_add80(one,tiny,64,0,&r,&flags);
    fail+=r.significand!=one.significand || flags!=1;
    fail+=!e87_add80(one,tiny,64,3,&r,&flags);
    fail+=r.significand!=UINT64_C(0x8000000000000800) || flags!=1;
    fail+=!e87_add80(one,tiny,80,0,&r,&flags);
    fail+=r.significand!=UINT64_C(0x8000000000000008) || flags!=0;
    fail+=e87_add80(one,tiny,17,0,&r,&flags)!=0;
    /* Cross-check the new integer path against SoftFloat on the shared normal
     * domain. This cannot validate early unnormal representations; those have
     * independent raw-value vectors in the execution test. */
    uint64_t seed=UINT64_C(0x932759bce0123497);
    const unsigned precisions[3]={32,64,80};
    for(unsigned k=0;k<1000;++k) for(unsigned pc=0;pc<3;++pc)
    for(unsigned rc=0;rc<4;++rc) {
        seed=seed*UINT64_C(6364136223846793005)+1;
        e87_real80_t a={seed|UINT64_C(0x8000000000000000),(0x4000+(k%130))|((k&1)<<15)};
        seed=seed*UINT64_C(6364136223846793005)+1;
        e87_real80_t b={seed|UINT64_C(0x8000000000000000),0x4000|((k&2)<<14)},want,got;
        unsigned wf,gf;
        if(!e87_add80(a,b,precisions[pc],rc,&want,&wf) ||
           !e87_add80_unnormal(a,b,precisions[pc],rc,&got,&gf) ||
           want.significand!=got.significand || want.sign_exponent!=got.sign_exponent || wf!=gf) ++fail;
    }
    printf("NPX arithmetic adapter: %u failures\n",fail);
    return fail!=0;
}
