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
        obj<<"mtllib "<<path.stem().string()<<".mtl\n";
        size_t first=1;
        for(size_t k=0;k<pieces.size();++k) {
            const Piece &p=pieces[k];
            obj<<"o part_"<<k<<"\nusemtl material_"<<k<<"\n";
            mtl<<"newmtl material_"<<k<<"\nKd "<<p.colour.x<<' '<<p.colour.y<<' '<<p.colour.z<<"\nNs "<<(p.metal>0?45:8)<<"\n";
            if(p.glow>0) mtl<<"Ke "<<p.colour.x*p.glow<<' '<<p.colour.y*p.glow<<' '<<p.colour.z*p.glow<<"\n";
            for(Vec3 v:p.mesh.verts) { v=transformPoint(p.frame,v); obj<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<"\n"; }
            for(auto t:p.mesh.tris) {
                const Vec3 a=transformPoint(p.frame,p.mesh.verts[t[0]]),b=transformPoint(p.frame,p.mesh.verts[t[1]]),c=transformPoint(p.frame,p.mesh.verts[t[2]]);
                if(length(cross(b-a,c-a))<1e-10) continue;
                obj<<"f "<<first+t[0]<<' '<<first+t[1]<<' '<<first+t[2]<<"\n";
            }
            first+=p.mesh.verts.size();
        }
        if(!obj || !mtl) throw std::runtime_error("cannot export Nomai asset");
    }
};
inline Vec3 ivory() { return {0.66,0.61,0.49}; }
inline Vec3 gold() { return {0.52,0.33,0.10}; }
inline Vec3 teal() { return {0.10,0.30,0.31}; }
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
    for(int row=0;row<6;++row) for(int k=0;k<sides;++k) {
        const double a=2*kPi*(k+0.5)/sides;
        if(std::sin(a)>0.7 && row<4) continue;
        const double y=0.25+(height-0.25)*(row+0.5)/6;
        const double span=2*radius*std::tan(kPi/sides)*0.97;
        const Vec3 c{radius*std::cos(a),y,radius*std::sin(a)};
        const Vec3 colour=row==4?teal():ivory()*(0.90+0.10*hash01(row*sides+k,218));
        m.add(roundedBox(span,(height-0.25)/6-0.018,0.26,0.025,1),mul(translate(c),rotate({0,1,0},90.0-a*180/kPi)),colour);
    }
    m.add(lathe({{radius+0.12,height},{radius+0.12,height+0.16},{0.4,height+1.0},{0.35,height+1.0},{radius-0.08,height+0.05}},sides),identity(),ivory());
    m.add(torus(radius,0.08,sides*3,5),translate(0,height-0.18,0),gold());
    m.append(door(),translate(0,0.22,radius+0.04));
    for(int side:{-1,1}) {
        const double x=side*1.08;
        m.add(box(0.50,1.15,0.10),translate(x,1.55,radius*0.78),{0.24,0.32,0.29});
        for(int i=0;i<25;++i) {
            const double a=i*0.42,b=(i+1)*0.42,r=0.015+0.006*i,rr=r+0.006;
            const Vec3 p{x+r*std::cos(a),1.55+r*std::sin(a),radius*0.78+0.06},q{x+rr*std::cos(b),1.55+rr*std::sin(b),radius*0.78+0.06};
            beam(m,p,q,0.016,{0.48,0.64,0.65});
        }
    }

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
    for(int side:{-1,1}) {
        beam(m,{side*(width*0.5),0.2,-length*0.5},{side*(width*0.5),0.2,length*0.5},0.16,gold());
        beam(m,{side*(width*0.5),0.05,-length*0.5},{side*(width*0.5),0.05,length*0.5},0.24,teal());
        for(int end:{-1,1}) {
            const Vec3 base{side*(width*0.5+0.16),0.0,end*(length*0.5-0.35)},top=base+Vec3{0,2.4,0};
            beam(m,base,top,0.26,ivory());
            beam(m,top,{side*(width*0.5),0.35,0},0.055,gold());
        }
    }
    for(int i=0;i<8;++i) m.add(box(width-0.25,0.016,0.025),translate(0,0.16,-length*0.5+(i+0.5)*length/8),{0.35,0.32,0.24});
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
    // A broken underside of crust makes the suspension readable in a studio view.
    for(int i=0;i<9;++i) m.add(uvSphere(1,8,5),mul(translate((i-4)*1.3,8.3,-1.8),scale(1.1,0.8+0.2*hash01(i,29),1.3)),{0.16,0.18,0.23});
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
    for(int i=0;i<9;++i) m.add(box(12,0.65,0.65+0.15*std::sin(i)),translate(0,i*0.7,-7.8),i%3==0?Vec3{0.62,0.35,0.15}:Vec3{0.42,0.20,0.09});
    return m;
}
inline Model ashTwinTower() {
    Model m;
    m.add(lathe({{0,0},{1.7,0},{1.4,0.35},{0.55,0.8},{0.35,8.7},{0.6,8.85},{0,8.85}},12),identity(),ivory());
    for(int side=0;side<4;++side) for(int level=0;level<3;++level) {
        std::vector<Vec3> hex;
        for(int k=0;k<6;++k) {const double a=kPi/2+2*kPi*k/6;hex.push_back({0.85*std::cos(a),1.0*std::sin(a),0});}
        const Mat4 panel=mul(rotate({0,1,0},side*90),translate(0,3.5+level*1.55,0.95+0.10*level),rotate({1,0,0},-18));
        m.add(polygonPrism(hex,0.12),panel,gold(),1.0);
        for(Vec3 &v:hex) v=v*0.84;
        m.add(polygonPrism(hex,0.14),mul(panel,translate(0,0,0.05)),{0.06,0.24,0.43},1.0);
        for(int k=0;k<6;++k) {const Vec3 a=hex[k],b=hex[(k+1)%6];beam(m,transformPoint(panel,a+Vec3{0,0,0.14}),transformPoint(panel,b+Vec3{0,0,0.14}),0.035,gold());}
        beam(m,{0,1.3,0},transformPoint(panel,{0,-0.9,0}),0.07,gold());
    }
    return m;
}
inline Model giantsDeepIsland() {
    Model m;
    m.add(uvSphere(1,32,16),mul(translate(0,-1.2,0),scale(6.0,1.8,5.0)),{0.14,0.19,0.16});
    m.add(uvSphere(1,32,12),mul(translate(0,-0.2,0),scale(5.5,0.35,4.6)),{0.24,0.28,0.15});
    // A tall, stacked trial tower with pale stone, teal bands and open crown.
    m.append(dwelling(1.65,6.8,12),translate(0,0.05,-0.6));
    for(double y:{2.2,4.2,6.5}) m.add(torus(1.78,0.10,36,5),translate(0,y,-0.6),teal());
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
    m.add(uvSphere(1,24,16),mul(translate(0,2.2,0),scale(1.8,2.2,1.6)),{0.06,0.15,0.18},1.0);
    for(int vane=0;vane<4;++vane) {
        const Mat4 frame=rotate({0,1,0},vane*90);
        for(int i=0;i<24;++i) {
            const auto path=[](double t) {return Vec3{0,0.45+5.8*t,1.60+0.45*std::sin(t*kPi)-0.60*t*t};};
            const Vec3 a=transformPoint(frame,path(i/24.0)),b=transformPoint(frame,path((i+1)/24.0));
            beam(m,a,b,0.62,ivory(),0.24);
            beam(m,a+transformPoint(frame,{0.23,0,0.10}),b+transformPoint(frame,{0.23,0,0.10}),0.075,gold());
        }
        for(int i=0;i<22;++i) {
            const auto scroll=[](double t) {const double a=0.6+5.1*t,r=0.75*(1-0.70*t);return Vec3{0,1.05+r*std::sin(a),1.75+r*std::cos(a)};};
            beam(m,transformPoint(frame,scroll(i/22.0)),transformPoint(frame,scroll((i+1)/22.0)),0.24,ivory());
        }
    }
    m.add(box(1.1,0.5,2.8),translate(0,0.1,0),ivory());
    return m;
}
inline Vec3 spherePoint(double u,double v) { const double a=2*kPi*u,p=kPi*v;return {std::sin(p)*std::cos(a),std::cos(p),std::sin(p)*std::sin(a)}; }
inline Vec3 planetColour(int kind,Vec3 p) {
    const double u=std::atan2(p.z,p.x)/(2*kPi)+0.5,v=std::acos(std::max(-1.0,std::min(1.0,p.y)))/kPi;
    const double n=nature::fbm2(u*14+2*nature::noise2(u*5,v*5,81),v*13,82);
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
    Model m;std::array<Mesh,24> groups;
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
    }
    for(int i=0;i<24;++i) if(!groups[i].tris.empty()) {
        Vec3 colour;
        const double n=i/23.0;
        if(kind==0) colour=lerp(Vec3{0.025,0.10,0.06},Vec3{0.66,0.80,0.51},n);
        else if(kind==1) colour=lerp(Vec3{0.07,0.08,0.13},Vec3{0.52,0.44,0.36},n);
        else colour=lerp(Vec3{0.23,0.07,0.025},Vec3{0.84,0.47,0.18},n);
        m.add(std::move(groups[i]),identity(),colour);
    }
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
            const double a=t*5.4*kPi,r=0.018+(size-0.018)*t+offset;
            return normalize(n+x*(r*std::cos(a))+y*(r*std::sin(a)))*radius;
        };
        for(int i=0;i<120;++i) {
            const double t=i/120.0,u=(i+1)/120.0;
            quad(cloud,point(t,-0.009,1.004),point(u,-0.009,1.004),point(u,0.009,1.004),point(t,0.009,1.004),true);
            quad(shadow,point(t,0.010,1.003),point(u,0.010,1.003),point(u,0.016,1.003),point(t,0.016,1.003),true);
        }
    }
    m.add(cloud,identity(),{0.55,0.71,0.43});m.add(shadow,identity(),{0.025,0.14,0.09});
    return m;
}
inline Model brittleHollow() {
    Model m=globe(1,1.0,true);
    m.add(uvSphere(0.18,32,16),identity(),{0.001,0.001,0.002});
    m.add(torus(0.25,0.012,64,5),rotate({1,0,0},24),{0.42,0.55,0.95},0,2.0);
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
