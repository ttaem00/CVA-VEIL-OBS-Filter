// Independent libobs/D3D11 test process; never reads/writes OBS user profiles.
// Only public NASA/scikit-image astronaut fixture and generated test patterns.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/base.h>
#include <util/bmem.h>
#include <util/platform.h>
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
namespace {
constexpr const char *filter_id="trueitem_beauty_filter";
constexpr const char *control_keys[]={"smooth","texture","even","radius","sharpen","slim","chin","eyes","nose","lips","eyeBright","underEye","lipAmount","blushAmount","bright","warmth","vibrance","contrast","glow","vignette","grain"};
constexpr const char *extra_keys[]={"skinBright","redness","teethWhite","lipGloss","faceLength","eyeSpacing","noseLength"};
struct Fixture {gs_texture_t *texture=nullptr;std::uint32_t width=320,height=320;};
std::vector<std::uint8_t> input_pixels;
std::uint32_t input_width=320,input_height=320;
Fixture *fixture_instance=nullptr;
bool async_fixture=false;
bool async_feed_paused=false;uint64_t async_timestamp_offset=0;
std::vector<std::uint8_t>async_pixels;

void log_handler(int level,const char *format,va_list args,void*) {
    char text[8192];std::vsnprintf(text,sizeof(text),format,args);
    std::fprintf(stderr,"[libobs:%d] %s\n",level,text);std::fflush(stderr);
}
const char *fixture_name(void*) {return "Public fixture (GPU test only)";}
void *fixture_create(obs_data_t*,obs_source_t*) {
    auto *f=new Fixture;f->width=input_width;f->height=input_height;
    if(async_fixture){async_pixels=input_pixels;fixture_instance=f;return f;}
    obs_enter_graphics();const std::uint8_t *data=input_pixels.data();
    f->texture=gs_texture_create(f->width,f->height,GS_BGRA,1,&data,GS_DYNAMIC);
    obs_leave_graphics();if(!f->texture){delete f;return nullptr;}fixture_instance=f;return f;
}
void fixture_destroy(void *data) {auto *f=static_cast<Fixture*>(data);obs_enter_graphics();gs_texture_destroy(f->texture);obs_leave_graphics();fixture_instance=nullptr;delete f;}
std::uint32_t fixture_width(void *data){return static_cast<Fixture*>(data)->width;}
std::uint32_t fixture_height(void *data){return static_cast<Fixture*>(data)->height;}
void fixture_render(void *data,gs_effect_t*) {
    auto *f=static_cast<Fixture*>(data);
    // OBS inputs may be drawn inside a filter-owned effect (e.g. Lite).
    if(auto*active=gs_get_effect()){
        gs_effect_set_texture(gs_effect_get_param_by_name(active,"image"),f->texture);
        gs_draw_sprite(f->texture,0,f->width,f->height);return;
    }
    auto *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
    bool srgb=gs_framebuffer_srgb_enabled();gs_enable_framebuffer_srgb(false);
    gs_blend_state_push();gs_enable_blending(false);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),f->texture);
    while(gs_effect_loop(effect,"Draw"))gs_draw_sprite(f->texture,0,f->width,f->height);
    gs_blend_state_pop();gs_enable_framebuffer_srgb(srgb);
}
void replace_fixture(const std::vector<std::uint8_t> &pixels) {
    if(!fixture_instance)throw std::runtime_error("Fixture source unavailable");
    if(async_fixture){async_pixels=pixels;return;}
    obs_enter_graphics();gs_texture_set_image(fixture_instance->texture,pixels.data(),input_width*4,false);obs_leave_graphics();
}
std::string json(const std::string &s){std::string out="\"";for(unsigned char c:s){if(c=='\\'||c=='\"'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c<32)out+=' ';else out+=char(c);}return out+'"';}
void check_hr(HRESULT hr,const char *what){if(FAILED(hr))throw std::runtime_error(std::string(what)+" failed HRESULT "+std::to_string(static_cast<unsigned long>(hr)));}
void save_png(const fs::path &path,const std::vector<std::uint8_t> &pixels,std::uint32_t w,std::uint32_t h){
    IWICImagingFactory *factory=nullptr;IWICStream *stream=nullptr;IWICBitmapEncoder *encoder=nullptr;IWICBitmapFrameEncode *frame=nullptr;IPropertyBag2 *options=nullptr;
    try{
        check_hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
        check_hr(factory->CreateStream(&stream),"WIC stream");check_hr(stream->InitializeFromFilename(path.wstring().c_str(),GENERIC_WRITE),"WIC output file");
        check_hr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"WIC encoder");check_hr(encoder->Initialize(stream,WICBitmapEncoderNoCache),"PNG init");
        check_hr(encoder->CreateNewFrame(&frame,&options),"PNG frame");check_hr(frame->Initialize(options),"PNG frame init");check_hr(frame->SetSize(w,h),"PNG size");
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;check_hr(frame->SetPixelFormat(&format),"PNG pixel format");
        // Stored readback is premultiplied BGRA. PNG expects straight channels.
        auto straight=pixels;for(std::size_t i=0;i<straight.size();i+=4){unsigned a=straight[i+3];for(int c=0;c<3;++c)straight[i+c]=a?std::uint8_t(std::min(255u,(unsigned(straight[i+c])*255+a/2)/a)):0;}
        check_hr(frame->WritePixels(h,w*4,static_cast<UINT>(straight.size()),straight.data()),"PNG pixels");check_hr(frame->Commit(),"PNG frame commit");check_hr(encoder->Commit(),"PNG commit");
    }catch(...){if(options)options->Release();if(frame)frame->Release();if(encoder)encoder->Release();if(stream)stream->Release();if(factory)factory->Release();throw;}
    if(options)options->Release();frame->Release();encoder->Release();stream->Release();factory->Release();
}
std::vector<std::uint8_t> readback(obs_source_t *source,gs_texrender_t *target,gs_stagesurf_t *stage){
    if(async_fixture&&!async_feed_paused){obs_source_frame frame{};frame.data[0]=async_pixels.data();frame.linesize[0]=input_width*4;frame.width=input_width;frame.height=input_height;frame.format=VIDEO_FORMAT_BGRA;frame.full_range=true;frame.timestamp=os_gettime_ns()-async_timestamp_offset;obs_source_output_video(source,&frame);}
    std::vector<std::uint8_t> result(std::size_t(input_width)*input_height*4);
    obs_enter_graphics();gs_texrender_reset(target);
    if(!gs_texrender_begin(target,input_width,input_height)){obs_leave_graphics();throw std::runtime_error("capture texrender begin failed");}
    vec4 clear{};gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,float(input_width),0,float(input_height),-100,100);
    gs_blend_state_push();gs_enable_blending(false);bool srgb=gs_framebuffer_srgb_enabled();gs_enable_framebuffer_srgb(false);
    obs_source_video_render(source);
    gs_enable_framebuffer_srgb(srgb);gs_blend_state_pop();gs_texrender_end(target);
    gs_stage_texture(stage,gs_texrender_get_texture(target));std::uint8_t *mapped=nullptr;std::uint32_t stride=0;
    if(!gs_stagesurface_map(stage,&mapped,&stride)){obs_leave_graphics();throw std::runtime_error("capture stagesurface map failed");}
    for(std::uint32_t y=0;y<input_height;++y)std::memcpy(result.data()+std::size_t(y)*input_width*4,mapped+std::size_t(y)*stride,input_width*4);
    gs_stagesurface_unmap(stage);obs_leave_graphics();return result;
}
std::string status(obs_source_t *filter){auto *props=obs_source_properties(filter);std::string text;auto *property=props?obs_properties_get(props,"tracking_status"):nullptr;if(property)text=obs_property_description(property);if(props)obs_properties_destroy(props);return text;}
obs_data_t *neutral(){auto *data=obs_data_create();for(const char *key:control_keys)obs_data_set_int(data,key,0);for(const char *key:extra_keys)obs_data_set_int(data,key,0);obs_data_set_int(data,"strength",100);obs_data_set_int(data,"look",0);obs_data_set_int(data,"lookAmount",40);obs_data_set_int(data,"preview",0);obs_data_set_int(data,"split",50);obs_data_set_int(data,"texture",100);obs_data_set_int(data,"radius",50);obs_data_set_int(data,"lipColor",0xff5f48c4);obs_data_set_int(data,"blushColor",0xff6b8aff);obs_data_set_bool(data,"mirror",false);return data;}
obs_data_t *user_settings(const fs::path &file){
    auto *preset=obs_data_create_from_json_file(file.u8string().c_str());if(!preset)throw std::runtime_error("Cannot read saved public test settings");
    auto *values=obs_data_get_obj(preset,"values"),*colors=obs_data_get_obj(preset,"colors");auto *result=neutral();
    for(const char *key:control_keys)obs_data_set_int(result,key,obs_data_get_int(values,key));
    auto color=[&](const char *key){const char *text=obs_data_get_string(colors,key);unsigned rgb=0;if(std::sscanf(text,"#%x",&rgb)!=1)throw std::runtime_error("Preset color invalid");return 0xff000000u|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);};
    obs_data_set_int(result,"lipColor",color("lip"));obs_data_set_int(result,"blushColor",color("blush"));obs_data_set_bool(result,"mirror",obs_data_get_bool(preset,"mirror"));
    obs_data_release(colors);obs_data_release(values);obs_data_release(preset);return result;
}
struct Metrics{double rgb_mae=0,alpha_mae=0;int minimum=255,maximum=0,alpha_min=255,alpha_max=0;std::size_t changed=0;};
Metrics metrics(const std::vector<std::uint8_t> &pixels,const std::vector<std::uint8_t> &baseline){Metrics m;double rgb=0,alpha=0;for(std::size_t i=0;i<pixels.size();i+=4){bool changed=false;for(int c=0;c<3;++c){int d=std::abs(int(pixels[i+c])-int(baseline[i+c]));rgb+=d;changed|=d!=0;m.minimum=std::min(m.minimum,int(pixels[i+c]));m.maximum=std::max(m.maximum,int(pixels[i+c]));}alpha+=std::abs(int(pixels[i+3])-int(baseline[i+3]));m.alpha_min=std::min(m.alpha_min,int(pixels[i+3]));m.alpha_max=std::max(m.alpha_max,int(pixels[i+3]));m.changed+=changed;}m.rgb_mae=rgb/(pixels.size()/4*3);m.alpha_mae=alpha/(pixels.size()/4);return m;}
void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
bool same_settings(obs_data_t*a,obs_data_t*b){
    for(const char*k:control_keys)if(obs_data_get_int(a,k)!=obs_data_get_int(b,k))return false;
    for(const char*k:extra_keys)if(obs_data_get_int(a,k)!=obs_data_get_int(b,k))return false;
    for(const char*k:{"lipColor","blushColor","strength","look","lookAmount"})if(obs_data_get_int(a,k)!=obs_data_get_int(b,k))return false;
    return obs_data_get_bool(a,"mirror")==obs_data_get_bool(b,"mirror");
}
void preset_tests(obs_source_t*filter,const fs::path&preset,const fs::path&out){
    auto*d=user_settings(preset);obs_data_set_int(d,"faceLength",-26);obs_data_set_int(d,"eyeSpacing",31);
    obs_data_set_int(d,"teethWhite",40);obs_data_set_int(d,"look",2);obs_data_set_int(d,"strength",73);
    obs_data_set_bool(d,"natural",false);obs_data_set_int(d,"background_mode",3);obs_data_set_int(d,"background_similarity",321);obs_data_set_double(d,"background_feather",7.25);
    obs_data_set_string(d,"preset_name","VEIL 한글 테스트");obs_data_set_string(d,"preset_save",(out/"roundtrip.json").u8string().c_str());
    obs_source_update(filter,d);auto*props=obs_source_properties(filter);
    for(const char*k:control_keys)require(obs_properties_get(props,k)!=nullptr,"Missing original browser slider");
    for(const char*k:extra_keys)require(obs_properties_get(props,k)!=nullptr,"Missing VEIL slider");
    for(const char*k:{"brand","quick_help","shape_help","file_help","diagnostic_help"})require(!obs_properties_get(props,k),"Persistent explanatory paragraphs must not clutter the controls");
    require(!obs_property_visible(obs_properties_get(props,"preset_result"))&&!obs_property_visible(obs_properties_get(props,"preview_notice"))&&!obs_property_visible(obs_properties_get(props,"preview_reset")),"Context notices and actions must start hidden in processed view");
    require(std::string(obs_property_long_description(obs_properties_get(props,"strength"))).size()>0,"Strength retains optional help");
    auto click=[&](const char*k){auto*p=obs_properties_get(props,k);require(p!=nullptr,"Missing preset action");obs_property_button_clicked(p,filter);};
    click("preset_export");require(fs::exists(out/"roundtrip.json"),"VEIL export failed");
    auto*empty=neutral();obs_source_update(filter,empty);obs_data_release(empty);
    auto*path=obs_data_create();obs_data_set_string(path,"preset_load",(out/"roundtrip.json").u8string().c_str());obs_source_update(filter,path);obs_data_release(path);
    click("preset_import");auto*loaded=obs_source_get_settings(filter);require(same_settings(d,loaded),"VEIL roundtrip changed control values");require(!obs_data_get_bool(loaded,"natural")&&obs_data_get_int(loaded,"background_mode")==1&&obs_data_get_int(loaded,"background_similarity")==321&&obs_data_get_double(loaded,"background_feather")==7.25,"v5 fractional feather preset roundtrip failed");obs_data_release(loaded);obs_data_release(d);
    auto*v4=obs_data_create_from_json_file((out/"roundtrip.json").u8string().c_str());obs_data_set_int(v4,"version",4);auto*legacy_bg=obs_data_get_obj(v4,"background");obs_data_set_int(legacy_bg,"feather",1);obs_data_release(legacy_bg);obs_data_save_json(v4,(out/"v4.json").u8string().c_str());obs_data_release(v4);
    path=obs_data_create();obs_data_set_string(path,"preset_load",(out/"v4.json").u8string().c_str());obs_source_update(filter,path);obs_data_release(path);click("preset_import");loaded=obs_source_get_settings(filter);require(obs_data_get_int(loaded,"background_mode")==1&&obs_data_get_double(loaded,"background_feather")==1,"v4 preset compatibility");obs_data_release(loaded);
    auto*v3=obs_data_create_from_json_file((out/"v4.json").u8string().c_str());obs_data_set_int(v3,"version",3);auto*bg=obs_data_get_obj(v3,"background");obs_data_set_int(bg,"mode",1);obs_data_release(bg);obs_data_save_json(v3,(out/"v3.json").u8string().c_str());obs_data_release(v3);
    path=obs_data_create();obs_data_set_string(path,"preset_load",(out/"v3.json").u8string().c_str());obs_source_update(filter,path);obs_data_release(path);click("preset_import");loaded=obs_source_get_settings(filter);require(obs_data_get_int(loaded,"background_mode")==1&&!obs_data_get_bool(loaded,"natural"),"v3 preset import compatibility");obs_data_release(loaded);
    auto*v2=obs_data_create_from_json_file((out/"roundtrip.json").u8string().c_str());obs_data_set_int(v2,"version",2);obs_data_erase(v2,"natural");obs_data_erase(v2,"background");obs_data_save_json(v2,(out/"v2.json").u8string().c_str());obs_data_release(v2);
    path=obs_data_create();obs_data_set_string(path,"preset_load",(out/"v2.json").u8string().c_str());obs_source_update(filter,path);obs_data_release(path);click("preset_import");loaded=obs_source_get_settings(filter);require(obs_data_get_int(loaded,"faceLength")==-26&&obs_data_get_bool(loaded,"natural"),"v2 import compatibility failed");obs_data_release(loaded);
    path=obs_data_create();obs_data_set_string(path,"preset_load",preset.u8string().c_str());obs_source_update(filter,path);obs_data_release(path);click("preset_import");
    auto*expected=user_settings(preset);loaded=obs_source_get_settings(filter);require(same_settings(expected,loaded),"Legacy import changed values/colors/mirror or retained new effects");
    obs_data_release(loaded);
    auto*invalid=obs_data_create_from_json_file((out/"roundtrip.json").u8string().c_str());auto*ext=obs_data_get_obj(invalid,"enhancements");obs_data_set_int(ext,"faceLength",999);obs_data_release(ext);
    obs_data_save_json(invalid,(out/"invalid.json").u8string().c_str());obs_data_release(invalid);
    path=obs_data_create();obs_data_set_string(path,"preset_load",(out/"invalid.json").u8string().c_str());obs_source_update(filter,path);obs_data_release(path);click("preset_import");
    loaded=obs_source_get_settings(filter);require(same_settings(expected,loaded),"Invalid preset partially mutated settings");obs_data_release(loaded);
    auto visible=[&](const char *key){return obs_property_visible(obs_properties_get(props,key));};
    loaded=obs_source_get_settings(filter);
    obs_data_set_int(loaded,"section",0);
    obs_data_set_int(loaded,"preview",2);
    obs_property_modified(obs_properties_get(props,"preview"),loaded);
    obs_source_update(filter,loaded);
    require(visible("quick")&&visible("split")&&visible("preview_notice")&&visible("preview_reset"),
            "Split output must show its position, warning and return action");

    obs_data_set_int(loaded,"section",2);
    obs_property_modified(obs_properties_get(props,"section"),loaded);
    require(visible("shape")&&!visible("skin")&&!visible("quick_presets")&&!visible("quick_options")&&!visible("tools"),
            "Face details must show only their own settings");
    require(!visible("split")&&visible("preview_notice")&&visible("preview_reset"),
            "Changing section must keep the warning and return action while comparison is on air");
    click("preview_reset");
    require(obs_data_get_int(loaded,"preview")==0&&obs_data_get_int(loaded,"section")==2&&same_settings(expected,loaded),
            "Returning to processed output must preserve the selected section and saved look");
    require(visible("shape")&&!visible("split")&&!visible("preview_notice")&&!visible("preview_reset"),
            "Returning to processed output must hide comparison-only controls");

    obs_data_set_int(loaded,"section",0);
    obs_data_set_int(loaded,"preview",3);
    obs_property_modified(obs_properties_get(props,"preview"),loaded);
    obs_data_set_int(loaded,"section",5);
    obs_property_modified(obs_properties_get(props,"section"),loaded);
    require(visible("files")&&visible("preview_notice"),"Preset files must retain the active output warning");
    obs_data_set_string(loaded,"preset_load",preset.u8string().c_str());
    obs_source_update(filter,loaded);
    click("preset_import");
    require(obs_data_get_int(loaded,"preview")==0&&visible("preset_result")&&!visible("preview_notice")&&!visible("preview_reset"),
            "Preset import must clear the output warning when it restores processed output");
    obs_data_release(loaded);
    // OBS invokes every modified callback when reopening properties. It must not
    // overwrite imported colours with a stale palette selection.
    loaded=obs_source_get_settings(filter);obs_properties_apply_settings(props,loaded);require(same_settings(expected,loaded),"Opening properties changed the saved look");obs_data_release(loaded);
    loaded=obs_source_get_settings(filter);obs_data_set_int(loaded,"lip_palette",1);obs_property_modified(obs_properties_get(props,"lip_palette"),loaded);auto palette_colour=obs_data_get_int(loaded,"lipColor");obs_data_set_int(loaded,"lipColor",0xff123456);obs_property_modified(obs_properties_get(props,"lipColor"),loaded);require(obs_data_get_int(loaded,"lip_palette")==-1,"Custom colour did not deselect palette");obs_properties_apply_settings(props,loaded);require(obs_data_get_int(loaded,"lipColor")==0xff123456,"Reopening properties overwrote custom colour");obs_data_set_int(loaded,"lip_palette",1);obs_property_modified(obs_properties_get(props,"lip_palette"),loaded);require(obs_data_get_int(loaded,"lipColor")==palette_colour,"Cannot reselect the same palette after a custom colour");obs_data_release(loaded);
    for(int i=0;i<7;++i){path=obs_data_create();obs_data_set_int(path,"builtin",i);obs_source_update(filter,path);obs_data_release(path);click("preset_builtin");loaded=obs_source_get_settings(filter);require(std::string(obs_data_get_string(loaded,"preset_name")).size()>0,"Builtin preset name missing");require(obs_data_get_int(loaded,"faceLength")==0&&obs_data_get_int(loaded,"look")==0,"Builtin retained extra effects");if(i==0)require(obs_data_get_int(loaded,"texture")==100&&obs_data_get_int(loaded,"smooth")==0,"Off preset is not neutral");obs_data_release(loaded);}
    click("preset_restore");loaded=obs_source_get_settings(filter);require(same_settings(expected,loaded),"Personal preset restore failed");obs_data_release(loaded);obs_data_release(expected);
    obs_properties_destroy(props);std::cout<<"PASS VEIL v5 fractional/background roundtrip, v4/v3/v2/v1 compatibility, invalid-preset atomicity, 28 sliders, navigation and exact saved values\n";
}
} // namespace

int main(int argc,char **argv){
    if(argc!=7&&argc!=9){std::cerr<<"Usage: gpu_smoke.exe OBS_ROOT MODULE_DLL MODULE_DATA FIXTURE_320_BGRA PRESET_JSON OUTPUT_DIR [WIDTH HEIGHT]\n";return 2;}
    fs::path obs_root=fs::u8path(argv[1]),module_path=fs::u8path(argv[2]),data_path=fs::u8path(argv[3]),fixture_file=fs::u8path(argv[4]),preset_file=fs::u8path(argv[5]),output=fs::u8path(argv[6]);
    AddDllDirectory((obs_root/L"bin/64bit").c_str());
    bool started=false,graphics_ready=false,showing=false;obs_source_t *source=nullptr,*filter=nullptr;gs_texrender_t *target=nullptr;gs_stagesurf_t *stage=nullptr;int outcome=1;
    base_set_log_handler(log_handler,nullptr);std::ofstream report;bool first=true;
    try{
        fs::create_directories(output);input_pixels.resize(std::size_t(input_width)*input_height*4);std::ifstream input(fixture_file,std::ios::binary);input.read(reinterpret_cast<char*>(input_pixels.data()),static_cast<std::streamsize>(input_pixels.size()));if(input.gcount()!=static_cast<std::streamsize>(input_pixels.size()))throw std::runtime_error("Public BGRA fixture must be 320x320x4 bytes");
        if(argc==9){
            unsigned w=std::stoul(argv[7]),h=std::stoul(argv[8]);require(w>=320&&h>=180&&w<=1920&&h<=1080,"Test dimensions out of bounds");
            auto original=input_pixels;input_width=w;input_height=h;input_pixels.assign(std::size_t(w)*h*4,0);
            unsigned size=std::min(w,h),left=(w-size)/2,top=(h-size)/2;
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){std::size_t i=(std::size_t(y)*w+x)*4;input_pixels[i+3]=255;
                if(x>=left&&x<left+size&&y>=top&&y<top+size){unsigned ox=(x-left)*320/size,oy=(y-top)*320/size;std::memcpy(input_pixels.data()+i,original.data()+(std::size_t(oy)*320+ox)*4,4);}}
        }
        std::string config=(output/"isolated-module-config").u8string();if(!obs_startup("en-US",config.c_str(),nullptr))throw std::runtime_error("obs_startup failed");started=true;
        // OBS check_path concatenates directly: registered data paths need '/'.
        std::string lib_data=(obs_root/"data/libobs").generic_u8string()+"/";obs_add_data_path(lib_data.c_str());
        std::string graphics=(obs_root/"bin/64bit/libobs-d3d11.dll").u8string();obs_video_info video{};video.graphics_module=graphics.c_str();video.fps_num=30;video.fps_den=1;video.base_width=input_width;video.base_height=input_height;video.output_width=input_width;video.output_height=input_height;video.output_format=VIDEO_FORMAT_BGRA;video.adapter=0;video.gpu_conversion=false;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;
        int reset=obs_reset_video(&video);if(reset!=OBS_VIDEO_SUCCESS)throw std::runtime_error("obs_reset_video failed "+std::to_string(reset));graphics_ready=true;
        obs_enter_graphics();char *effect_error=nullptr;auto *test_effect=gs_effect_create_from_file((data_path/"beauty.effect").u8string().c_str(),&effect_error);
        std::string compile_error=effect_error?effect_error:"";bfree(effect_error);if(test_effect){for(const char *name:{"Blur","Composite","Mask"})if(!gs_effect_get_technique(test_effect,name))compile_error+=" Missing technique "+std::string(name);gs_effect_destroy(test_effect);}obs_leave_graphics();
        if(!test_effect||!compile_error.empty())throw std::runtime_error("OBS effect-container compile: "+compile_error);
        std::cout<<"PASS OBS effect-container/D3D11 compile\n";
        obs_module_t *module=nullptr;int opened=obs_open_module(&module,module_path.u8string().c_str(),data_path.u8string().c_str());if(opened!=MODULE_SUCCESS||!module||!obs_init_module(module))throw std::runtime_error("Native module load/init failed "+std::to_string(opened));
        obs_source_info info{};info.id="trueitem_gpu_fixture";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;info.get_name=fixture_name;info.create=fixture_create;info.destroy=fixture_destroy;info.get_width=fixture_width;info.get_height=fixture_height;info.video_render=fixture_render;obs_register_source(&info);
        source=obs_source_create_private(info.id,"Public fixture",nullptr);if(!source)throw std::runtime_error("Fixture source creation failed");obs_source_inc_showing(source);showing=true;
        obs_enter_graphics();target=gs_texrender_create(GS_BGRA,GS_ZS_NONE);stage=gs_stagesurface_create(input_width,input_height,GS_BGRA);obs_leave_graphics();if(!target||!stage)throw std::runtime_error("Capture resources failed");
        auto baseline=readback(source,target,stage);save_png(output/"input.png",baseline,input_width,input_height);
        auto *settings=neutral();filter=obs_source_create_private(filter_id,"TRUE ITEM GPU test",settings);obs_data_release(settings);if(!filter)throw std::runtime_error("Filter creation failed (check effect/model log)");obs_source_filter_add(source,filter);
        preset_tests(filter,preset_file,output);
        // Background-bearing presets now apply to their real processing filters.
        // Face GPU comparisons below intentionally isolate beauty processing.
        obs_source_enum_filters(source,[](obs_source_t*,obs_source_t*child,void*){if(std::string(obs_source_get_id(child))!=filter_id)obs_source_set_enabled(child,false);},nullptr);
        report.open(output/"gpu-results.json");report<<"{\n  \"scope\": \"Independent libobs process, public fixture only, no OBS UI/settings\",\n  \"width\":"<<input_width<<",\n  \"height\":"<<input_height<<",\n  \"effect_compile\": true,\n  \"cases\": [\n";bool neutral_ok=false,shape_ok=false,alpha_ok=false,face_seen=false;
        auto run=[&](const char *name,obs_data_t *changes,const std::vector<std::uint8_t> &reference,int frames){
            obs_source_update(filter,changes);obs_data_release(changes);std::vector<std::uint8_t> pixels;auto begin=std::chrono::steady_clock::now();
            std::vector<double> capture_ms;for(int i=0;i<frames;++i){auto t=std::chrono::steady_clock::now();pixels=readback(source,target,stage);capture_ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());std::this_thread::sleep_until(t+std::chrono::milliseconds(34));}
            std::sort(capture_ms.begin(),capture_ms.end());double p95=capture_ms[std::min(capture_ms.size()-1,capture_ms.size()*95/100)];
            double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();auto m=metrics(pixels,reference);std::string text=status(filter);face_seen|=text.find("얼굴 인식됨")!=std::string::npos;
            std::ofstream raw(output/(std::string(name)+".bgra"),std::ios::binary);raw.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));raw.close();save_png(output/(std::string(name)+".png"),pixels,input_width,input_height);
            if(!first)report<<",\n";first=false;report<<"    {\"name\":"<<json(name)<<",\"frames\":"<<frames<<",\"elapsed_seconds\":"<<elapsed<<",\"capture_pipeline_p95_ms\":"<<p95<<",\"status\":"<<json(text)<<",\"rgb_mae\":"<<m.rgb_mae<<",\"alpha_mae\":"<<m.alpha_mae<<",\"changed_pixels\":"<<m.changed<<",\"rgb_min\":"<<m.minimum<<",\"rgb_max\":"<<m.maximum<<",\"alpha_min\":"<<m.alpha_min<<",\"alpha_max\":"<<m.alpha_max<<"}";report.flush();
            std::cout<<name<<": "<<text<<" RGB MAE="<<m.rgb_mae<<" alpha MAE="<<m.alpha_mae<<" changed="<<m.changed<<" elapsed="<<elapsed<<"s\n";std::cout.flush();return m;
        };
        neutral_ok=run("neutral",neutral(),baseline,80).rgb_mae<=1.0;
        run("user-preset",user_settings(preset_file),baseline,45);
        auto*zero=user_settings(preset_file);obs_data_set_bool(zero,"mirror",false);obs_data_set_int(zero,"strength",0);require(run("strength-zero",zero,baseline,3).rgb_mae<=1,"Zero strength is not identity");
        auto*before=user_settings(preset_file);obs_data_set_bool(before,"mirror",false);obs_data_set_int(before,"preview",1);require(run("original-preview",before,baseline,3).rgb_mae<=1,"Original preview is not identity");
        auto mirrored=baseline;for(unsigned y=0;y<input_height;++y)for(unsigned x=0;x<input_width;++x)std::memcpy(mirrored.data()+(std::size_t(y)*input_width+x)*4,baseline.data()+(std::size_t(y)*input_width+(input_width-1-x))*4,4);
        auto*mirror=neutral();obs_data_set_bool(mirror,"mirror",true);require(run("mirror",mirror,mirrored,3).rgb_mae<=1,"Mirror output is not the horizontal reflection");
        auto*split=neutral();obs_data_set_int(split,"bright",60);obs_data_set_int(split,"preview",2);run("split-view",split,baseline,3);
        std::vector<std::uint8_t> split_pixels(baseline.size());std::ifstream split_raw(output/"split-view.bgra",std::ios::binary);split_raw.read(reinterpret_cast<char*>(split_pixels.data()),static_cast<std::streamsize>(split_pixels.size()));
        double left_diff=0,right_diff=0;for(unsigned y=0;y<input_height;++y)for(unsigned x=0;x<input_width;++x){auto i=(std::size_t(y)*input_width+x)*4;for(int c=0;c<3;++c){double d=std::abs(int(split_pixels[i+c])-int(baseline[i+c]));if(x<input_width*48/100)left_diff+=d;else if(x>input_width*52/100)right_diff+=d;}}
        require(left_diff==0&&right_diff>input_width*input_height,"Split preview did not keep the left original and right processed");
        auto*mask=neutral();obs_data_set_int(mask,"preview",3);require(run("mask-preview",mask,baseline,3).rgb_mae>.1,"Mask preview is not visible");
        for(const char*k:extra_keys){auto*extra=neutral();obs_data_set_int(extra,k,80);require(run(k,extra,baseline,8).rgb_mae>0.0001,"New control has no GPU effect");}
        for(int look=1;look<=5;++look){auto*d=neutral();obs_data_set_int(d,"look",look);obs_data_set_int(d,"lookAmount",100);require(run(("colour-look-"+std::to_string(look)).c_str(),d,baseline,2).rgb_mae>0.1,"Colour look has no effect");}
        auto *shape=neutral();obs_data_set_int(shape,"slim",65);obs_data_set_int(shape,"chin",10);obs_data_set_int(shape,"eyes",35);obs_data_set_int(shape,"nose",40);obs_data_set_int(shape,"lips",10);shape_ok=run("shape-on",shape,baseline,60).rgb_mae>.05;
        auto*half_reference=neutral();obs_data_set_int(half_reference,"slim",40);obs_data_set_int(half_reference,"faceLength",30);obs_data_set_int(half_reference,"eyeSpacing",30);obs_data_set_int(half_reference,"noseLength",30);run("half-shape-reference",half_reference,baseline,8);
        std::vector<std::uint8_t> half_pixels(baseline.size());std::ifstream half_raw(output/"half-shape-reference.bgra",std::ios::binary);half_raw.read(reinterpret_cast<char*>(half_pixels.data()),static_cast<std::streamsize>(half_pixels.size()));
        auto*half_strength=neutral();obs_data_set_int(half_strength,"slim",80);obs_data_set_int(half_strength,"faceLength",60);obs_data_set_int(half_strength,"eyeSpacing",60);obs_data_set_int(half_strength,"noseLength",60);obs_data_set_int(half_strength,"strength",50);require(run("half-strength-no-ghost",half_strength,half_pixels,8).rgb_mae<=.1,"Partial strength cross-fades differently shaped faces");
        auto*stress=neutral();for(const char*k:control_keys)obs_data_set_int(stress,k,100);for(const char*k:extra_keys)obs_data_set_int(stress,k,100);run("all-controls-max",stress,baseline,15);
        auto blank=input_pixels;for(std::size_t i=0;i<blank.size();i+=4){blank[i]=110;blank[i+1]=65;blank[i+2]=30;blank[i+3]=255;}replace_fixture(blank);auto *no_face=neutral();obs_data_set_int(no_face,"slim",65);obs_data_set_int(no_face,"eyes",35);require(run("no-face",no_face,blank,95).rgb_mae<=1,"No-face retained shape warp");
        auto transparent=input_pixels;for(std::uint32_t y=0;y<input_height;++y)for(std::uint32_t x=0;x<input_width;++x){std::size_t i=(std::size_t(y)*input_width+x)*4;unsigned a=(x<input_width/4||x>=input_width*3/4)?0:unsigned((x-input_width/4)*255/(input_width/2));for(int c=0;c<3;++c)transparent[i+c]=std::uint8_t((unsigned(transparent[i+c])*a+127)/255);transparent[i+3]=std::uint8_t(a);}replace_fixture(transparent);
        auto alpha=run("transparent-neutral",neutral(),transparent,75);alpha_ok=alpha.alpha_mae<=1.0&&alpha.alpha_min==0&&alpha.alpha_max>100;
        auto*active=user_settings(preset_file);obs_data_set_bool(active,"mirror",false);obs_data_set_int(active,"slim",0);
        require(run("transparent-active",active,transparent,8).alpha_mae<=1,"Colour/skin effects changed upstream alpha");
        replace_fixture(input_pixels);auto*persist=user_settings(preset_file);obs_source_update(filter,persist);obs_data_release(persist);
        auto*saved=obs_source_get_settings(filter);obs_data_save_json(saved,(output/"filter-settings.json").u8string().c_str());
        obs_source_filter_remove(source,filter);obs_source_release(filter);filter=nullptr;
        auto*reloaded=obs_data_create_from_json_file((output/"filter-settings.json").u8string().c_str());filter=obs_source_create_private(filter_id,"VEIL recreated filter",reloaded);require(filter!=nullptr,"Filter recreation failed");obs_source_filter_add(source,filter);
        auto*actual=obs_source_get_settings(filter);require(same_settings(saved,actual),"Filter settings changed after save/load and recreation");obs_data_release(actual);obs_data_release(saved);
        run("recreated-filter",reloaded,baseline,80);require(status(filter).find("얼굴 인식됨")!=std::string::npos,"Recreated tracker did not reacquire the face");
        report<<"\n  ],\n  \"neutral_ok\":"<<(neutral_ok?"true":"false")<<",\n  \"shape_changed\":"<<(shape_ok?"true":"false")<<",\n  \"face_seen\":"<<(face_seen?"true":"false")<<",\n  \"alpha_preserved\":"<<(alpha_ok?"true":"false")<<"\n}\n";
        outcome=neutral_ok&&shape_ok&&face_seen&&alpha_ok?0:1;std::cout<<(outcome?"FAIL":"PASS")<<" integration checks; see gpu-results.json and PNG/BGRA files\n";
    }catch(const std::exception &error){std::cerr<<"GPU SMOKE ERROR: "<<error.what()<<"\n";if(report.is_open())report<<"\n  ],\n  \"error\":"<<json(error.what())<<",\n  \"passed\":false\n}\n";}
    if(filter){if(source)obs_source_filter_remove(source,filter);obs_source_release(filter);}if(source){if(showing)obs_source_dec_showing(source);obs_source_release(source);}if(started){if(graphics_ready){obs_enter_graphics();gs_stagesurface_destroy(stage);gs_texrender_destroy(target);obs_leave_graphics();}obs_shutdown();}CoUninitialize();return outcome;
}
