#pragma once
// Reference-driven reconstructions for The Last Nomai Signal, not extracted
// game assets. See docs/NOMAI_WORLD_ASSETS.md for sources and fidelity limits.
#include "nomai_mesh.h"
#include "nature_lib.h"
#include <filesystem>
#include <fstream>
#include <array>

namespace cartoon { namespace nomai { namespace assets {
struct Piece {
    Mesh mesh;
    Mat4 frame;
    Vec3 colour;
    double metal=0.0, glow=0.0;
};
struct Model {
    std::vector<Piece> pieces;
    void add(Mesh mesh, Mat4 frame, Vec3 colour, double metal=0.0, double glow=0.0) {
        pieces.push_back({std::move(mesh),frame,colour,metal,glow});
    }
    void append(const Model &other, const Mat4 &at=identity()) {
        for(const Piece &p:other.pieces) pieces.push_back({p.mesh,mul(at,p.frame),p.colour,p.metal,p.glow});
    }
    std::string text(const Mat4 &at=identity()) const {
        std::string out;
        for(const Piece &p:pieces) {
            const std::string material=p.metal>0 ? metalMaterial(p.colour,0.42) : diffuseMaterial(p.colour);
            const Vec3 emission=p.colour*p.glow;
            out+=attr(material,meshText(p.mesh,mul(at,p.frame)),p.glow>0 ? &emission : nullptr);
        }
        return out;
    }
    // Portable geometric assets: no cache paths or proprietary source meshes.
    void exportObj(const std::filesystem::path &path) const {
        std::ofstream obj(path),mtl(path.parent_path()/(path.stem().string()+".mtl"));
        // Miniature towers on normalized planets contain sub-millimetre inlays.
        // The default six significant digits can collapse their exported faces.
        obj.precision(17);
        obj<<"mtllib "<<path.stem().string()<<".mtl\n";
        size_t first=1;
        for(size_t k=0;k<pieces.size();++k) {
            const Piece &p=pieces[k];
            obj<<"o part_"<<k<<"\nusemtl material_"<<k<<"\n";
            mtl<<"newmtl material_"<<k<<"\nKd "<<p.colour.x<<' '<<p.colour.y<<' '<<p.colour.z<<"\nNs "<<(p.metal>0?45:8)<<"\n";
            if(p.glow>0) mtl<<"Ke "<<p.colour.x*p.glow<<' '<<p.colour.y*p.glow<<' '<<p.colour.z*p.glow<<"\n";
            for(Vec3 v:p.mesh.verts) { v=transformPoint(p.frame,v); obj<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<"\n"; }
            const Mat4 inv=invertAffine(p.frame);
            for(Vec3 n:p.mesh.normals) {
                n=normalize(Vec3{inv[0]*n.x+inv[4]*n.y+inv[8]*n.z,
                                 inv[1]*n.x+inv[5]*n.y+inv[9]*n.z,
                                 inv[2]*n.x+inv[6]*n.y+inv[10]*n.z});
                obj<<"vn "<<n.x<<' '<<n.y<<' '<<n.z<<"\n";
            }
            for(auto t:p.mesh.tris) {
                const Vec3 a=transformPoint(p.frame,p.mesh.verts[t[0]]),b=transformPoint(p.frame,p.mesh.verts[t[1]]),c=transformPoint(p.frame,p.mesh.verts[t[2]]);
                if(length(cross(b-a,c-a))<1e-10) continue;
                obj<<"f";
                for(int i:t) obj<<' '<<first+i<<"//"<<first+i;
                obj<<"\n";
            }
            first+=p.mesh.verts.size();
        }
        if(!obj || !mtl) throw std::runtime_error("cannot export Nomai asset");
    }
};
inline Vec3 ivory() { return {0.66,0.61,0.49}; }
inline Vec3 gold() { return {0.52,0.33,0.10}; }
inline Vec3 teal() { return {0.10,0.30,0.31}; }
// Fully three-dimensional fragments. nature::rock deliberately flattens its
// underside for ground scatter and cannot form an overhead crust or cave wall.
inline Mesh rockFragment(double size,int salt) {
    Mesh m=uvSphere(1,16,10);
    for(Vec3 &v:m.verts) v=v*(size*0.5*(0.82+0.30*nature::fbm2(v.x*3+v.y,v.z*3-v.y,salt)));
    std::vector<Vec3> sum(m.verts.size(),Vec3{0,0,0});
    for(auto t:m.tris) {
        const Vec3 n=cross(m.verts[t[1]]-m.verts[t[0]],m.verts[t[2]]-m.verts[t[0]]);
        for(int i:t) sum[i]=sum[i]+n;
    }
    for(size_t i=0;i<sum.size();++i) m.normals[i]=length(sum[i])>1e-12?normalize(sum[i]):normalize(m.verts[i]);
    return m;
}
inline void beam(Model &m,Vec3 a,Vec3 b,double width,Vec3 colour,double depth=0.0) {
    m.add(box(width,length(b-a),depth>0?depth:width),alignY((a+b)*0.5,b-a),colour);
}
inline void quad(Mesh &m,Vec3 a,Vec3 b,Vec3 c,Vec3 d,bool smooth=false) {
    const int n=static_cast<int>(m.verts.size());
    const Vec3 nn=normalize(cross(b-a,c-a));
    for(Vec3 v:{a,b,c,d}) { m.verts.push_back(v); m.normals.push_back(smooth?normalize(v):nn); }
    m.tris.push_back({n,n+1,n+2}); m.tris.push_back({n,n+2,n+3});
}
inline Mesh polygonPrism(const std::vector<Vec3> &points,double depth) {
    Mesh m;
    const int n=static_cast<int>(points.size());
    // Convex polygons, counterclockwise in xy, centered about z=0.
    for(int face:{-1,1}) {
        for(int k=1;k+1<n;++k) {
            const int first=static_cast<int>(m.verts.size());
            const int ids[3]={0,face>0?k:k+1,face>0?k+1:k};
            for(int i:ids) { m.verts.push_back(points[i]+Vec3{0,0,face*depth*0.5}); m.normals.push_back({0,0,static_cast<double>(face)}); }
            m.tris.push_back({first,first+1,first+2});
        }
    }
    for(int i=0;i<n;++i) {const int j=(i+1)%n;quad(m,points[i]-Vec3{0,0,depth*0.5},points[j]-Vec3{0,0,depth*0.5},points[j]+Vec3{0,0,depth*0.5},points[i]+Vec3{0,0,depth*0.5});}
    return m;
}
// Closed continuous rectangular sweep in the yz plane, avoiding visible
// overlapping box caps along the escape pod's curved protection plates.
inline Mesh sweptPlate(const std::vector<Vec3> &path,double width,double depth) {
    Mesh m;std::vector<std::array<Vec3,4>> rings;
    for(size_t i=0;i<path.size();++i) {
        const Vec3 tangent=normalize(path[std::min(i+1,path.size()-1)]-path[i?i-1:0]);
        const Vec3 n=normalize(cross(Vec3{1,0,0},tangent))*width*0.5;
        const Vec3 x{depth*0.5,0,0},c=path[i];
        rings.push_back({c-x-n,c-x+n,c+x+n,c+x-n});
    }
    for(size_t i=1;i<rings.size();++i) for(int j=0;j<4;++j)
        quad(m,rings[i-1][j],rings[i-1][(j+1)%4],rings[i][(j+1)%4],rings[i][j]);
    const auto &a=rings.front(),&b=rings.back();
    quad(m,a[3],a[2],a[1],a[0]);quad(m,b[0],b[1],b[2],b[3]);
    return m;
}
inline void hexLines(Model &m,const Mat4 &at,double radius,double yScale,double z,double width,Vec3 colour) {
    for(int k=0;k<6;++k) {
        const double a=kPi/2+k*kPi/3,b=a+kPi/3;
        beam(m,transformPoint(at,{radius*std::cos(a),radius*yScale*std::sin(a),z}),
               transformPoint(at,{radius*std::cos(b),radius*yScale*std::sin(b),z}),width,colour);
    }
}
// Angular branching spirals: decorative writing, not a translation of game text.
inline void writing(Model &m,const Mat4 &at,double size=1.0) {
    for(int branch=0;branch<3;++branch) {
        const Vec3 root{(branch-1)*0.19,branch==1?0.16:-0.14,0};
        for(int i=0;i<32;++i) {
            const auto point=[&](double t) {const double a=t*8.5,r=0.012+0.13*t;return root+Vec3{r*std::cos(a),r*std::sin(a),0};};
            beam(m,transformPoint(at,point(i/32.0)*size),transformPoint(at,point((i+1)/32.0)*size),0.009*size,{0.28,0.55,0.72});
        }
        beam(m,transformPoint(at,root*size),transformPoint(at,Vec3{0,-0.35,0}*size),0.009*size,{0.28,0.55,0.72});
    }
}
inline Model door(double width=1.4,double height=2.3) {
    Model m;
    const double x=width*0.5, spring=height*0.65;
    for(int side:{-1,1}) {
        beam(m,{side*x,0,0},{side*x,spring,0},0.20,ivory(),0.32);
        beam(m,{side*x,spring,0},{0,height,0},0.20,ivory(),0.32);
        beam(m,{side*(x+0.16),0,0},{side*(x+0.16),spring+0.06,0},0.06,gold(),0.37);
        beam(m,{side*(x+0.16),spring+0.06,0},{0,height+0.23,0},0.06,gold(),0.37);
    }
    return m;
}
inline Model dwelling(double radius=1.6,double height=3.1,int sides=10) {
    Model m;
    m.add(lathe({{0,0},{radius+0.25,0},{radius+0.25,0.22},{0,0.22}},sides),identity(),ivory());
    // Separate courses leave a real opening at the front, not a painted door.
    const int rows=std::max(6,static_cast<int>(std::ceil(height/0.45)));
    const double doorwayHeight=std::min(2.3,height*0.80);
    for(int row=0;row<rows;++row) for(int k=0;k<sides;++k) {
        const double a=2*kPi*(k+0.5)/sides;
        if(std::sin(a)>0 && std::abs(std::cos(a))<1.5*std::sin(kPi/sides) && 0.25+(height-0.25)*row/rows<doorwayHeight+0.22) continue;
        const double y=0.25+(height-0.25)*(row+0.5)/rows;
        const double span=2*radius*std::tan(kPi/sides)*0.97;
        const Vec3 c{radius*std::cos(a),y,radius*std::sin(a)};
        const Vec3 colour=row==4?teal():ivory()*(0.90+0.10*hash01(row*sides+k,218));
        m.add(roundedBox(span,(height-0.25)/rows-0.018,0.26,0.025,1),mul(translate(c),rotate({0,1,0},90.0-a*180/kPi)),colour);
    }
    m.add(lathe({{radius+0.12,height},{radius+0.12,height+0.16},{0.4,height+1.0},{0.35,height+1.0},{radius-0.08,height+0.05}},sides),identity(),ivory());
    m.add(torus(radius,0.08,sides*3,5),translate(0,height-0.18,0),gold());
    m.append(door(1.4,doorwayHeight),translate(0,0.22,radius+0.04));
    for(int side:{-1,1}) {
        const double x=side*1.08;
        m.add(box(0.50,1.15,0.10),translate(x,1.55,radius*0.78),{0.24,0.32,0.29});
        for(int i=0;i<25;++i) {
            const double a=i*0.42,b=(i+1)*0.42,r=0.015+0.006*i,rr=r+0.006;
            const Vec3 p{x+r*std::cos(a),1.55+r*std::sin(a),radius*0.78+0.06},q{x+rr*std::cos(b),1.55+rr*std::sin(b),radius*0.78+0.06};
            beam(m,p,q,0.016,{0.48,0.64,0.65});
        }
    }

    // Roof seams and entry threshold; actual recesses remain open.
    for(int k=0;k<sides;++k) {
        const double a=2*kPi*k/sides;
        beam(m,{(radius+0.13)*std::cos(a),height+0.17,(radius+0.13)*std::sin(a)},
               {0.40*std::cos(a),height+1.015,0.40*std::sin(a)},0.025,gold());
    }
    m.add(box(1.48,0.12,0.75),translate(0,0.22,radius+0.25),ivory());
    m.add(box(1.62,0.10,0.95),translate(0,0.10,radius+0.35),ivory()*0.85);
    // Circular interior bench, roof oculus rim, wall-mounted writing surfaces.
    for(int k=0;k<sides;++k) {
        const double a=2*kPi*(k+0.5)/sides;
        if(std::sin(a)>0.7) continue; // Keep the entry traversable.
        m.add(box(2*(radius-0.30)*std::tan(kPi/sides)*0.96,0.35,0.30),
              mul(translate((radius-0.30)*std::cos(a),0.43,(radius-0.30)*std::sin(a)),rotate({0,1,0},90-a*180/kPi)),{0.40,0.32,0.21});
    }
    m.add(torus(0.38,0.035,24,5),translate(0,height+1.0,0),gold());
    // Blue/rust triangular frieze beneath the roof.
    for(int k=0;k<sides;++k) {
        const double a=2*kPi*(k+0.5)/sides;
        const std::vector<Vec3> tri={{-0.22,-0.12,0},{0.22,-0.12,0},{0,0.16,0}};
        m.add(polygonPrism(tri,0.025),mul(translate(radius*std::cos(a),height-0.38,radius*std::sin(a)),rotate({0,1,0},90-a*180/kPi)),{0.52,0.23,0.10});
    }
    return m;
}
inline Model suspensionBridge(double length=6.0,double width=2.0) {
    Model m;
    m.add(box(width,0.26,length),translate(0,0.02,0),ivory());
    // Layered rust/teal cornices, inset flagstones, alternating diamond border.
    const int courses=static_cast<int>(std::ceil(length/0.34));
    for(int i=0;i<courses;++i) for(int j=0;j<3;++j)
        m.add(box((width-0.38)/3-0.018,0.035,length/courses-0.016),
              translate((j-1)*(width-0.38)/3,0.165,-length/2+(i+0.5)*length/courses),ivory()*(0.82+0.16*hash01(i*3+j,78)));
    for(int side:{-1,1}) {
        const double x=side*(width/2-0.10);
        m.add(box(0.20,0.09,length),translate(x,0.18,0),{0.42,0.19,0.095});
        m.add(box(0.12,0.08,length),translate(side*width/2,-0.04,0),teal());
        m.add(box(0.13,0.035,length),translate(side*width/2,-0.10,0),gold());
        for(int i=0;i<courses;++i) {
            const double z=-length/2+(i+0.5)*length/courses;
            m.add(box(0.12,0.015,0.12),mul(translate(x,0.235,z),rotate({0,1,0},45)),ivory());
            m.add(box(0.068,0.018,0.068),mul(translate(x,0.245,z),rotate({0,1,0},45)),teal());
        }
        // Splayed carved pylons and rigid stays from the published bridge sheet.
        for(int end:{-1,1}) {
            const double z=end*(length/2-0.4);
            const std::vector<Vec3> profile={{-0.34,-0.38,0},{0.34,-0.38,0},{0.22,0.40,0},{0.12,1.65,0},{-0.12,1.65,0},{-0.22,0.40,0}};
            const Mat4 pillar=translate(side*(width/2+0.15),0,z);
            m.add(polygonPrism(profile,0.32),pillar,ivory());
            for(int rib:{-1,1}) beam(m,transformPoint(pillar,{rib*0.24,-0.30,0.18}),transformPoint(pillar,{rib*0.08,1.58,0.18}),0.045,gold());
            const Vec3 top=transformPoint(pillar,{0,1.5,0});
            beam(m,top,{side*(width/2),0.35,0},0.075,gold());
            m.add(box(0.21,0.15,0.23),translate(side*width/2,0.30,0),ivory());
        }
    }
    return m;
}
inline Model hangingCity() {
    Model m;
    for(int k=0;k<3;++k) {
        const Vec3 at{(k-1)*3.8,k==1?1.0:0.0,k==1?-2.0:0.0};
        m.add(lathe({{0,-0.4},{2.25,-0.4},{2.25,0},{0,0}},8),translate(at),ivory());
        m.append(dwelling(1.45,k==1?4.0:3.0),translate(at));
        for(int side:{-1,1}) beam(m,at+Vec3{side*1.9,0,-0.8},at+Vec3{side*1.9,7.3,-0.8},0.18,gold());
    }
    m.append(suspensionBridge(4.0,1.6),mul(translate(-1.9,0.1,0.3),rotate({0,1,0},90)));
    m.append(suspensionBridge(4.0,1.6),mul(translate(1.9,0.1,0.3),rotate({0,1,0},90)));
    // Every hanger has an anchor block in the crust above its own district.
    for(int k=0;k<3;++k) {
        const Vec3 at{(k-1)*3.8,k==1?1.0:0.0,k==1?-2.0:0.0};
        for(int side:{-1,1}) {
            const Vec3 top=at+Vec3{side*1.9,7.3,-0.8};
            m.add(box(0.48,0.22,0.52),translate(top),ivory());
            m.add(rockFragment(2.4,97+k*2+side),mul(translate(top+Vec3{0,0.55,0}),scale(1.8,0.65,1.3)),{0.16,0.18,0.23});
        }
    }
    return m;
}
inline Model sunlessCity() {
    Model m;
    for(int level=0;level<3;++level) {
        const double y=level*1.1,z=-level*2.6;
        m.add(box(10-level,1.1,3.0),translate(0,y-0.55,z),{0.48,0.24,0.11});
        for(int side:{-1,1}) m.append(dwelling(1.1,2.1+0.3*level,8),translate(side*(2.8-0.3*level),y,z));
        for(int i=0;i<6;++i) m.add(box(1.25,0.20,0.48),translate(0,y+i*0.18,z+1.7-i*0.42),ivory());
    }
    // Layered canyon backdrop, left open for cameras and animation staging.
    for(int row=0;row<9;++row) for(int col=0;col<7;++col) {
        const double x=(col-3)*1.65+(row%2)*0.3,y=row*0.7;
        m.add(rockFragment(1.0,31+row*7+col),mul(translate(x,y,-7.8),scale(2.1,0.95,1.2)),
              row%3==0?Vec3{0.62,0.35,0.15}:Vec3{0.42,0.20,0.09});
    }
    return m;
}
inline Model ashTwinTower() {
    Model m;
    m.add(lathe({{0,0},{1.2,0},{1.2,0.18},{0.48,0.48},{0.23,8.7},{0.38,8.8},{0.38,8.95},{0,8.95}},12),identity(),ivory());
    for(int side=0;side<4;++side) {
        const Mat4 around=rotate({0,1,0},side*90);
        // Fluted spine and spreading root buttresses, not a plain cylinder.
        for(int rib:{-1,1}) {
            std::vector<Vec3> root={{rib*0.16,0.05,1.0},{rib*0.16,0.5,0.72},{rib*0.16,1.3,0.35},{rib*0.16,2.1,0.27},{rib*0.16,8.65,0.27}};
            for(size_t i=1;i<root.size();++i) beam(m,transformPoint(around,root[i-1]),transformPoint(around,root[i]),0.07,gold());
        }
        for(int level=0;level<5;++level) {
            std::vector<Vec3> hex;
            for(int k=0;k<6;++k) {const double a=kPi/2+2*kPi*k/6;hex.push_back({0.68*std::cos(a),0.79*std::sin(a),0});}
            const Mat4 panel=mul(around,translate(0,2.65+level*1.22,0.58+0.09*level),rotate({1,0,0},-13));
            m.add(polygonPrism(hex,0.12),panel,gold(),1.0);
            for(Vec3 &v:hex) v=v*0.90;
            m.add(polygonPrism(hex,0.14),mul(panel,translate(0,0,0.035)),{0.035,0.17,0.32},1.0);
            // Blue glass panes and nested copper hexagonal circuitry on both faces.
            for(int face:{-1,1}) {
                for(double r:{0.15,0.31,0.52,0.63}) hexLines(m,panel,r,0.79/0.68,face*0.12,0.014,gold());
                for(int k=0;k<6;++k) {
                    const double a=kPi/2+k*kPi/3;
                    beam(m,transformPoint(panel,{0.15*std::cos(a),0.174*std::sin(a),face*0.12}),
                           transformPoint(panel,{0.63*std::cos(a),0.732*std::sin(a),face*0.12}),0.012,gold());
                }
            }
            for(int strip=0;strip<7;++strip) {
                const double x=(strip-3)*0.13,h=0.61-0.4*std::abs(x);
                m.add(box(0.025,h*2,0.005),mul(panel,translate(x,0,0.109)),{0.04,0.24+0.02*(strip%3),0.42},1.0);
            }
            beam(m,transformPoint(around,{0,2.2+level*1.22,0.25}),transformPoint(panel,{0,-0.65,0}),0.07,gold());
        }
    }
    return m;
}
inline Model giantsDeepIsland() {
    Model m;
    m.add(uvSphere(1,32,16),mul(translate(0,-1.2,0),scale(6.0,1.8,5.0)),{0.14,0.19,0.16});
    m.add(uvSphere(1,32,12),mul(translate(0,-0.2,0),scale(5.5,0.35,4.6)),{0.24,0.28,0.15});
    for(int k=0;k<18;++k) {
        const double a=2*kPi*k/18;
        m.add(nature::rock(1.4,180+k),mul(translate(4.6*std::cos(a),-0.20,3.7*std::sin(a)),scale(1.1,0.8,1.1)),{0.16,0.22,0.16});
    }
    // A tall, stacked trial tower with pale stone, teal bands and open crown.
    m.append(dwelling(1.65,6.8,12),translate(0,0.05,-0.6));
    for(double y:{2.2,4.2,6.5}) m.add(torus(1.78,0.10,36,5),translate(0,y,-0.6),teal());
    // Paired exterior ramp studies and the three-diamond Nomai motif.
    for(int side:{-1,1}) for(int i=0;i<40;++i) {
        const double a=side*(0.4+i*0.09),b=side*(0.4+(i+1)*0.09);
        const Vec3 p{2.0*std::sin(a),0.4+i*0.10,-0.6+2.0*std::cos(a)},q{2.0*std::sin(b),0.5+i*0.10,-0.6+2.0*std::cos(b)};
        beam(m,p,q,0.42,ivory(),0.16);
    }
    for(int i=0;i<3;++i) {
        const Vec3 at{(i-1)*0.34,5.4+(i==1?0.34:0.0),1.23};
        m.add(box(0.30,0.30,0.08),mul(translate(at),rotate({0,0,1},45)),gold());
        m.add(box(0.19,0.19,0.09),mul(translate(at+Vec3{0,0,0.03}),rotate({0,0,1},45)),{0.65,0.32,0.05},0,0.5);
    }
    for(int i=0;i<3;++i) {
        const Vec3 root{(i-1)*3.4,0.0,1.5};
        beam(m,root,root+Vec3{0.25,2.0,0},0.12,{0.25,0.17,0.08});
        for(int k=0;k<6;++k) {
            const double a=2*kPi*k/6;
            const Vec3 top=root+Vec3{0.25,2.0,0},tip=top+Vec3{1.1*std::cos(a),-0.4,1.1*std::sin(a)};
            beam(m,top,tip,0.13,{0.10,0.22,0.10},0.04);
        }
    }
    return m;
}
// A vertical gravity-cannon shuttle reconstruction. Lift glow is not a rocket
// exhaust: the original game's shuttles have no independent thrust drive.
inline Model shuttle() {
    Model m;
    m.add(lathe({{0,0.8},{1.6,0.8},{1.9,1.25},{1.85,3.2},{1.45,4.3},{0.65,5.0},{0,5.1}},24),identity(),ivory());
    m.add(lathe({{0,0.65},{1.55,0.65},{1.7,0.88},{0,0.88}},24),identity(),{0.13,0.15,0.14});
    const Mat4 window=mul(translate(0,2.7,1.94),rotate({1,0,0},90));
    m.add(lathe({{0,-0.02},{0.82,-0.02},{0.82,0.04},{0,0.04}},32),window,{0.035,0.10,0.12},1.0);
    m.add(torus(0.85,0.10,32,6),window,gold(),1.0);
    for(int side:{-1,1}) {
        beam(m,{side*0.65,0.9,1.7},{side*0.65,4.1,1.4},0.12,gold());
        beam(m,{side*0.65,4.1,1.4},{0,4.9,0.7},0.12,gold());
    }
    for(int k=0;k<3;++k) {
        const double a=2*kPi*k/3;
        const Vec3 top{1.6*std::cos(a),1.65,1.6*std::sin(a)},foot{2.5*std::cos(a),0.12,2.5*std::sin(a)};
        beam(m,top,foot,0.18,gold());
        m.add(box(0.65,0.18,0.7),translate(foot),ivory());
    }
    m.add(torus(1.50,0.055,40,6),translate(0,0.65,0),{0.25,0.55,0.80},0,2.0);
    return m;
}
// The distinct older escape-pod silhouette follows Jacobson's Escape-Pod-B:
// swept protective vanes, curled lower scrolls, central dark glass and keel.
inline Model escapePod() {
    Model m;
    m.add(uvSphere(1,40,24),mul(translate(0,2.1,0),scale(1.35,1.75,1.6)),{0.055,0.18,0.24},1.0);
    // Swept continuous plates with a genuine curled lower profile. The earlier
    // four straight rods read as a cage and missed the concept's main silhouette.
    std::vector<Vec3> path;
    const Vec3 p0{0,5.8,-1.6},p1{0,3.4,-1.6},p2{0,3.4,2.3},p3{0,1.2,2.3};
    for(int i=0;i<=64;++i) {
        const double t=i/64.0,u=1-t;
        path.push_back(p0*(u*u*u)+p1*(3*u*u*t)+p2*(3*u*t*t)+p3*(t*t*t));
    }
    for(int i=1;i<=64;++i) {
        const double t=i/64.0,a=-5.6*t,r=1.05*(1-0.78*t);
        path.push_back({0,1.2+r*std::sin(a),1.25+r*std::cos(a)});
    }
    // The protection plates sweep in one longitudinal direction, on either
    // side of the cabin. A radial arrangement incorrectly resembles a cage.
    for(int side:{-1,1}) {
        const Mat4 frame=translate(side*1.32,0,0);
        m.add(sweptPlate(path,0.54,0.30),frame,ivory());
        m.add(sweptPlate(path,0.24,0.32),frame,{0.18,0.22,0.21});
        for(int edge:{-1,1}) m.add(sweptPlate(path,0.055,0.025),mul(frame,translate(edge*0.16,0,0)),gold(),1.0);
        const Vec3 tip=path.front();
        m.add(box(0.36,0.25,0.35),mul(frame,translate(tip)),ivory());
        // Nested outer relief follows the same curl, avoiding separate floating scrolls.
        m.add(sweptPlate(path,0.38,0.06),mul(frame,translate(side*0.19,0,0)),ivory());
        m.add(sweptPlate(path,0.12,0.065),mul(frame,translate(side*0.19,0,0)),gold());
    }
    // Canopy ribs, a lower keel and a framed exit hatch.
    for(int k=0;k<8;++k) {
        const Mat4 frame=rotate({0,1,0},k*45);
        std::vector<Vec3> rib;
        for(int i=0;i<=32;++i) {const double a=0.16+(kPi-0.32)*i/32.0;rib.push_back({0,2.1+1.77*std::cos(a),1.62*std::sin(a)});}
        m.add(sweptPlate(rib,0.035,0.04),frame,gold());
    }
    m.add(lathe({{0,0.12},{0.75,0.12},{1.05,0.35},{1.3,0.7},{1.25,0.82},{0,0.82}},12),identity(),ivory());
    m.add(box(0.95,1.3,0.12),translate(0,1.60,1.46),{0.10,0.15,0.16});
    for(int side:{-1,1}) beam(m,{side*0.52,0.91,1.55},{side*0.52,2.30,1.55},0.10,ivory());
    for(double y:{0.91,2.30}) beam(m,{-0.52,y,1.55},{0.52,y,1.55},0.10,ivory());
    writing(m,translate(0,1.6,1.54),1.0);
    return m;
}
inline Vec3 spherePoint(double u,double v) { const double a=2*kPi*u,p=kPi*v;return {std::sin(p)*std::cos(a),std::cos(p),std::sin(p)*std::sin(a)}; }
inline Vec3 planetColour(int kind,Vec3 p) {
    const double u=std::atan2(p.z,p.x)/(2*kPi)+0.5,v=std::acos(std::max(-1.0,std::min(1.0,p.y)))/kPi;
    const double n=(nature::fbm2(p.x*7+p.z*3,p.y*7,82)+nature::fbm2(p.z*9,p.y*9+p.x*3,91))*0.5;
    if(kind==0) {
        double cloud=0.35+0.55*n;
        for(int k=0;k<6;++k) {
            const double x=std::remainder(u-(k+0.35)/6.0,1.0),y=v-(0.25+0.10*(k%4));
            const double r=std::hypot(x*2,y),a=std::atan2(y,x*2);
            cloud+=0.26*std::exp(-r*r/0.008)*(0.5+0.5*std::sin(a*2+r*120));
        }
        return lerp(Vec3{0.025,0.17,0.105},Vec3{0.58,0.72,0.44},smoothstep(0.28,0.85,cloud));
    }
    if(kind==1) return lerp(Vec3{0.11,0.13,0.20},Vec3{0.40,0.34,0.31},n);
    const double bands=0.5+0.5*std::sin(v*85+3*n);
    return lerp(Vec3{0.32,0.10,0.035},Vec3{0.72,0.39,0.14},0.5*n+0.5*bands);
}
inline Model globe(int kind,double radius=1.0,bool cutaway=false) {
    Model m;std::array<Mesh,24> groups;Mesh crust;
    const int nu=112,nv=56;
    for(int y=0;y<nv;++y) for(int x=0;x<nu;++x) {
        const Vec3 c=spherePoint((x+0.5)/nu,(y+0.5)/nv);
        if(cutaway && c.z>0.25 && c.x>-0.55 && c.y<0.65) continue;
        const Vec3 colour=planetColour(kind,c);
        const int bin=std::max(0,std::min(23,static_cast<int>((kind==0?(colour.y-0.17)/0.55:kind==1?(colour.x-0.11)/0.29:(colour.x-0.32)/0.40)*23)));
        Vec3 pts[4]={spherePoint(x/static_cast<double>(nu),y/static_cast<double>(nv)),spherePoint((x+1)/static_cast<double>(nu),y/static_cast<double>(nv)),spherePoint((x+1)/static_cast<double>(nu),(y+1)/static_cast<double>(nv)),spherePoint(x/static_cast<double>(nu),(y+1)/static_cast<double>(nv))};
        for(Vec3 &p:pts) {
            double r=radius;
            if(kind>0) r*=1.0+0.025*nature::noise2(p.x*20+p.z*9,p.y*20,41);
            if(kind==2) r*=1.0-0.08*std::pow(0.5+0.5*std::sin(std::atan2(p.z,p.x)*13+p.y*8),14);
            p=p*r;
        }
        quad(groups[bin],pts[0],pts[1],pts[2],pts[3],true);
        if(cutaway) {
            const auto removed=[](Vec3 p) {return p.z>0.25 && p.x>-0.55 && p.y<0.65;};
            const auto inside=[](Vec3 p) {return normalize(p)*0.88;};
            quad(crust,inside(pts[3]),inside(pts[2]),inside(pts[1]),inside(pts[0]));
            const Vec3 neighbors[4]={spherePoint((x+0.5)/nu,(y-0.5)/nv),spherePoint((x+1.5)/nu,(y+0.5)/nv),
                                     spherePoint((x+0.5)/nu,(y+1.5)/nv),spherePoint((x-0.5)/nu,(y+0.5)/nv)};
            for(int edge=0;edge<4;++edge) if(removed(neighbors[edge])) {
                const Vec3 a=pts[edge],b=pts[(edge+1)%4];quad(crust,a,inside(a),inside(b),b);
            }
        }
    }
    for(int i=0;i<24;++i) if(!groups[i].tris.empty()) {
        Vec3 colour;
        const double n=i/23.0;
        if(kind==0) colour=lerp(Vec3{0.025,0.10,0.06},Vec3{0.66,0.80,0.51},n);
        else if(kind==1) colour=lerp(Vec3{0.07,0.08,0.13},Vec3{0.52,0.44,0.36},n);
        else colour=lerp(Vec3{0.23,0.07,0.025},Vec3{0.84,0.47,0.18},n);
        m.add(std::move(groups[i]),identity(),colour);
    }
    if(!crust.tris.empty()) m.add(std::move(crust),identity(),{0.13,0.15,0.21});
    return m;
}
inline Model giantsDeep() {
    Model m=globe(0);
    const Vec3 centers[]={normalize(Vec3{-0.45,0.35,0.85}),normalize(Vec3{0.25,-0.30,0.94}),normalize(Vec3{0.7,0.5,0.25}),normalize(Vec3{-0.8,-0.35,-0.2}),normalize(Vec3{0.1,0.9,0.1})};
    Mesh cloud,shadow;
    for(int storm=0;storm<5;++storm) {
        const Vec3 n=centers[storm],x=normalize(cross(n,Vec3{0,1,0})),y=cross(x,n);
        const double size=storm==0?0.29:0.18;
        const auto point=[&](double t,double offset,double radius) {
            const double a=t*5.4*kPi,r=0.018+(size-0.018)*t+offset*(0.25+0.75*std::sin(t*kPi));
            return normalize(n+x*(r*std::cos(a))+y*(r*std::sin(a)))*radius;
        };
        for(int i=0;i<120;++i) {
            const double t=i/120.0,u=(i+1)/120.0;
            quad(cloud,point(t,-0.020,1.004),point(u,-0.020,1.004),point(u,0.020,1.004),point(t,0.020,1.004),true);
            quad(shadow,point(t,0.021,1.003),point(u,0.021,1.003),point(u,0.032,1.003),point(t,0.032,1.003),true);
        }
    }
    m.add(cloud,identity(),{0.55,0.71,0.43});m.add(shadow,identity(),{0.025,0.14,0.09});
    return m;
}
inline Model brittleHollow() {
    Model m=globe(1,1.0,true);
    m.add(uvSphere(0.18,32,16),identity(),{0.001,0.001,0.002});
    for(int i=0;i<12;++i) {
        const double a=2*kPi*i/12;
        const Vec3 pos{0.72*std::cos(a),-0.38+0.13*std::sin(a*2),0.72*std::sin(a)};
        m.add(lathe({{0,0},{0.12,0.05},{0.10,0.20},{0,0.48}},5),mul(translate(pos),alignY({0,0,0},pos*-1.0)),{0.15,0.24,0.39});
    }
    m.add(uvSphere(0.24,24,12),translate(-1.35,0.68,-0.25),{0.29,0.10,0.025});
    for(int k=0;k<7;++k) {
        const double a=k*2.4;
        m.add(uvSphere(0.045,8,5),translate(-1.35+0.21*std::cos(a),0.68+0.21*std::sin(a),-0.15),{0.85,0.24,0.025},0,0.6);
    }
    return m;
}
inline Model hourglassTwins() {
    Model m;
    m.append(globe(2,0.82),translate(-1.18,0,0));
    m.add(uvSphere(0.72,72,36),translate(1.18,0.12,0),{0.65,0.39,0.16});
    const Vec3 a{-0.38,0.04,0},b{0.49,0.10,0};
    m.add(lathe({{0.16,0},{0.11,0.2},{0.07,0.45},{0.10,0.7},{0.17,length(b-a)}},32),alignY(a,b-a),{0.72,0.47,0.22});
    for(int k=0;k<6;++k) {
        const double angle=k*2*kPi/6;
        m.append(ashTwinTower(),mul(translate(1.18+0.66*std::cos(angle),0.12+0.66*std::sin(angle),0),rotate({0,0,1},angle*180/kPi-90),scale(0.025,0.025,0.025)));
    }
    return m;
}
} } }
