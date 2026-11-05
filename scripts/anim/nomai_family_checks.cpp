#include "../../src/shared/cartoon/film_kinds.h"
#include <iostream>
#include <stdexcept>
int main() {
    using namespace cartoon;
    const nomai::LifeStage ages[]={nomai::LifeStage::Child,nomai::LifeStage::Adolescent,nomai::LifeStage::AdultA,nomai::LifeStage::AdultB,nomai::LifeStage::Elder};
    for(auto age:ages) {
        const auto kind=film_kinds::explorerKind(age,true);
        film_file::CharacterMoment m;
        m.frame=mul(translate(3.0,0.5,-2.0),rotate({0,1,0},37));
        m.moving=true;m.feetLocked=true;
        for(int s=0;s<2;++s) m.lockedFeet[s]=kind.footAt(0.5,s);
        for(double step:{0.0,0.5,1.0,1.5,2.0}) {
            m.steps=step;
            const auto feet=kind.feet(m);
            if(feet.size()!=2) throw std::runtime_error("wrong foot count");
            for(int s=0;s<2;++s)
                if(length(feet[s].at-transformPoint(m.frame,m.lockedFeet[s]))>1e-8) throw std::runtime_error("scaled foot lock slides");
        }
        if(!std::isfinite(kind.height)||kind.height<0.8) throw std::runtime_error("invalid camera height");
        std::cout<<"PASS "<<nomai::familyLook(age).label<<": locked feet stay fixed through translated/rotated walk poses\n";
    }
    if(nomai::familyLook(nomai::LifeStage::Child).head<=nomai::familyLook(nomai::LifeStage::AdultA).head)
        throw std::runtime_error("child lost its larger relative head");
}
