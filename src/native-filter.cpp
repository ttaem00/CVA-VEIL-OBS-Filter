// Copyright (C) 2026 TRUE ITEM contributors. GPL-2.0-or-later.
// Beauty mathematics and OneEuro port: ctbot000/face-beautifier, MIT.
#include <obs-module.h>
#include <util/platform.h>
#include <util/bmem.h>
#include <windows.h>
#include "beauty_core.h"
#include "face_tracker.h"
#include "landmark_smoother.h"
#include "background_tools.h"
#include "registered_background_feature.h"
#include "video_diagnostics.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void)
{ return "VEIL native face detail, skin, makeup and colour filter"; }

namespace {
using namespace trueitem;
struct Control { const char *key, *label; int BeautyParams::*member; int minimum; };
constexpr Control controls[] = {
 {"smooth", "피부 부드럽게", &BeautyParams::smooth, 0},
 {"texture", "피부 질감 유지", &BeautyParams::texture, 0},
 {"even", "피부 톤 균일화", &BeautyParams::even, 0},
 {"radius", "피부 보정 범위", &BeautyParams::radius, 0},
 {"sharpen", "선명도", &BeautyParams::sharpen, 0},
 {"slim", "얼굴 폭 줄이기", &BeautyParams::slim, 0},
 {"chin", "턱 모양", &BeautyParams::chin, -100},
 {"eyes", "눈 크기", &BeautyParams::eyes, 0},
 {"nose", "코 폭 줄이기", &BeautyParams::nose, 0},
 {"lips", "입술 크기", &BeautyParams::lips, -100},
 {"eyeBright", "눈·치아 밝게", &BeautyParams::eyeBright, 0},
 {"underEye", "눈 밑 밝게", &BeautyParams::underEye, 0},
 {"lipAmount", "입술 색 농도", &BeautyParams::lipAmount, 0},
 {"blushAmount", "볼 색 농도", &BeautyParams::blushAmount, 0},
 {"bright", "밝기", &BeautyParams::bright, -100},
 {"warmth", "색 온도", &BeautyParams::warmth, -100},
 {"vibrance", "생기·채도", &BeautyParams::vibrance, -100},
 {"contrast", "대비", &BeautyParams::contrast, -100},
 {"glow", "은은한 광택", &BeautyParams::glow, 0},
 {"vignette", "가장자리 어둡게", &BeautyParams::vignette, 0},
 {"grain", "필름 입자", &BeautyParams::grain, 0},
};
constexpr Control enhancements[] = {
 {"skinBright","피부 밝기",&BeautyParams::skinBright,0},
 {"redness","피부 붉은기 줄이기",&BeautyParams::redness,0},
 {"teethWhite","치아 화이트닝",&BeautyParams::teethWhite,0},
 {"lipGloss","입술 광택",&BeautyParams::lipGloss,0},
 {"faceLength","얼굴 길이 · 짧게 ↔ 길게",&BeautyParams::faceLength,-100},
 {"eyeSpacing","눈 사이 간격 · 좁게 ↔ 넓게",&BeautyParams::eyeSpacing,-100},
 {"noseLength","코 길이 · 짧게 ↔ 길게",&BeautyParams::noseLength,-100},
};
constexpr const char*lip_palette[]={"#c4485f","#e0624a","#cf2233","#96305a","#b9736a","#7c3557"};
constexpr const char*blush_palette[]={"#ff9f80","#ff8098","#ff8a6b","#d98fb0"};
struct BuiltinPreset {const char*label;std::array<int,21> values;};
constexpr BuiltinPreset builtins[] = {
 {"보정 없음",{0,100,0,50,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
 {"내추럴",{38,35,30,50,18,0,0,0,0,0,22,25,0,0,8,6,6,0,0,0,0}},
 {"글로우",{55,25,45,55,14,0,0,0,0,0,30,35,18,22,16,14,14,0,28,0,0}},
 {"포슬린",{78,12,62,62,10,0,0,0,0,0,35,45,0,0,20,-4,-6,0,18,18,0}},
 {"글램",{60,22,50,55,22,45,20,35,25,0,45,45,55,35,10,6,16,12,0,0,0}},
 {"돌",{72,16,55,60,12,58,26,46,32,12,42,50,42,32,18,8,10,0,22,0,0}},
 {"브라우저 초기값",{45,30,35,50,20,0,0,0,0,0,25,25,0,0,6,4,6,0,0,0,0}},
};

struct InputFrame {std::vector<uint8_t> bgra; int w=0,h=0; uint64_t timestamp=0,generation=0;};
struct Filter {
 obs_source_t *source=nullptr;
 gs_effect_t *effect=nullptr;
 gs_texrender_t *raw=nullptr,*half_a=nullptr,*half_b=nullptr,*quarter_a=nullptr,*quarter_b=nullptr,*small=nullptr;
 gs_stagesurf_t *stages[2]{}; bool stage_valid[2]{}; uint64_t stage_time[2]{}; int stage_index=0,stage_w=0,stage_h=0;
 gs_texture_t *mask_tex=nullptr; int mask_w=0,mask_h=0;
 std::mutex mutex;
 std::condition_variable cv;
 BeautyParams params;
 InputFrame pending;
 bool queued=false,stopping=false;
 LandmarkFrame landmarks;
 MaskImage mask;
 uint64_t sequence=0,uploaded=0,last_submit=0;
 uint64_t input_epoch=0,last_input_frame=0,generation=1;bool input_waiting=false;
 bool runtime_ready=false;
 std::string status="얼굴 인식을 준비하고 있습니다.";
 std::thread worker;
 veil::VideoDiagnostics trace;
};

std::filesystem::path module_dir()
{
 HMODULE module=nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>(&module_dir),&module);
 std::wstring buffer(32768,L'\0');
 DWORD size=GetModuleFileNameW(module,buffer.data(),DWORD(buffer.size()));
 if(!size || size>=buffer.size())return {};
 buffer.resize(size);return std::filesystem::path(buffer).parent_path();
}

void tracking_worker(Filter *f)
{
 FaceTracker tracker; std::string error;
 char *model_path=obs_module_file("models");
 bool ready=model_path && tracker.initialize(std::filesystem::u8path(model_path),module_dir()/L"onnxruntime.dll",2,error);
 bfree(model_path);
 {std::lock_guard<std::mutex> lock(f->mutex);f->runtime_ready=ready;
  f->status=ready?"카메라를 바라보면 얼굴 보정을 시작합니다.":("얼굴 추적을 시작하지 못했습니다: "+error);}
 if(!ready) {f->trace.errors++;blog(LOG_ERROR,"[TRUEITEM] Face tracking initialization failed: %s",error.c_str());return;}
 blog(LOG_INFO,"[TRUEITEM] Native CPU face tracker ready; no browser, camera or network opened");
 LandmarkSmoother smoothing; LandmarkFrame previous; float presence=0; bool logged_face=false;uint64_t generation=0;
 for(;;) {
  InputFrame input;
  {std::unique_lock<std::mutex> lock(f->mutex);
   f->cv.wait_for(lock,std::chrono::seconds(1),[f]{return f->stopping||f->queued;});if(f->stopping)break;
   if(!f->queued){lock.unlock();f->trace.beat("beauty",f->source);continue;}
   input=std::move(f->pending);f->queued=false;}
  if(input.generation!=generation){smoothing.reset();previous={};presence=0;generation=input.generation;}
  f->trace.begin_work(input.timestamp);LandmarkFrame next;next.timestamp_ns=input.timestamp;
  bool found=tracker.detect_bgra(input.bgra.data(),input.w,input.h,input.w*4,next);
  if(!tracker.last_error().empty()){const auto errors=++f->trace.errors;if(errors==1||errors%300==0)blog(LOG_WARNING,"[VEIL trace] Face tracking error (%llu): %s",(unsigned long long)errors,tracker.last_error().c_str());}
  if(found) {
   smoothing.filter(next);presence=std::min(1.f,presence+.16f);next.presence=presence;previous=next;
   if(!logged_face){blog(LOG_INFO,"[TRUEITEM] Face detected: 478 landmarks, input %dx%d",input.w,input.h);logged_face=true;}
  } else {
   presence=std::max(0.f,presence-.08f);next=previous;next.timestamp_ns=input.timestamp;
   next.presence=presence;next.valid=presence>0;if(!next.valid)smoothing.reset();
  }
  auto mask=build_mask(next,float(input.w)/float(input.h),320);
  {std::lock_guard<std::mutex> lock(f->mutex);if(input.generation!=f->generation){f->trace.finish_work(input.timestamp);continue;}f->landmarks=next;f->mask=std::move(mask);++f->sequence;
   f->status=found?"얼굴 인식됨 · 보정 중":"얼굴을 찾고 있습니다. 카메라를 바라봐 주세요.";}
  f->trace.finish_work(input.timestamp);f->trace.beat("beauty",f->source);
 }
}

void update(void *data,obs_data_t *settings)
{
 auto *f=static_cast<Filter*>(data);BeautyParams p;
 for(auto &c:controls)p.*(c.member)=std::clamp(int(obs_data_get_int(settings,c.key)),c.minimum,100);
 for(auto &c:enhancements)p.*(c.member)=std::clamp(int(obs_data_get_int(settings,c.key)),c.minimum,100);
 p.strength=std::clamp(int(obs_data_get_int(settings,"strength")),0,100);
 p.look=std::clamp(int(obs_data_get_int(settings,"look")),0,5);
 p.lookAmount=std::clamp(int(obs_data_get_int(settings,"lookAmount")),0,100);
 p.preview=std::clamp(int(obs_data_get_int(settings,"preview")),0,3);
 p.split=std::clamp(int(obs_data_get_int(settings,"split")),0,100);
 p.natural=obs_data_get_bool(settings,"natural");
 auto color=[settings](const char *key){uint32_t v=uint32_t(obs_data_get_int(settings,key));return std::array<float,3>{float(v&255)/255,float((v>>8)&255)/255,float((v>>16)&255)/255};};
 p.lip_rgb=color("lipColor");p.blush_rgb=color("blushColor");p.mirror=obs_data_get_bool(settings,"mirror");
 std::lock_guard<std::mutex>lock(f->mutex);f->params=p;
}
void defaults(obs_data_t *d)
{
 BeautyParams p;for(auto &c:controls)obs_data_set_default_int(d,c.key,p.*(c.member));
 for(auto &c:enhancements)obs_data_set_default_int(d,c.key,0);
 obs_data_set_default_int(d,"strength",100);obs_data_set_default_int(d,"look",0);
 obs_data_set_default_int(d,"lookAmount",40);obs_data_set_default_int(d,"preview",0);
 obs_data_set_default_int(d,"split",50);obs_data_set_default_int(d,"section",0);
 obs_data_set_default_bool(d,"natural",true);
 veil::background_defaults(d);
 obs_data_set_default_int(d,"builtin",-1);
 obs_data_set_default_int(d,"lip_palette",-1);obs_data_set_default_int(d,"blush_palette",-1);
 obs_data_set_default_int(d,"lip_palette_applied",-1);obs_data_set_default_int(d,"blush_palette_applied",-1);
 obs_data_set_default_int(d,"lipColor",0xff5f48c4);obs_data_set_default_int(d,"blushColor",0xff6b8aff);
 obs_data_set_default_bool(d,"mirror",true);
 obs_data_set_default_string(d,"preset_name","내 방송 설정 2026-11-03");
 char *path=obs_module_config_path("my-broadcast.json");
 if(path){obs_data_set_default_string(d,"preset_save",path);bfree(path);}
}

bool has_type(obs_data_t*d,const char*k,obs_data_type type)
{obs_data_item_t*i=obs_data_item_byname(d,k);bool ok=i&&obs_data_item_gettype(i)==type;obs_data_item_release(&i);return ok;}
bool exact_keys(obs_data_t*d,std::initializer_list<const char*> keys)
{
 if(!d)return false;size_t count=0;obs_data_item_t*i=obs_data_first(d);
 while(i){++count;bool found=false;for(auto k:keys)if(std::string(obs_data_item_get_name(i))==k)found=true;
  if(!found){obs_data_item_release(&i);return false;}obs_data_item_next(&i);}
 return count==keys.size();
}
bool parse_color(const char *s,uint32_t &out)
{
 if(!s||std::strlen(s)!=7||*s!='#')return false;unsigned v=0;
 for(int i=1;i<7;++i){char c=s[i];int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
  if(n<0)return false;v=v*16+unsigned(n);}
 out=0xff000000|((v&255)<<16)|(v&0xff00)|((v>>16)&255);return true;
}
bool import_preset(obs_data_t *preset,obs_data_t *target)
{
 const bool legacy=std::string(obs_data_get_string(preset,"schema"))=="trueitem.face-beautifier.preset"&&obs_data_get_double(preset,"version")==1;
 const bool v5=obs_data_get_double(preset,"version")==5;
 const bool v4=obs_data_get_double(preset,"version")==4||v5;
 const bool v3=obs_data_get_double(preset,"version")==3||v4;
 const bool veil=std::string(obs_data_get_string(preset,"schema"))=="veil.obs-beauty.preset"&&(obs_data_get_double(preset,"version")==2||v3);
 if((!legacy&&!veil)||!(legacy?exact_keys(preset,{"schema","version","name","values","colors","mirror"}):
    v3?exact_keys(preset,{"schema","version","name","values","colors","mirror","enhancements","natural","background"}):exact_keys(preset,{"schema","version","name","values","colors","mirror","enhancements"}))||
    !has_type(preset,"version",OBS_DATA_NUMBER)||
    !has_type(preset,"name",OBS_DATA_STRING)||!has_type(preset,"mirror",OBS_DATA_BOOLEAN))return false;
 obs_data_t *values=obs_data_get_obj(preset,"values"),*colors=obs_data_get_obj(preset,"colors");
 if(!values||!colors){obs_data_release(values);obs_data_release(colors);return false;}
 bool valid=exact_keys(colors,{"lip","blush"});size_t count=0;
 if(values){obs_data_item_t*i=obs_data_first(values);while(i){++count;obs_data_item_next(&i);}}
 valid=valid&&count==std::size(controls);uint32_t lip=0,blush=0;
 for(auto&c:controls){double n=obs_data_get_double(values,c.key);valid=valid&&has_type(values,c.key,OBS_DATA_NUMBER)&&std::isfinite(n)&&n==std::floor(n)&&n>=c.minimum&&n<=100;}
 valid=valid&&parse_color(obs_data_get_string(colors,"lip"),lip)&&parse_color(obs_data_get_string(colors,"blush"),blush);
 auto*extra=obs_data_get_obj(preset,"enhancements");
 if(veil){
  valid=valid&&exact_keys(extra,{"skinBright","redness","teethWhite","lipGloss","faceLength","eyeSpacing","noseLength","strength","look","lookAmount"});
  for(auto&c:enhancements){double n=obs_data_get_double(extra,c.key);valid=valid&&has_type(extra,c.key,OBS_DATA_NUMBER)&&std::isfinite(n)&&n==std::floor(n)&&n>=c.minimum&&n<=100;}
  for(const auto&key:{"strength","look","lookAmount"}){double n=obs_data_get_double(extra,key);valid=valid&&has_type(extra,key,OBS_DATA_NUMBER)&&std::isfinite(n)&&n==std::floor(n)&&n>=0&&n<=(std::string(key)=="look"?5:100);}
 }
 auto*background=obs_data_get_obj(preset,"background");uint32_t key_color=0;
 if(veil&&v3){
  valid=valid&&has_type(preset,"natural",OBS_DATA_BOOLEAN)&&exact_keys(background,{"mode","color","similarity","smoothness","spill","stability","feather"})&&parse_color(obs_data_get_string(background,"color"),key_color);
  for(const char*k:{"mode","similarity","smoothness","spill","stability","feather"}){
   const std::string key=k;double n=obs_data_get_double(background,k);int maximum=key=="mode"?(v4?3:2):key=="feather"?(v5?50:5):key=="stability"?100:1000;
   valid=valid&&has_type(background,k,OBS_DATA_NUMBER)&&std::isfinite(n)&&((v5&&key=="feather")||n==std::floor(n))&&n>=(maximum==1000?1:0)&&n<=maximum;
  }
 }
 if(valid){for(auto&c:controls)obs_data_set_int(target,c.key,obs_data_get_int(values,c.key));
  for(auto&c:enhancements)obs_data_set_int(target,c.key,veil?obs_data_get_int(extra,c.key):0);
  obs_data_set_int(target,"strength",veil?obs_data_get_int(extra,"strength"):100);
  obs_data_set_int(target,"look",veil?obs_data_get_int(extra,"look"):0);
  obs_data_set_int(target,"lookAmount",veil?obs_data_get_int(extra,"lookAmount"):40);
  obs_data_set_int(target,"lipColor",lip);obs_data_set_int(target,"blushColor",blush);
  obs_data_set_bool(target,"mirror",obs_data_get_bool(preset,"mirror"));
  obs_data_set_bool(target,"natural",veil&&v3?obs_data_get_bool(preset,"natural"):true);
  if(veil&&v3){
   const std::pair<const char*,const char*>keys[]={{"mode","background_mode"},{"similarity","background_similarity"},{"smoothness","background_smoothness"},{"spill","background_spill"},{"stability","background_stability"},{"feather","background_feather"}};
   for(auto&k:keys)obs_data_set_int(target,k.second,obs_data_get_int(background,k.first));
   obs_data_set_double(target,"background_feather",obs_data_get_double(background,"feather"));obs_data_set_int(target,"background_color",key_color);
   if(!veil::registered_background_enabled&&obs_data_get_int(target,"background_mode")==3)obs_data_set_int(target,"background_mode",1);
   obs_data_set_int(target,"background_controls_version",1);
  }
  obs_data_set_int(target,"preview",0);
  for(const char*k:{"lip_palette","blush_palette","lip_palette_applied","blush_palette_applied"})obs_data_set_int(target,k,-1);
  obs_data_set_string(target,"preset_name",obs_data_get_string(preset,"name"));}
 obs_data_release(background);obs_data_release(extra);obs_data_release(values);obs_data_release(colors);return valid;
}
obs_data_t *export_preset(obs_data_t*d)
{
 auto *p=obs_data_create(),*v=obs_data_create(),*c=obs_data_create(),*extra=obs_data_create();
 obs_data_set_string(p,"schema","veil.obs-beauty.preset");obs_data_set_int(p,"version",5);
 obs_data_set_bool(p,"natural",obs_data_get_bool(d,"natural"));
 obs_data_set_string(p,"name",obs_data_get_string(d,"preset_name"));obs_data_set_bool(p,"mirror",obs_data_get_bool(d,"mirror"));
 for(auto&control:controls)obs_data_set_int(v,control.key,obs_data_get_int(d,control.key));
 for(auto&control:enhancements)obs_data_set_int(extra,control.key,obs_data_get_int(d,control.key));
 for(const char*key:{"strength","look","lookAmount"})obs_data_set_int(extra,key,obs_data_get_int(d,key));
 obs_data_set_obj(p,"enhancements",extra);obs_data_release(extra);
 auto color=[&](const char*from,const char*to){auto n=uint32_t(obs_data_get_int(d,from));char s[8];
  std::snprintf(s,sizeof(s),"#%02x%02x%02x",n&255,(n>>8)&255,(n>>16)&255);obs_data_set_string(c,to,s);};
 color("lipColor","lip");color("blushColor","blush");obs_data_set_obj(p,"values",v);obs_data_set_obj(p,"colors",c);
 auto*background=obs_data_create();const std::pair<const char*,const char*>keys[]={{"mode","background_mode"},{"similarity","background_similarity"},{"smoothness","background_smoothness"},{"spill","background_spill"},{"stability","background_stability"},{"feather","background_feather"}};
 for(auto&k:keys)obs_data_set_int(background,k.first,obs_data_get_int(d,k.second));
 obs_data_set_double(background,"feather",obs_data_get_double(d,"background_feather"));
 auto n=uint32_t(obs_data_get_int(d,"background_color"));char key_color[8];std::snprintf(key_color,sizeof(key_color),"#%02x%02x%02x",n&255,(n>>8)&255,(n>>16)&255);obs_data_set_string(background,"color",key_color);
 obs_data_set_obj(p,"background",background);obs_data_release(background);
 obs_data_release(v);obs_data_release(c);return p;
}
bool section_changed(obs_properties_t*,obs_property_t*,obs_data_t*);

bool preset_click(obs_properties_t *props,obs_property_t *property,void *data)
{
 auto*f=static_cast<Filter*>(data);if(!f)return false;
 auto*d=obs_source_get_settings(f->source);std::string action=obs_property_name(property),message;
 bool success=false,with_background=false,background_success=true;
 try {
  if(action=="preset_restore"||(action=="preset_builtin"&&obs_data_get_int(d,"builtin")==-1)) {
   char*path=obs_module_file("user-preset.json");auto*p=path?obs_data_create_from_json_file(path):nullptr;bfree(path);
   success=p&&import_preset(p,d);with_background=success&&has_type(p,"background",OBS_DATA_OBJECT);obs_data_release(p);
  } else if(action=="preset_builtin") {
   int index=int(obs_data_get_int(d,"builtin"));
   if(index>=0&&index<int(std::size(builtins))){
    const auto&p=builtins[index];for(size_t i=0;i<std::size(controls);++i)obs_data_set_int(d,controls[i].key,p.values[i]);
    for(auto&c:enhancements)obs_data_set_int(d,c.key,0);
    obs_data_set_int(d,"strength",100);obs_data_set_int(d,"look",0);obs_data_set_int(d,"preview",0);
    obs_data_set_string(d,"preset_name",p.label);success=true;
    if(index==6){obs_data_set_int(d,"lipColor",0xff5f48c4);obs_data_set_int(d,"blushColor",0xff809fff);
     for(const char*k:{"lip_palette","blush_palette","lip_palette_applied","blush_palette_applied"})obs_data_set_int(d,k,-1);}
   }
  } else if(action=="preset_import") {
   const char*path=obs_data_get_string(d,"preset_load");auto file=std::filesystem::u8path(path);
   if(!file.empty()&&std::filesystem::file_size(file)<=65536){auto*p=obs_data_create_from_json_file(path);success=p&&import_preset(p,d);with_background=success&&has_type(p,"background",OBS_DATA_OBJECT);obs_data_release(p);}
  } else {
   std::string path=obs_data_get_string(d,"preset_save");auto file=std::filesystem::u8path(path);
   if(!file.empty()&&file.extension()==L".json"){
    if(file.has_parent_path())std::filesystem::create_directories(file.parent_path());
    auto*snapshot=obs_data_get_defaults(d);obs_data_apply(snapshot,d);veil::background_snapshot(f->source,snapshot);
    auto*p=export_preset(snapshot);obs_data_release(snapshot);success=obs_data_save_json_safe(p,path.c_str(),"tmp","bak");obs_data_release(p);
   }
  }
 }catch(const std::exception&){success=false;}
 if(success&&action!="preset_export"){
  if(with_background)background_success=veil::background_apply_preset(f->source,d);
  obs_source_update(f->source,d);
  section_changed(props,nullptr,d);
 }
 message=success?(action=="preset_export"?"현재 설정을 저장했습니다.":"프리셋을 적용했습니다."):
  "프리셋을 처리하지 못했습니다. VEIL·브라우저 프리셋 파일인지, 저장 경로가 .json으로 끝나는지 확인해 주세요.";
 if(success&&!background_success)message="얼굴 설정은 적용했지만 배경 필터를 적용하지 못했습니다. 카메라에 VEIL 필터를 추가했는지 확인하고 VEIL을 다시 설치해 주세요.";
 // Text is shown inside OBS rather than opening a blocking native dialog.
 obs_property_set_description(obs_properties_get(props,"preset_result"),message.c_str());
 obs_property_set_visible(obs_properties_get(props,"preset_result"),true);
 obs_property_text_set_info_type(obs_properties_get(props,"preset_result"),success&&background_success?OBS_TEXT_INFO_NORMAL:OBS_TEXT_INFO_ERROR);
 if(success&&action!="preset_export")obs_property_set_description(obs_properties_get(props,"active_preset"),obs_data_get_string(d,"preset_name"));
 obs_data_release(d);
 return true;
}
bool section_changed(obs_properties_t*p,obs_property_t*,obs_data_t*d)
{
 int section=int(obs_data_get_int(d,"section"));
 const char*groups[]={"quick","skin","shape","makeup","colour","files","tools","background"};
 for(int i=0;i<8;++i)obs_property_set_visible(obs_properties_get(p,groups[i]),i==section);
 obs_property_set_visible(obs_properties_get(p,"quick_presets"),section==0);
 obs_property_set_visible(obs_properties_get(p,"quick_options"),section==0);
 obs_property_set_visible(obs_properties_get(p,"preset_result"),false);
 const int preview=std::clamp(int(obs_data_get_int(d,"preview")),0,3);
 const char *output_notices[]={
  "",
  "원본 영상이 방송과 녹화에 표시됩니다.",
  "좌우 비교 화면이 방송과 녹화에 표시됩니다.",
  "마스크 화면이 방송과 녹화에 표시됩니다.",
 };
 auto *notice=obs_properties_get(p,"preview_notice");
 obs_property_set_description(notice,output_notices[preview]);
 obs_property_set_visible(notice,preview!=0);
 obs_property_set_visible(obs_properties_get(p,"preview_reset"),preview!=0);
 obs_property_set_visible(obs_properties_get(p,"split"),section==0&&preview==2);
 obs_property_set_visible(obs_properties_get(p,"lookAmount"),obs_data_get_int(d,"look")!=0);
 return true;
}
bool reset_preview(obs_properties_t *props, obs_property_t *, void *data)
{
 auto *filter=static_cast<Filter*>(data);
 if(!filter)return false;
 auto *settings=obs_source_get_settings(filter->source);
 obs_data_set_int(settings,"preview",0);
 obs_source_update(filter->source,settings);
 section_changed(props,nullptr,settings);
 obs_data_release(settings);
 return true;
}
bool refresh_status(obs_properties_t*p,obs_property_t*,void*data)
{
 auto*f=static_cast<Filter*>(data);if(!f)return false;
 std::lock_guard<std::mutex>lock(f->mutex);
 obs_property_set_description(obs_properties_get(p,"tracking_status"),f->input_waiting?"카메라에서 새 영상이 들어오기를 기다리고 있습니다.":f->status.c_str());return true;
}
bool palette_changed(obs_properties_t*,obs_property_t*property,obs_data_t*d)
{
 const bool is_lip=std::string(obs_property_name(property))=="lip_palette";
 int index=int(obs_data_get_int(d,is_lip?"lip_palette":"blush_palette"));uint32_t color=0;
 const char*applied=is_lip?"lip_palette_applied":"blush_palette_applied";
 if(index!=obs_data_get_int(d,applied)&&index>=0&&index<(is_lip?6:4)&&parse_color(is_lip?lip_palette[index]:blush_palette[index],color)){
  obs_data_set_int(d,is_lip?"lipColor":"blushColor",color);obs_data_set_int(d,applied,index);
 }
 return true;
}
bool custom_color_changed(obs_properties_t*,obs_property_t*property,obs_data_t*d)
{
 bool lip=std::string(obs_property_name(property))=="lipColor";
 const char*key=lip?"lip_palette":"blush_palette";int index=int(obs_data_get_int(d,key));uint32_t color=0;
 if(index>=0&&index<(lip?6:4)&&parse_color(lip?lip_palette[index]:blush_palette[index],color)&&
    uint32_t(obs_data_get_int(d,lip?"lipColor":"blushColor"))!=color){
  obs_data_set_int(d,key,-1);obs_data_set_int(d,lip?"lip_palette_applied":"blush_palette_applied",-1);
 }
 return true;
}
obs_properties_t *properties(void *data)
{
 auto*f=static_cast<Filter*>(data);auto*p=obs_properties_create();
 auto*nav=obs_properties_add_list(p,"section","조절할 항목",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
 const char*sections[]={"빠른 설정","피부","얼굴 디테일","메이크업","빛·색감","프리셋","상태","배경 필터 안내"};
 for(int i=0;i<8;++i)obs_property_list_add_int(nav,sections[i],i);
 obs_property_set_modified_callback(nav,section_changed);

 auto*notice=obs_properties_add_text(p,"preview_notice","",OBS_TEXT_INFO);
 obs_property_text_set_info_type(notice,OBS_TEXT_INFO_WARNING);
 obs_properties_add_button2(p,"preview_reset","보정 영상으로 돌아가기",reset_preview,f);

 auto*quick=obs_properties_create();
 auto*strength=obs_properties_add_int_slider(quick,"strength","전체 보정 강도",0,100,1);
 obs_property_int_set_suffix(strength,"%");
 obs_property_set_long_description(strength,"0%는 얼굴·색감 보정을 끕니다. 좌우 반전과 별도 배경 필터는 유지됩니다.");
 auto*mode=obs_properties_add_list(quick,"preview","출력 보기",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
 obs_property_list_add_int(mode,"보정 영상",0);obs_property_list_add_int(mode,"원본 확인",1);
 obs_property_list_add_int(mode,"좌우 비교 · 왼쪽 원본",2);obs_property_list_add_int(mode,"마스크 확인 · 방송에 표시됨",3);
 obs_property_set_modified_callback(mode,section_changed);
 obs_property_int_set_suffix(obs_properties_add_int_slider(quick,"split","비교선 위치",0,100,1),"%");
 obs_properties_add_group(p,"quick","보정과 출력",OBS_GROUP_NORMAL,quick);

 auto*presets=obs_properties_create();
 auto*current=f?obs_source_get_settings(f->source):nullptr;
 obs_properties_add_text(presets,"active_preset",current?obs_data_get_string(current,"preset_name"):"내 방송 설정",OBS_TEXT_INFO);
 obs_data_release(current);
 auto*builtin=obs_properties_add_list(presets,"builtin","기본 프리셋",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
 obs_property_list_add_int(builtin,"내가 저장한 방송 설정",-1);
 for(size_t i=0;i<std::size(builtins);++i)obs_property_list_add_int(builtin,builtins[i].label,int(i));
 obs_properties_add_button2(presets,"preset_builtin","프리셋 적용",preset_click,f);
 obs_properties_add_group(p,"quick_presets","프리셋",OBS_GROUP_NORMAL,presets);

 auto*options=obs_properties_create();
 obs_properties_add_bool(options,"mirror","좌우 반전");
 obs_property_set_long_description(obs_properties_add_bool(options,"natural","자연스러운 얼굴형"),"얼굴 변형이 겹칠 때 과도한 늘어남을 줄입니다. 끄면 이전 보정 방식을 사용합니다.");
 obs_properties_add_group(p,"quick_options","추가 설정",OBS_GROUP_NORMAL,options);
 const char *names[]={"피부","얼굴 디테일","메이크업","빛·색감"};
 const char *ids[]={"skin","shape","makeup","colour"};
 const size_t boundaries[]={0,5,10,14,21};
 for(int g=0;g<4;++g){auto*group=obs_properties_create();for(size_t i=boundaries[g];i<boundaries[g+1];++i){const auto&c=controls[i];obs_properties_add_int_slider(group,c.key,c.label,c.minimum,100,1);}
  for(size_t i=0;i<std::size(enhancements);++i)if((g==0&&i<2)||(g==1&&i>=4)||(g==2&&i>=2&&i<4)){const auto&c=enhancements[i];obs_properties_add_int_slider(group,c.key,c.label,c.minimum,100,1);}
  if(g==1)for(const char*key:{"faceLength","eyeSpacing","noseLength"})obs_property_set_long_description(obs_properties_get(group,key),"0은 원래 얼굴입니다. 음수는 짧게·좁게, 양수는 길게·넓게 조절합니다.");
  if(g==2){
   auto*lip=obs_properties_add_list(group,"lip_palette","입술 팔레트",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
   const char*lips[]={"로즈","코랄","레드","베리","누드","플럼"};obs_property_list_add_int(lip,"직접 지정한 색",-1);
   for(int i=0;i<6;++i)obs_property_list_add_int(lip,lips[i],i);obs_property_set_modified_callback(lip,palette_changed);
   obs_property_set_modified_callback(obs_properties_add_color(group,"lipColor","입술 색 직접 지정"),custom_color_changed);
   auto*blush=obs_properties_add_list(group,"blush_palette","볼 팔레트",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
   const char*blushes[]={"피치","로즈","코랄","모브"};obs_property_list_add_int(blush,"직접 지정한 색",-1);
   for(int i=0;i<4;++i)obs_property_list_add_int(blush,blushes[i],i);obs_property_set_modified_callback(blush,palette_changed);
   obs_property_set_modified_callback(obs_properties_add_color(group,"blushColor","볼 색 직접 지정"),custom_color_changed);
  }
  if(g==3){
   auto*look=obs_properties_add_list(group,"look","컬러 필터",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
   const char*looks[]={"없음","로지","피치","쿨 클린","소프트 필름","모노"};
   for(int i=0;i<6;++i)obs_property_list_add_int(look,looks[i],i);obs_property_set_modified_callback(look,section_changed);
   obs_properties_add_int_slider(group,"lookAmount","컬러 필터 강도",0,100,1);
  }
  obs_properties_add_group(p,ids[g],names[g],OBS_GROUP_NORMAL,group);
 }
 auto*files=obs_properties_create();
 obs_properties_add_text(files,"preset_name","프리셋 이름",OBS_TEXT_DEFAULT);
 obs_properties_add_path(files,"preset_save","저장 위치 (.json)",OBS_PATH_FILE_SAVE,"JSON (*.json)",nullptr);
 obs_properties_add_button2(files,"preset_export","현재 설정 저장",preset_click,f);
 obs_properties_add_path(files,"preset_load","불러올 파일 (.json)",OBS_PATH_FILE,"JSON (*.json)",nullptr);
 obs_properties_add_button2(files,"preset_import","파일에서 불러오기",preset_click,f);
 obs_property_set_long_description(obs_properties_add_button2(files,"preset_restore","내 방송 설정 복원",preset_click,f),"처음 저장한 개인 방송 설정으로 되돌립니다.");
 obs_property_set_long_description(obs_properties_get(files,"preset_export"),"얼굴 보정과 전용 배경 필터의 현재 값을 JSON으로 저장합니다.");
 obs_property_set_long_description(obs_properties_get(files,"preset_import"),"브라우저·이전 VEIL 프리셋도 지원합니다. 배경을 포함한 파일은 배경 필터에도 함께 적용합니다.");
 obs_properties_add_group(p,"files","프리셋 관리",OBS_GROUP_NORMAL,files);
 auto*tools=obs_properties_create();
 std::string status="필터를 추가하면 얼굴 인식을 시작합니다.";
 if(f){std::lock_guard<std::mutex>lock(f->mutex);status=f->input_waiting?"카메라에서 새 영상이 들어오기를 기다리고 있습니다.":f->status;}
 obs_properties_add_text(tools,"tracking_status",status.c_str(),OBS_TEXT_INFO);
 obs_properties_add_button2(tools,"refresh_status","상태 새로고침",refresh_status,f);
 obs_property_set_long_description(obs_properties_get(tools,"refresh_status"),"얼굴 인식 상태를 갱신합니다. 한 얼굴을 기준으로 동작하며, 카메라·해상도는 OBS 영상 캡처 장치 속성에서 변경합니다.");
 obs_properties_add_text(tools,"version","VEIL " VEIL_VERSION,OBS_TEXT_INFO);
 obs_properties_add_group(p,"tools","상태 확인",OBS_GROUP_NORMAL,tools);
 veil::background_properties(p,f?f->source:nullptr);
 obs_properties_add_text(p,"preset_result","",OBS_TEXT_INFO);
 auto*d=f?obs_source_get_settings(f->source):obs_data_create();section_changed(p,nullptr,d);obs_data_release(d);
 return p;
}

void destroy(void *data)
{
 auto*f=static_cast<Filter*>(data);if(!f)return;
 {std::lock_guard<std::mutex>lock(f->mutex);f->stopping=true;}f->cv.notify_one();
 if(f->worker.joinable())f->worker.join();
 obs_enter_graphics();gs_effect_destroy(f->effect);
 for(auto*t:{f->raw,f->half_a,f->half_b,f->quarter_a,f->quarter_b,f->small})gs_texrender_destroy(t);
 for(auto*s:f->stages)gs_stagesurface_destroy(s);gs_texture_destroy(f->mask_tex);obs_leave_graphics();delete f;
}
void *create(obs_data_t *settings,obs_source_t *context)
{
 auto*f=new Filter;f->source=context;update(f,settings);
 obs_enter_graphics();char*path=obs_module_file("beauty.effect"),*error=nullptr;
 if(path)f->effect=gs_effect_create_from_file(path,&error);bfree(path);
 if(!f->effect){blog(LOG_ERROR,"[TRUEITEM] Cannot load beauty.effect: %s",error?error:"file missing");bfree(error);obs_leave_graphics();destroy(f);return nullptr;}
 bfree(error);
 f->raw=gs_texrender_create(GS_RGBA,GS_ZS_NONE);f->half_a=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 f->half_b=gs_texrender_create(GS_RGBA,GS_ZS_NONE);f->quarter_a=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 f->quarter_b=gs_texrender_create(GS_RGBA,GS_ZS_NONE);f->small=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 uint8_t black[4]={0,0,0,255};const uint8_t*ptr=black;f->mask_tex=gs_texture_create(1,1,GS_RGBA,1,&ptr,GS_DYNAMIC);
 f->mask_w=f->mask_h=1;obs_leave_graphics();
 try{f->worker=std::thread(tracking_worker,f);}catch(...){destroy(f);return nullptr;}
 return f;
}
bool begin_target(gs_texrender_t*t,int w,int h)
{
 gs_texrender_reset(t);if(!gs_texrender_begin(t,uint32_t(w),uint32_t(h)))return false;
 vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,float(w),0,float(h),-100,100);return true;
}
void set_texture(gs_effect_t*e,const char*k,gs_texture_t*t){gs_effect_set_texture(gs_effect_get_param_by_name(e,k),t);}
void set_float(gs_effect_t*e,const char*k,float v){gs_effect_set_float(gs_effect_get_param_by_name(e,k),v);}
void blur(Filter*f,gs_texture_t*input,gs_texrender_t*target,int w,int h,float x,float y)
{
 if(!begin_target(target,w,h))return;set_texture(f->effect,"uTex",input);
 vec2 step;vec2_set(&step,x,y);gs_effect_set_vec2(gs_effect_get_param_by_name(f->effect,"uStep"),&step);
 while(gs_effect_loop(f->effect,"Blur"))gs_draw_sprite(input,0,uint32_t(w),uint32_t(h));gs_texrender_end(target);
}
void submit_frame(Filter*f,gs_texture_t*input,int width,int height)
{
 uint64_t now=os_gettime_ns();if(now-f->last_submit<33000000)return;
 const bool asynchronous=f->trace.async_input(f->source);const auto input_frame=f->trace.source_frames.load();
 if(asynchronous&&input_frame==f->last_input_frame)return;
 {std::lock_guard<std::mutex>lock(f->mutex);if(!f->runtime_ready||f->queued)return;}
 const int w=320,h=std::max(1,int(std::lround(double(height)*320/width)));
 if(h>2048)return;
 if(f->stage_w!=w||f->stage_h!=h){for(auto&s:f->stages){gs_stagesurface_destroy(s);s=gs_stagesurface_create(w,h,GS_RGBA);}
  f->stage_w=w;f->stage_h=h;f->stage_valid[0]=f->stage_valid[1]=false;}
 if(!begin_target(f->small,w,h))return;
 auto*e=obs_get_base_effect(OBS_EFFECT_DEFAULT);set_texture(e,"image",input);
 while(gs_effect_loop(e,"Draw"))gs_draw_sprite(input,0,w,h);gs_texrender_end(f->small);
 int current=f->stage_index,old=1-current;
 gs_stage_texture(f->stages[current],gs_texrender_get_texture(f->small));f->stage_valid[current]=true;f->stage_time[current]=now;
 if(f->stage_valid[old]){InputFrame in;in.w=w;in.h=h;in.timestamp=f->stage_time[old];in.bgra.resize(size_t(w)*h*4);uint8_t*data=nullptr;uint32_t stride=0;
  if(gs_stagesurface_map(f->stages[old],&data,&stride)){
   f->trace.capture(data,stride,w,h);
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto*s=data+y*stride+x*4;auto*d=in.bgra.data()+(size_t(y)*w+x)*4;
    d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=s[3];}
   gs_stagesurface_unmap(f->stages[old]);
   {std::lock_guard<std::mutex>lock(f->mutex);in.generation=f->generation;f->pending=std::move(in);f->queued=true;}f->cv.notify_one();
  }else f->trace.maps_failed++;
 }
 f->stage_index=old;f->last_submit=now;f->last_input_frame=input_frame;
}
void bind_uniforms(Filter*f,const CoreUniforms&u,const BeautyParams&p)
{
 auto*e=f->effect;auto&s=u.shader;
 set_float(e,"uAspect",u.aspect);set_float(e,"uFace",u.face);set_float(e,"uTime",float(double(os_gettime_ns()%1000000000000ULL)/1e9));
 set_float(e,"uMirror",u.mirror?1.f:0.f);
 set_float(e,"uNatural",u.natural?1.f:0.f);
 set_float(e,"uCompare",p.preview==1?1.2f:p.preview==2?p.split/100.f:-1.f);
 set_float(e,"uStrength",p.strength/100.f);set_float(e,"uLookAmount",p.lookAmount/100.f);
 gs_effect_set_int(gs_effect_get_param_by_name(e,"uLook"),p.look);
 gs_effect_set_int(gs_effect_get_param_by_name(e,"uWarpCount"),u.warp_count);
 gs_effect_set_val(gs_effect_get_param_by_name(e,"uWarpP"),u.warp_p.data(),sizeof(u.warp_p));
 gs_effect_set_val(gs_effect_get_param_by_name(e,"uWarpQ"),u.warp_q.data(),sizeof(u.warp_q));
 auto v4=[e](const char*k,const Vec4&v){gs_effect_set_val(gs_effect_get_param_by_name(e,k),v.data(),sizeof(v));};
 v4("uEyeL",u.eye_l);v4("uEyeR",u.eye_r);v4("uCheekL",u.cheek_l);v4("uCheekR",u.cheek_r);
 v4("uMouth",u.mouth);
 gs_effect_set_val(gs_effect_get_param_by_name(e,"uLipColor"),s.lipColor.data(),sizeof(s.lipColor));
 gs_effect_set_val(gs_effect_get_param_by_name(e,"uBlushColor"),s.blushColor.data(),sizeof(s.blushColor));
 const std::pair<const char*,float> values[]={
  {"uSmooth",s.smooth},{"uTexture",s.texture},{"uEven",s.even},{"uSharpen",s.sharpen},
  {"uBright",s.bright},{"uWarmth",s.warmth},{"uVibrance",s.vibrance},{"uContrast",s.contrast},
  {"uGlow",s.glow},{"uVignette",s.vignette},{"uGrain",s.grain},{"uEyeBright",s.eyeBright},
  {"uUnderEye",s.underEye},{"uLipAmount",s.lipAmount},{"uBlushAmount",s.blushAmount},
  {"uSkinBright",s.skinBright},{"uRedness",s.redness},{"uTeethWhite",s.teethWhite},{"uLipGloss",s.lipGloss}};
 for(auto&kv:values)set_float(e,kv.first,kv.second);
}
void render(void *data,gs_effect_t*)
{
 auto*f=static_cast<Filter*>(data);veil::RenderTraceScope trace(f->trace);auto*target=obs_filter_get_target(f->source);
 const bool asynchronous=f->trace.async_input(f->source),waiting=asynchronous&&f->trace.waiting_for_input(os_gettime_ns());const auto epoch=f->trace.source_epoch.load();
 {std::lock_guard<std::mutex>lock(f->mutex);
  if((waiting&&!f->input_waiting)||(asynchronous&&epoch!=f->input_epoch)){
   f->generation++;f->queued=false;f->landmarks={};f->stage_valid[0]=f->stage_valid[1]=false;f->last_input_frame=0;
  }
  f->input_waiting=waiting;f->input_epoch=epoch;
 }
 if(waiting){obs_source_skip_video_filter(f->source);return;}
 uint32_t w=target?obs_source_get_base_width(target):0,h=target?obs_source_get_base_height(target):0;
 if(!w||!h||!f->effect){obs_source_skip_video_filter(f->source);return;}
 f->trace.phase=2;if(!obs_source_process_filter_begin(f->source,GS_RGBA,OBS_NO_DIRECT_RENDERING)){f->trace.begins_failed++;return;}
 const bool old_linear=gs_set_linear_srgb(false),old_srgb=gs_framebuffer_srgb_enabled();gs_enable_framebuffer_srgb(false);
 gs_blend_state_push();gs_blend_function(GS_BLEND_ONE,GS_BLEND_ZERO);
 if(!begin_target(f->raw,w,h)){
  f->trace.targets_failed++;
  gs_blend_state_pop();gs_enable_framebuffer_srgb(old_srgb);gs_set_linear_srgb(old_linear);
  obs_source_process_filter_end(f->source,obs_get_base_effect(OBS_EFFECT_DEFAULT),w,h);return;}
 f->trace.phase=3;obs_source_process_filter_end(f->source,obs_get_base_effect(OBS_EFFECT_DEFAULT),w,h);gs_texrender_end(f->raw);
 auto*src=gs_texrender_get_texture(f->raw);f->trace.phase=4;submit_frame(f,src,int(w),int(h));f->trace.phase=5;
 BeautyParams params;LandmarkFrame points;
 {std::lock_guard<std::mutex>lock(f->mutex);params=f->params;points=f->landmarks;
  if(f->sequence!=f->uploaded&&!f->mask.rgba.empty()){
   if(f->mask.width!=f->mask_w||f->mask.height!=f->mask_h){gs_texture_destroy(f->mask_tex);const uint8_t*p=f->mask.rgba.data();
    f->mask_tex=gs_texture_create(f->mask.width,f->mask.height,GS_RGBA,1,&p,GS_DYNAMIC);f->mask_w=f->mask.width;f->mask_h=f->mask.height;
   }else gs_texture_set_image(f->mask_tex,f->mask.rgba.data(),f->mask.width*4,false);
   f->uploaded=f->sequence;
  }
 }
 uint64_t now=os_gettime_ns();if(!points.timestamp_ns||now-points.timestamp_ns>500000000ULL){points.valid=false;points.presence=0;}
 auto u=compute_uniforms(points,params,w,h);
 int hw=std::max(1,int(w)/2),hh=std::max(1,int(h)/2),qw=std::max(1,int(w)/4),qh=std::max(1,int(h)/4);
 blur(f,src,f->half_a,hw,hh,u.blur_scale*1.15f/hw,0);
 blur(f,gs_texrender_get_texture(f->half_a),f->half_b,hw,hh,0,u.blur_scale*1.15f/hh);
 blur(f,gs_texrender_get_texture(f->half_b),f->quarter_a,qw,qh,u.blur_scale*2.6f/qw,0);
 blur(f,gs_texrender_get_texture(f->quarter_a),f->quarter_b,qw,qh,0,u.blur_scale*2.6f/qh);
 bind_uniforms(f,u,params);set_texture(f->effect,"uSrc",src);set_texture(f->effect,"uFine",gs_texrender_get_texture(f->half_b));
 set_texture(f->effect,"uWide",gs_texrender_get_texture(f->quarter_b));set_texture(f->effect,"uMask",f->mask_tex);
 // Restore the caller's blending before drawing the final, alpha-preserving image.
 gs_blend_state_pop();
 while(gs_effect_loop(f->effect,params.preview==3?"Mask":"Composite"))gs_draw_sprite(src,0,w,h);
 gs_enable_framebuffer_srgb(old_srgb);gs_set_linear_srgb(old_linear);
}
const char* name(void*){return "VEIL 필터 · 얼굴·피부·메이크업";}
obs_source_frame*observe_frame(void*data,obs_source_frame*frame){return static_cast<Filter*>(data)->trace.observe(frame);}
void tick(void*data,float){auto*f=static_cast<Filter*>(data);f->trace.ticks++;f->trace.beat("beauty",f->source);}
void load(void*data,obs_data_t*){veil::background_prepare(static_cast<Filter*>(data)->source);}
}

bool obs_module_load(void)
{
 obs_source_info info{};info.id="trueitem_beauty_filter";info.type=OBS_SOURCE_TYPE_FILTER;
 info.output_flags=OBS_SOURCE_VIDEO;info.get_name=name;info.create=create;info.destroy=destroy;
 info.update=update;info.get_defaults=defaults;info.get_properties=properties;info.video_render=render;
 info.filter_video=observe_frame;info.video_tick=tick;info.load=load;
 obs_register_source(&info);veil::load_background_engines();blog(LOG_INFO,"[VEIL] Native beauty filter " VEIL_VERSION " registered");return true;
}
