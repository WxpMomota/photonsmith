#pragma once
// A film-ready family, inferred from the Nomai statue/suit references. These
// are original characters, not named game characters or canonical age charts.
#include "nomai_explorer.h"

namespace cartoon { namespace nomai {
enum class LifeStage { Child, Adolescent, AdultA, AdultB, Elder };
struct FamilyLook {
    const char *label;
    double stature, build, head, horns, beard, lean;
    Vec3 cloth, fur;
    bool masked;
};
inline FamilyLook familyLook(LifeStage stage) {
    switch (stage) {
        case LifeStage::Child: return {"Child",0.67,1.12,1.25,0.25,0.0,0.0,{1.12,1.0,0.85},{1.05,0.95,0.85},false};
        case LifeStage::Adolescent: return {"Adolescent",0.84,0.96,1.12,0.65,0.25,0.0,{0.85,1.0,1.15},{1.0,1.0,1.0},true};
        case LifeStage::AdultB: return {"Adult B",0.97,1.05,1.0,1.0,0.85,0.0,{1.28,0.88,0.72},{1.0,0.9,0.82},true};
        case LifeStage::Elder: return {"Elder",0.95,1.0,1.02,1.05,1.25,7.0,{0.72,0.83,0.72},{1.10,1.30,1.60},false};
        default: return {"Adult A",1.0,1.0,1.0,1.0,1.0,0.0,{1.0,1.0,1.0},{1.0,1.0,1.0},true};
    }
}
inline ExplorerPose familyPose(LifeStage stage, double time=0.5) {
    const FamilyLook look=familyLook(stage);
    ExplorerPose p=standPose(time);
    p.staff=0; p.maskShown=look.masked; p.hood=look.masked;
    p.headScale=look.head; p.hornGrowth=look.horns; p.beardScale=look.beard;
    p.robeTint=look.cloth; p.furTint=look.fur; p.lean=look.lean;
    p.headPitch=stage==LifeStage::Child ? -7.0 : stage==LifeStage::Elder ? 7.0 : 2.0;
    p.handR={-0.22,0.74,0.10};
    return p;
}
inline Mat4 familyFrame(LifeStage stage, const Mat4 &at) {
    const FamilyLook look=familyLook(stage);
    return mul(at,scale(look.stature*look.build,look.stature,look.stature*look.build));
}
inline std::string familyMemberText(LifeStage stage,const Mat4 &at,double time=0.5,double detail=0.014) {
    return explorerText(familyPose(stage,time),familyFrame(stage,at),detail);
}
} }
