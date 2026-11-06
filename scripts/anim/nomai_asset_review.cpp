#include "../../src/shared/cartoon/nomai_family.h"
#include "../../src/shared/cartoon/nomai_game_assets.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace cartoon;
using namespace cartoon::nomai;
namespace na=cartoon::nomai::assets;

static std::string header(Vec3 eye,Vec3 target,int width=1000,int height=800,double fov=36,bool space=false) {
    std::string out="Film \"rgb\" \"integer xresolution\" ["+std::to_string(width)+"] \"integer yresolution\" ["+std::to_string(height)+"]\n";
    out+="LookAt "+fmt3(eye)+" "+fmt3(target)+" 0 1 0\nCamera \"perspective\" \"float fov\" ["+std::to_string(fov)+"]\nWorldBegin\n";
    out+=space ? "LightSource \"infinite\" \"rgb L\" [0.015 0.015 0.02]\n" : "LightSource \"infinite\" \"rgb L\" [0.28 0.28 0.28]\n";
    out+="AttributeBegin\nLightSource \"distant\" \"point3 from\" [3 7 5] \"point3 to\" [0 0 0] \"rgb L\" [2.5 2.3 2.0]\nAttributeEnd\n";
    return out;
}
static std::string floorAt(double y) {
    return "AttributeBegin\nMaterial \"diffuse\" \"rgb reflectance\" [0.16 0.19 0.21]\nShape \"trianglemesh\" \"integer indices\" [0 2 1 0 3 2] \"point3 P\" [-100 "+std::to_string(y)+" -100 100 "+std::to_string(y)+" -100 100 "+std::to_string(y)+" 100 -100 "+std::to_string(y)+" 100]\nAttributeEnd\n";
}
static void write(const std::filesystem::path &path,const std::string &s) {
    std::ofstream out(path,std::ios::binary);out<<s;if(!out) throw std::runtime_error("cannot write scene");
}
static void check(const na::Model &m) {
    if(m.pieces.empty()) throw std::runtime_error("empty model");
    for(const na::Piece &p:m.pieces) {
        if(p.mesh.verts.size()!=p.mesh.normals.size()) throw std::runtime_error("normal count");
        for(Vec3 v:p.mesh.verts) if(!std::isfinite(length(transformPoint(p.frame,v)))) throw std::runtime_error("nonfinite vertex");
        for(Vec3 n:p.mesh.normals) if(!std::isfinite(length(n)) || length(n)<0.9) throw std::runtime_error("invalid normal");
        for(auto t:p.mesh.tris) for(int k:t) if(k<0 || k>=static_cast<int>(p.mesh.verts.size())) throw std::runtime_error("index out of bounds");
    }
}
static void checkSweptPlate() {
    const Mesh m=na::sweptPlate({{0,0,0},{0,1,0},{0,2,0}},0.4,0.2);
    for(auto t:m.tris) {
        const Vec3 a=m.verts[t[0]],b=m.verts[t[1]],c=m.verts[t[2]];
        const Vec3 centre=(a+b+c)*(1.0/3.0);
        if(dot(cross(b-a,c-a),centre-Vec3{0,1,0})<=0)
            throw std::runtime_error("swept plate has inward-facing surface");
    }
    const Vec3 a=na::planetColour(0,normalize(Vec3{-1,0.2,1e-9}));
    const Vec3 b=na::planetColour(0,normalize(Vec3{-1,0.2,-1e-9}));
    if(length(a-b)>1e-6) throw std::runtime_error("planet colour longitude seam");
    std::cout<<"PASS continuous plate outward winding and planet colour seam\n";
}
int main(int argc,char **argv) {
    if(argc<3) {std::cerr<<"nomai_asset_review SCENE_DIR MODEL_DIR [family|objects|all]\n";return 1;}
    const std::filesystem::path scenes=argv[1],models=argv[2];
    std::filesystem::create_directories(scenes);std::filesystem::create_directories(models);
    const std::string mode=argc>3?argv[3]:"all";
    if(mode!="all" && mode!="family" && mode!="objects") throw std::runtime_error("unknown review mode");
    checkSweptPlate();
    if(mode!="family") {
        const std::vector<std::pair<std::string,na::Model>> catalogue={
            {"nomai-shuttle",na::shuttle()},{"nomai-escape-pod",na::escapePod()},
            {"giants-deep",na::giantsDeep()},{"brittle-hollow",na::brittleHollow()},{"hourglass-twins",na::hourglassTwins()},
            {"quantum-trials-island",na::giantsDeepIsland()},{"hanging-city",na::hangingCity()},
            {"sunless-city",na::sunlessCity()},{"ash-twin-solar-tower",na::ashTwinTower()}
        };
        for(const auto &item:catalogue) {
            const auto &name=item.first;const na::Model &m=item.second;check(m);
            Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
            size_t triangles=0;
            for(const auto &p:m.pieces) {
                triangles+=p.mesh.tris.size();
                for(Vec3 v:p.mesh.verts) {v=transformPoint(p.frame,v);lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
            }
            Vec3 target=(lo+hi)*0.5;target.x=-target.x;
            const double r=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z})*0.5;
            const bool planet=name=="giants-deep"||name=="brittle-hollow"||name=="hourglass-twins";
            Vec3 dir=planet?normalize(Vec3{0.5,0.25,1.5}):normalize(Vec3{1.0,0.55,1.5});
            std::string s=header(target+dir*(r*4.0),target,1000,800,36,planet);
            if(!planet) s+=floorAt(lo.y-0.03);
            s+=m.text()+"WorldEnd\n";
            write(scenes/(name+".pbrt"),s);m.exportObj(models/(name+".obj"));
            const Vec3 angles[]={{0,0.1,1},{-1,0.15,0},{0,0.1,-1}};
            const char *labels[]={"front","side","back"};
            const std::string geometry=m.text()+"WorldEnd\n";
            for(int view=0;view<3;++view) {
                std::string review=header(target+normalize(angles[view])*(r*4.0),target,1000,800,36,planet);
                if(!planet) review+=floorAt(lo.y-0.03);
                write(scenes/(name+"-"+labels[view]+".pbrt"),review+geometry);
            }
            std::cout<<"PASS "<<name<<": "<<triangles<<" source triangles; exported OBJ/MTL and PBRT\n";
        }
    }
    if(mode!="objects") {
        std::string s=header({0,2.0,8.2},{0,0.95,0},1920,900,26);
        s+=floorAt(-0.02);
        const LifeStage stages[]={LifeStage::Child,LifeStage::Adolescent,LifeStage::AdultA,LifeStage::AdultB,LifeStage::Elder};
        for(int k=0;k<5;++k) {
            for(double phase:{0.0,0.5,1.0,1.5,2.0}) {
                ExplorerPose p=familyPose(stages[k],phase);p.walk=phase;p.stride=0.6;
                const ExplorerBody b=explorerBody(p);
                for(Vec3 foot:b.ball) if(!std::isfinite(length(foot))) throw std::runtime_error("invalid family gait");
            }
            const std::string member=familyMemberText(stages[k],identity(),0.5,0.014);
            s+="AttributeBegin\nTranslate "+std::to_string(-(k-2)*1.2)+" 0 0\n"+member+"AttributeEnd\n";
            const char *slugs[]={"child","adolescent","adult-a","adult-b","elder"};
            const double h=familyLook(stages[k]).stature;
            write(scenes/(std::string("nomai-family-")+slugs[k]+".pbrt"),header({2.8*h,1.6*h,4.8*h},{0,0.85*h,0},800,800,32)+floorAt(-0.02)+member+"WorldEnd\n");
            std::cout<<"PASS family "<<familyLook(stages[k]).label<<"; pose generated\n";
        }
        write(scenes/"nomai-family.pbrt",s+"WorldEnd\n");
    }
}
