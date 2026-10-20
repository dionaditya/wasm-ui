#pragma once
// Minimal PDF parser: text extraction + page dimensions
// Supports FlateDecode streams, traditional xref tables, xref streams (PDF 1.5+), object streams
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <zlib.h>

namespace Pdf {

struct TextSpan { float x=0, y=0, fontSize=12; std::string text; };
struct Page     { float w=612, h=792; std::vector<TextSpan> spans; };
struct Doc      { bool ok=false; std::string error; std::vector<Page> pages; std::vector<uint8_t> raw; };

// ─── Low-level helpers ────────────────────────────────────────────────────────

static bool isWS(uint8_t c) {
    return c==' '||c=='\t'||c=='\r'||c=='\n'||c==0x0C||c==0x00;
}
static bool isDelim(uint8_t c) {
    return c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||c=='{'||c=='}'||c=='/'||c=='%';
}

struct View {
    const uint8_t* p;
    const uint8_t* end;
    void skip()     { while(p<end && isWS(*p)) ++p; }
    void skipLine() { while(p<end && *p!='\n'&&*p!='\r') ++p; if(p<end&&*p=='\r')++p; if(p<end&&*p=='\n')++p; }
    bool startsWith(const char* s) const { size_t n=strlen(s); return (size_t)(end-p)>=n && memcmp(p,s,n)==0; }
};

static ptrdiff_t rfind(const uint8_t* data, size_t sz, const char* needle, size_t before=SIZE_MAX) {
    size_t n=strlen(needle), lim=before<sz?before:sz;
    if(lim<n) return -1;
    for(ptrdiff_t i=(ptrdiff_t)(lim-n);i>=0;--i)
        if(memcmp(data+i,needle,n)==0) return i;
    return -1;
}
static ptrdiff_t ffind(const uint8_t* data, size_t sz, const char* needle, size_t from=0) {
    size_t n=strlen(needle);
    for(size_t i=from;i+n<=sz;++i)
        if(memcmp(data+i,needle,n)==0) return (ptrdiff_t)i;
    return -1;
}

static bool decompressFlate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    out.clear();
    z_stream zs{}; if(inflateInit(&zs)!=Z_OK) return false;
    zs.next_in=(Bytef*)in; zs.avail_in=(uInt)inLen;
    uint8_t buf[65536]; int ret;
    do { zs.next_out=buf; zs.avail_out=sizeof(buf); ret=inflate(&zs,Z_NO_FLUSH); out.insert(out.end(),buf,buf+sizeof(buf)-zs.avail_out); } while(ret==Z_OK);
    inflateEnd(&zs); return ret==Z_STREAM_END;
}

// ─── Tokenizer ────────────────────────────────────────────────────────────────

enum TokType { T_EOF,T_INT,T_REAL,T_NAME,T_STRING,T_OP,T_ARROPEN,T_ARRCLOSE,T_DICTOPEN,T_DICTCLOSE };
struct Token  { TokType type=T_EOF; std::string str; double num=0; };

static Token nextToken(View& v) {
    // skip whitespace + comments
    v.skip();
    while(v.p<v.end && *v.p=='%') { v.skipLine(); v.skip(); }
    if(v.p>=v.end) return {};

    Token t; uint8_t c=*v.p;

    if(c=='/') {
        t.type=T_NAME; ++v.p;
        while(v.p<v.end&&!isWS(*v.p)&&!isDelim(*v.p)) {
            if(*v.p=='#'&&v.p+2<v.end) { char h[3]={(char)v.p[1],(char)v.p[2],0}; t.str+=(char)strtol(h,nullptr,16); v.p+=3; }
            else t.str+=(char)*v.p++;
        }
        return t;
    }
    if(c=='(') {
        t.type=T_STRING; ++v.p; int depth=1;
        while(v.p<v.end&&depth>0) {
            uint8_t ch=*v.p++;
            if(ch=='\\' && v.p<v.end) {
                uint8_t e=*v.p++;
                switch(e){case 'n':t.str+='\n';break;case 'r':t.str+='\r';break;case 't':t.str+='\t';break;
                          case '(':t.str+='(';break;case ')':t.str+=')';break;case '\\':t.str+='\\';break;
                          default: if(e>='0'&&e<='7'){int o=e-'0'; for(int i=0;i<2&&v.p<v.end&&*v.p>='0'&&*v.p<='7';++i)o=o*8+(*v.p++-'0'); t.str+=(char)o;} else t.str+=(char)e; }
            } else if(ch=='('){++depth;t.str+='(';}
            else if(ch==')'){--depth; if(depth>0)t.str+=')';}
            else t.str+=(char)ch;
        }
        return t;
    }
    if(c=='<') {
        if(v.p+1<v.end&&v.p[1]=='<'){t.type=T_DICTOPEN;v.p+=2;return t;}
        t.type=T_STRING; ++v.p;
        while(v.p<v.end&&*v.p!='>') {
            uint8_t hi=*v.p++; if(isWS(hi))continue;
            uint8_t lo=(v.p<v.end&&*v.p!='>')?*v.p++:'0';
            char h[3]={(char)hi,(char)lo,0}; t.str+=(char)strtol(h,nullptr,16);
        }
        if(v.p<v.end)++v.p; return t;
    }
    if(c=='>'){if(v.p+1<v.end&&v.p[1]=='>'){t.type=T_DICTCLOSE;v.p+=2;return t;} t.type=T_OP;t.str=">";++v.p;return t;}
    if(c=='['){t.type=T_ARROPEN;++v.p;return t;}
    if(c==']'){t.type=T_ARRCLOSE;++v.p;return t;}

    // number vs operator
    bool neg=(c=='-'), pos=(c=='+');
    bool isNum=((c>='0'&&c<='9')||c=='.'|| (neg||pos));
    if(neg||pos) { if(v.p+1>=v.end||((v.p[1]<'0'||v.p[1]>'9')&&v.p[1]!='.')) isNum=false; }
    if(isNum) {
        const uint8_t* s=v.p; bool dot=false;
        if(*v.p=='-'||*v.p=='+')++v.p;
        while(v.p<v.end&&((*v.p>='0'&&*v.p<='9')||(*v.p=='.'&&!dot))){if(*v.p=='.')dot=true;++v.p;}
        std::string ns((char*)s,v.p-s); t.num=strtod(ns.c_str(),nullptr);
        t.str=ns; t.type=dot?T_REAL:T_INT; return t;
    }
    t.type=T_OP;
    while(v.p<v.end&&!isWS(*v.p)&&!isDelim(*v.p)) t.str+=(char)*v.p++;
    return t;
}

// ─── Value ────────────────────────────────────────────────────────────────────

struct Value {
    enum Kind{Null,Bool,Int,Real,Name,String,Array,Dict,Ref} kind=Null;
    int ival=0; double fval=0; std::string sval;
    std::vector<Value> arr;
    std::unordered_map<std::string,Value> dict;
    int ref_obj=0, ref_gen=0;
    bool is_stream=false; // sval holds raw stream bytes when true

    bool isNull()  const{return kind==Null;}
    bool isNum()   const{return kind==Int||kind==Real;}
    bool isStr()   const{return kind==String;}
    bool isName()  const{return kind==Name;}
    bool isArray() const{return kind==Array;}
    bool isDict()  const{return kind==Dict||is_stream;}
    bool isRef()   const{return kind==Ref;}
    double asNum() const{return kind==Int?(double)ival:fval;}

    Value* get(const std::string& k){if(kind!=Dict&&!is_stream)return nullptr;auto it=dict.find(k);return it!=dict.end()?&it->second:nullptr;}
    const Value* get(const std::string& k) const{if(kind!=Dict&&!is_stream)return nullptr;auto it=dict.find(k);return it!=dict.end()?&it->second:nullptr;}
};

// ─── Parser state ─────────────────────────────────────────────────────────────

struct ObjLoc { enum{InFile,InObjStm}src=InFile; size_t offset=0; int stm_obj=0; int stm_idx=0; };

struct Parser {
    const uint8_t* data; size_t size;
    std::unordered_map<int,ObjLoc> xref;
    Value trailer;
    // cache for decompressed obj streams
    std::unordered_map<int,std::vector<uint8_t>> objstm_cache;

    Value parseValue(View& v);

    Value resolve(const Value& v) { return v.isRef()?getObj(v.ref_obj):v; }

    Value getObj(int num) {
        auto it=xref.find(num); if(it==xref.end()) return {};
        const ObjLoc& loc=it->second;
        if(loc.src==ObjLoc::InFile) {
            if(loc.offset==0) return {};
            View v{data+loc.offset,data+size};
            nextToken(v); nextToken(v); // obj_num gen
            Token t3=nextToken(v); if(t3.type!=T_OP||t3.str!="obj") return {};
            Value obj=parseValue(v);
            v.skip();
            if(v.startsWith("stream")) {
                v.p+=6; if(v.p<v.end&&*v.p=='\r')++v.p; if(v.p<v.end&&*v.p=='\n')++v.p;
                int len=0;
                if(obj.isDict()){if(auto* lv=obj.get("Length")){Value lr=resolve(*lv);if(lr.isNum())len=(int)lr.asNum();}}
                obj.sval.assign((const char*)v.p,(const char*)v.p+len);
                obj.is_stream=true;
            }
            return obj;
        } else {
            return getFromObjStm(loc.stm_obj, loc.stm_idx);
        }
    }

    Value getFromObjStm(int stm_obj, int idx) {
        Value stm=getObj(stm_obj);
        if(!stm.is_stream) return {};
        auto& cache=objstm_cache[stm_obj];
        if(cache.empty()) {
            auto d=getStreamBytes(stm);
            if(d.empty()) return {};
            cache=d;
        }
        int N=0; if(auto* nv=stm.get("N")){Value r=resolve(*nv);if(r.isNum())N=(int)r.asNum();}
        int first=0; if(auto* fv=stm.get("First")){Value r=resolve(*fv);if(r.isNum())first=(int)r.asNum();}

        const uint8_t* sp=cache.data(); const uint8_t* se=sp+cache.size();
        View sv{sp,se};
        std::vector<std::pair<int,int>> entries;
        for(int i=0;i<N;++i){ Token tn=nextToken(sv),to=nextToken(sv); entries.push_back({(int)tn.num,(int)to.num}); }
        if(idx>=(int)entries.size()) return {};
        int off=entries[idx].second;
        if(sp+first+off>=se) return {};
        View ov{sp+first+off,se};
        return parseValue(ov);
    }

    std::vector<uint8_t> getStreamBytes(const Value& stm) {
        if(!stm.is_stream) return {};
        bool isFlate=false;
        if(auto* fv=stm.get("Filter")){
            Value fr=resolve(*fv);
            if(fr.isName()&&(fr.sval=="FlateDecode"||fr.sval=="Fl")) isFlate=true;
            if(fr.isArray()&&!fr.arr.empty()){Value f0=resolve(fr.arr[0]);if(f0.isName()&&(f0.sval=="FlateDecode"||f0.sval=="Fl"))isFlate=true;}
        }
        if(isFlate){ std::vector<uint8_t> out; decompressFlate((const uint8_t*)stm.sval.data(),stm.sval.size(),out); return out; }
        return std::vector<uint8_t>((const uint8_t*)stm.sval.data(),(const uint8_t*)stm.sval.data()+stm.sval.size());
    }

    bool parseXRefTable(size_t offset) {
        if(offset>=size) return false;
        View v{data+offset,data+size}; v.skip();
        Token t=nextToken(v); if(t.type!=T_OP||t.str!="xref") return false;
        while(true){
            v.skip(); View pk=v; Token t1=nextToken(pk); if(t1.type!=T_INT) break;
            Token t2=nextToken(pk); if(t2.type!=T_INT) break; v=pk;
            int first=(int)t1.num,count=(int)t2.num;
            for(int i=0;i<count;++i){
                Token ot=nextToken(v),gt=nextToken(v),ut=nextToken(v);
                if(ot.type==T_EOF) break;
                int obj=first+i;
                if(ut.str=="n"&&xref.find(obj)==xref.end()){ObjLoc l;l.offset=(size_t)ot.num;xref[obj]=l;}
            }
        }
        v.skip(); Token tt=nextToken(v); if(tt.str=="trailer") trailer=parseValue(v);
        return true;
    }

    bool parseXRefStream(size_t offset) {
        if(offset>=size) return false;
        View v{data+offset,data+size};
        nextToken(v); nextToken(v); Token t3=nextToken(v); if(t3.type!=T_OP||t3.str!="obj") return false;
        Value sd=parseValue(v);
        v.skip(); if(!v.startsWith("stream")) return false;
        v.p+=6; if(v.p<v.end&&*v.p=='\r')++v.p; if(v.p<v.end&&*v.p=='\n')++v.p;
        int len=0;
        if(sd.isDict()){if(auto* lv=sd.get("Length")){Value lr=resolve(*lv);if(lr.isNum())len=(int)lr.asNum();}}
        sd.sval.assign((const char*)v.p,(const char*)v.p+len); sd.is_stream=true;
        trailer=sd;
        auto raw=getStreamBytes(sd); if(raw.empty()) return false;
        int w1=1,w2=4,w3=2;
        if(auto* wv=sd.get("W")){Value wr=resolve(*wv);if(wr.isArray()&&wr.arr.size()>=3){w1=(int)resolve(wr.arr[0]).asNum();w2=(int)resolve(wr.arr[1]).asNum();w3=(int)resolve(wr.arr[2]).asNum();}}
        std::vector<std::pair<int,int>> idx_pairs;
        if(auto* iv=sd.get("Index")){Value ir=resolve(*iv);if(ir.isArray()){for(size_t i=0;i+1<ir.arr.size();i+=2)idx_pairs.push_back({(int)resolve(ir.arr[i]).asNum(),(int)resolve(ir.arr[i+1]).asNum()});}}
        if(idx_pairs.empty()){int sz=0;if(auto* sv2=sd.get("Size")){sz=(int)resolve(*sv2).asNum();}idx_pairs.push_back({0,sz});}
        auto rd=[&](const uint8_t*& p,int w)->uint64_t{uint64_t r=0;for(int i=0;i<w;++i)r=(r<<8)|*p++;return r;};
        const uint8_t* rp=raw.data(); const uint8_t* re=rp+raw.size();
        for(auto [first,count]:idx_pairs){
            for(int i=0;i<count&&rp<re;++i){
                uint64_t tp=w1?rd(rp,w1):1, f2=w2?rd(rp,w2):0, f3=w3?rd(rp,w3):0;
                int obj=first+i;
                if(xref.find(obj)!=xref.end()) continue;
                if(tp==1){ObjLoc l;l.offset=(size_t)f2;xref[obj]=l;}
                else if(tp==2){ObjLoc l;l.src=ObjLoc::InObjStm;l.stm_obj=(int)f2;l.stm_idx=(int)f3;xref[obj]=l;}
            }
        }
        return true;
    }

    void collectPages(const Value& root, std::vector<Value>& out) {
        // Iterative BFS to avoid stack overflow on deep page trees
        std::vector<Value> queue;
        queue.push_back(root);
        while (!queue.empty() && out.size() < 500) {
            Value node = queue.back(); queue.pop_back();
            Value rv = resolve(node);
            if (!rv.isDict()) continue;
            auto* tv = rv.get("Type"); if (!tv) continue;
            Value tr = resolve(*tv);
            if (tr.sval == "Pages") {
                auto* kids = rv.get("Kids"); if (!kids) continue;
                Value kr = resolve(*kids); if (!kr.isArray()) continue;
                for (auto& k : kr.arr) queue.push_back(k);
            } else if (tr.sval == "Page") {
                out.push_back(rv);
            }
        }
    }

    std::string pdfStrToUtf8(const std::string& s) {
        if(s.size()>=2&&(uint8_t)s[0]==0xFE&&(uint8_t)s[1]==0xFF){
            std::string r; for(size_t i=2;i+1<s.size();i+=2){uint16_t cp=((uint8_t)s[i]<<8)|(uint8_t)s[i+1]; if(cp<0x80)r+=(char)cp; else if(cp<0x800){r+=(char)(0xC0|(cp>>6));r+=(char)(0x80|(cp&0x3F));} else{r+=(char)(0xE0|(cp>>12));r+=(char)(0x80|((cp>>6)&0x3F));r+=(char)(0x80|(cp&0x3F));}} return r;
        }
        std::string r; for(uint8_t c:(const std::string&)s){ if(c<0x80)r+=(char)c; else{r+=(char)(0xC0|(c>>6));r+=(char)(0x80|(c&0x3F));}} return r;
    }

    void extractText(const std::vector<uint8_t>& sd, float ph, std::vector<TextSpan>& out) {
        View v{sd.data(),sd.data()+sd.size()};
        bool in_bt=false;
        float Tm_e=0,Tm_f=0, Tlm_x=0,Tlm_y=0, Tf_sz=12, Tlead=0;
        float Tm_a=1,Tm_b=0;
        std::vector<Value> stack;

        auto pushNum=[&](double n){Value iv;iv.kind=Value::Int;iv.ival=(int)n;iv.fval=n;iv.kind=(n==(int)n)?Value::Int:Value::Real;stack.push_back(iv);};
        (void)pushNum;

        while(v.p<v.end){
            v.skip(); if(v.p>=v.end) break;
            // Skip inline images (BI ... EI)
            if(v.startsWith("BI")){
                const uint8_t* ei=(const uint8_t*)memmem(v.p,v.end-v.p,"EI",2);
                if(ei){v.p=ei+2;stack.clear();continue;} else break;
            }
            Token t=nextToken(v);
            if(t.type==T_EOF) break;
            if(t.type==T_INT)   {Value iv;iv.kind=Value::Int;iv.ival=(int)t.num;iv.fval=t.num;stack.push_back(iv);}
            else if(t.type==T_REAL){Value rv;rv.kind=Value::Real;rv.fval=t.num;stack.push_back(rv);}
            else if(t.type==T_STRING){Value sv;sv.kind=Value::String;sv.sval=t.str;stack.push_back(sv);}
            else if(t.type==T_NAME){Value nv;nv.kind=Value::Name;nv.sval=t.str;stack.push_back(nv);}
            else if(t.type==T_ARROPEN){
                Value arr;arr.kind=Value::Array;
                while(v.p<v.end){
                    View pk=v; Token pt=nextToken(pk);
                    if(pt.type==T_ARRCLOSE){v=pk;break;}
                    if(pt.type==T_EOF) break;
                    if(pt.type==T_INT){Value iv;iv.kind=Value::Int;iv.ival=(int)pt.num;iv.fval=pt.num;arr.arr.push_back(iv);v=pk;}
                    else if(pt.type==T_REAL){Value rv;rv.kind=Value::Real;rv.fval=pt.num;arr.arr.push_back(rv);v=pk;}
                    else if(pt.type==T_STRING){Value sv;sv.kind=Value::String;sv.sval=pt.str;arr.arr.push_back(sv);v=pk;}
                    else if(pt.type==T_NAME){Value nv;nv.kind=Value::Name;nv.sval=pt.str;arr.arr.push_back(nv);v=pk;}
                    else{v=pk;}
                }
                stack.push_back(arr);
            }
            else if(t.type==T_DICTOPEN){int d=1;while(v.p<v.end&&d>0){Token dt=nextToken(v);if(dt.type==T_DICTOPEN)++d;else if(dt.type==T_DICTCLOSE)--d;else if(dt.type==T_EOF)break;}stack.clear();}
            else if(t.type==T_OP){
                const std::string& op=t.str;
                if(op=="BT"){in_bt=true;Tm_a=1;Tm_b=0;Tm_e=0;Tm_f=0;Tlm_x=0;Tlm_y=0;}
                else if(op=="ET"){in_bt=false;}
                else if(op=="Tf"){if(stack.size()>=2)Tf_sz=(float)stack[stack.size()-1].asNum();}
                else if(op=="Td"||op=="TD"){if(stack.size()>=2){float dx=(float)stack[stack.size()-2].asNum(),dy=(float)stack[stack.size()-1].asNum();if(op=="TD")Tlead=-dy;Tlm_x+=dx;Tlm_y+=dy;Tm_e=Tlm_x;Tm_f=Tlm_y;}}
                else if(op=="Tm"){if(stack.size()>=6){Tm_a=(float)stack[stack.size()-6].asNum();Tm_b=(float)stack[stack.size()-5].asNum();Tm_e=(float)stack[stack.size()-2].asNum();Tm_f=(float)stack[stack.size()-1].asNum();Tlm_x=Tm_e;Tlm_y=Tm_f;}}
                else if(op=="T*"||op=="\'"){if(op=="T*"||op=="\'"){Tlm_y-=Tlead;Tm_e=Tlm_x;Tm_f=Tlm_y;}}
                else if(op=="TL"){if(!stack.empty())Tlead=(float)stack.back().asNum();}
                else if((op=="Tj"||op=="\'")&&!stack.empty()&&stack.back().isStr()&&in_bt){
                    TextSpan sp;sp.x=Tm_e;sp.y=ph-Tm_f;sp.fontSize=Tf_sz*sqrtf(Tm_a*Tm_a+Tm_b*Tm_b);
                    sp.text=pdfStrToUtf8(stack.back().sval);
                    if(!sp.text.empty()){out.push_back(sp);if(out.size()>=4000)return;}
                }
                else if(op=="TJ"&&!stack.empty()&&stack.back().isArray()&&in_bt){
                    std::string combined; float cx=Tm_e;
                    for(auto& item:stack.back().arr){if(item.isStr()){combined+=pdfStrToUtf8(item.sval);cx+=item.sval.size()*Tf_sz*0.6f;}else if(item.isNum())cx-=(float)item.asNum()*Tf_sz/1000.f;}
                    if(!combined.empty()){TextSpan sp;sp.x=Tm_e;sp.y=ph-Tm_f;sp.fontSize=Tf_sz*sqrtf(Tm_a*Tm_a+Tm_b*Tm_b);sp.text=combined;out.push_back(sp);if(out.size()>=4000)return;}
                    Tm_e=cx;
                }
                else if(op=="\""&&stack.size()>=3&&in_bt){
                    // aw ac string "
                    if(stack[stack.size()-1].isStr()){TextSpan sp;sp.x=Tm_e;sp.y=ph-Tm_f;sp.fontSize=Tf_sz;sp.text=pdfStrToUtf8(stack[stack.size()-1].sval);if(!sp.text.empty())out.push_back(sp);}
                }
                stack.clear();
            }
            else stack.clear();
        }
    }

    bool parse(std::vector<Page>& pages) {
        ptrdiff_t sxpos=rfind(data,size,"startxref"); if(sxpos<0) return false;
        View v{data+sxpos+9,data+size}; Token xt=nextToken(v); if(xt.type!=T_INT) return false;
        size_t xref_off=(size_t)xt.num;

        // Detect xref table vs xref stream
        ptrdiff_t lk=ffind(data,size,"xref",xref_off);
        if(lk>=0&&(size_t)lk==xref_off) {
            parseXRefTable(xref_off);
            if(trailer.isDict()){if(auto* pv=trailer.get("Prev")){size_t pr=(size_t)resolve(*pv).asNum();parseXRefTable(pr);}}
        } else {
            parseXRefStream(xref_off);
            if(trailer.isDict()){if(auto* pv=trailer.get("Prev")){size_t pr=(size_t)resolve(*pv).asNum();parseXRefStream(pr);}}
        }
        if(xref.empty()) return false;

        if(!trailer.isDict()) return false;
        auto* rr=trailer.get("Root"); if(!rr) return false;
        Value catalog=resolve(*rr); if(!catalog.isDict()) return false;
        auto* pr=catalog.get("Pages"); if(!pr) return false;
        Value pt=resolve(*pr);

        std::vector<Value> raw_pages; collectPages(pt,raw_pages);

        for(auto& pg:raw_pages){
            Page op;
            auto* mb=pg.get("MediaBox");
            if(mb){Value mr=resolve(*mb);if(mr.isArray()&&mr.arr.size()>=4){float x0=(float)resolve(mr.arr[0]).asNum(),y0=(float)resolve(mr.arr[1]).asNum(),x1=(float)resolve(mr.arr[2]).asNum(),y1=(float)resolve(mr.arr[3]).asNum();op.w=x1-x0;op.h=y1-y0;}}
            auto* cc=pg.get("Contents");
            if(cc){
                std::vector<uint8_t> sd;
                auto appendStm=[&](const Value& s){Value sv=resolve(s);if(sv.is_stream){auto d=getStreamBytes(sv);sd.insert(sd.end(),d.begin(),d.end());sd.push_back(' ');}};
                Value cr=resolve(*cc);
                if(cr.isArray()){for(auto& item:cr.arr)appendStm(item);}else appendStm(cr);
                if(!sd.empty()) extractText(sd,op.h,op.spans);
            }
            pages.push_back(op);
            if(pages.size()>=200) break;
        }
        return !pages.empty();
    }
};

// ─── Value parsing (defined after Parser so it can reference it) ──────────────

inline Value Parser::parseValue(View& v) {
    v.skip();
    while(v.p<v.end&&*v.p=='%'){v.skipLine();v.skip();}
    Token t=nextToken(v);
    if(t.type==T_EOF) return {};

    if(t.type==T_INT){
        View saved=v; Token t2=nextToken(v);
        if(t2.type==T_INT){Token t3=nextToken(v);if(t3.type==T_OP&&t3.str=="R"){Value rv;rv.kind=Value::Ref;rv.ref_obj=(int)t.num;rv.ref_gen=(int)t2.num;return rv;}}
        v=saved; Value iv; iv.kind=Value::Int; iv.ival=(int)t.num; iv.fval=t.num; return iv;
    }
    if(t.type==T_REAL){Value rv;rv.kind=Value::Real;rv.fval=t.num;return rv;}
    if(t.type==T_NAME){
        if(t.str=="true"){Value b;b.kind=Value::Bool;b.ival=1;return b;}
        if(t.str=="false"){Value b;b.kind=Value::Bool;b.ival=0;return b;}
        if(t.str=="null") return {};
        Value n;n.kind=Value::Name;n.sval=t.str;return n;
    }
    if(t.type==T_STRING){Value s;s.kind=Value::String;s.sval=t.str;return s;}
    if(t.type==T_ARROPEN){
        Value arr;arr.kind=Value::Array;
        while(true){v.skip();View pk=v;Token pt=nextToken(pk);if(pt.type==T_ARRCLOSE||pt.type==T_EOF){v=pk;break;}arr.arr.push_back(parseValue(v));}
        return arr;
    }
    if(t.type==T_DICTOPEN){
        Value d;d.kind=Value::Dict;
        while(true){
            v.skip();View pk=v;Token kt=nextToken(pk);
            if(kt.type==T_DICTCLOSE||kt.type==T_EOF){v=pk;break;}
            if(kt.type!=T_NAME){v=pk;continue;}
            std::string key=kt.str; Value val=parseValue(pk);
            d.dict[key]=val; v=pk;
        }
        return d;
    }
    return {};
}

// ─── Public API ───────────────────────────────────────────────────────────────

inline Doc load(const uint8_t* data, size_t size) {
    Doc doc; doc.raw.assign(data,data+size);
    Parser p{data,size};
    doc.ok=p.parse(doc.pages);
    if(!doc.ok&&doc.error.empty()) doc.error="Failed to parse PDF structure";
    return doc;
}

} // namespace Pdf
