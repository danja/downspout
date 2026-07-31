#include "lightverb_serialization.hpp"
#include <cstdlib>
#include <string_view>
namespace downspout::lightverb {
std::string serializeParameters(const Parameters& raw){const Parameters p=clampParameters(raw);std::string out="version=2\n";for(std::uint32_t i=0;i<kParameterCount;++i){if(kParameterSpecs[i].output||i==kResetMidi)continue;out+=std::string(kParameterSpecs[i].symbol)+"="+std::to_string(p.values[i])+"\n";}return out;}
std::optional<Parameters> deserializeParameters(const std::string& text){Parameters p;std::size_t start=0;while(start<=text.size()){const auto nl=text.find('\n',start);const std::string_view line(text.data()+start,(nl==std::string::npos?text.size():nl)-start);start=nl==std::string::npos?text.size()+1:nl+1;if(line.empty())continue;const auto eq=line.find('=');if(eq==std::string_view::npos)return std::nullopt;const auto key=line.substr(0,eq);if(key=="version")continue;const std::string local(line.substr(eq+1));char*end=nullptr;const float value=std::strtof(local.c_str(),&end);if(!end||*end!='\0')return std::nullopt;bool found=false;for(std::uint32_t i=0;i<kParameterCount;++i){if(kParameterSpecs[i].output||i==kResetMidi)continue;if(key==kParameterSpecs[i].symbol){p.values[i]=value;found=true;break;}}if(!found)return std::nullopt;}return clampParameters(p);}
} // namespace downspout::lightverb
