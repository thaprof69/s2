#include "S2.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace {
std::string escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') out += "\\n"; else out += c;
  }
  return out;
}
std::string stringField(const std::string &json, const std::string &key, const std::string &fallback = "") {
  const std::string marker = "\"" + key + "\"";
  size_t p = json.find(marker); if (p == std::string::npos) return fallback;
  p = json.find(':', p + marker.size()); if (p == std::string::npos) return fallback;
  p = json.find('"', p + 1); if (p == std::string::npos) return fallback;
  std::string out; for (++p; p < json.size(); ++p) { if (json[p] == '"') break; if (json[p] == '\\' && p + 1 < json.size()) ++p; out += json[p]; }
  return out;
}
double numberField(const std::string &json, const std::string &key, double fallback) {
  const std::string marker = "\"" + key + "\""; size_t p = json.find(marker); if (p == std::string::npos) return fallback;
  p = json.find(':', p + marker.size()); if (p == std::string::npos) return fallback;
  const char *begin = json.c_str() + p + 1; char *end = 0; double value = std::strtod(begin, &end); return end == begin ? fallback : value;
}
bool boolField(const std::string &json, const std::string &key, bool fallback) {
  const std::string marker = "\"" + key + "\""; size_t p = json.find(marker); if (p == std::string::npos) return fallback;
  p = json.find(':', p + marker.size()); if (p == std::string::npos) return fallback;
  std::string tail = json.substr(p + 1, 8); return tail.find("true") != std::string::npos ? true : tail.find("false") != std::string::npos ? false : fallback;
}
std::string encode(int command, int channel, long value) { std::ostringstream o; o << ":w"; if (command + channel < 10) o << '0'; o << command + channel << value << "\\r\\n"; return o.str(); }
std::vector<std::string> translate(const std::string &op, const std::string &line) {
  int channel = static_cast<int>(numberField(line, "channel", 1)); int nativeChannel = std::max(0, std::min(1, channel - 1)); std::vector<std::string> commands;
  if (op == "configure") {
    double frequency = numberField(line, "frequency_hz", -1); double amplitude = numberField(line, "amplitude_vpp", -1); std::string waveform = stringField(line, "waveform");
    if (frequency > 0) { bool low = frequency < 600.0; commands.push_back(encode(63, nativeChannel, low ? 1 : 0)); commands.push_back(encode(23, nativeChannel, static_cast<long>(frequency * (low ? 100000.0 : 100.0)))); }
    if (amplitude >= 0) commands.push_back(encode(25, nativeChannel, static_cast<long>(amplitude * 100.0)));
    if (!waveform.empty()) { long value = waveform == "sine" ? 0 : waveform == "square" ? 1 : waveform == "sawtooth" ? 2 : -1; if (value < 0) throw std::runtime_error("unsupported waveform"); commands.push_back(encode(21, nativeChannel, value)); }
  } else if (op == "start") commands.push_back(encode(61, nativeChannel, 1));
  else if (op == "stop" || op == "emergency_stop" || op == "stop_all") { commands.push_back(encode(61, 0, 0)); commands.push_back(encode(61, 1, 0)); }
  return commands;
}
std::string array(const std::vector<std::string> &items) { std::ostringstream o; o << '['; for (size_t i=0;i<items.size();++i){if(i)o<<',';o<<'"'<<escape(items[i])<<'"';}o<<']';return o.str(); }
void respond(const std::string &id, bool ok, const std::string &result) { std::cout << "{\"v\":1,\"id\":\"" << escape(id) << "\",\"ok\":" << (ok?"true":"false") << ',' << result << "}" << std::endl; }
void rejectUnknownFields(const std::string &json) {
  static const std::set<std::string> allowed = {
    "v", "id", "op", "simulation", "inspection", "generator", "channel",
    "frequency_hz", "amplitude_vpp", "waveform"
  };
  for (size_t start = json.find('"'); start != std::string::npos; start = json.find('"', start + 1)) {
    size_t end = start + 1;
    while (end < json.size()) { if (json[end] == '\\') { end += 2; continue; } if (json[end] == '"') break; ++end; }
    if (end >= json.size()) break;
    size_t next = end + 1; while (next < json.size() && std::isspace(static_cast<unsigned char>(json[next]))) ++next;
    if (next < json.size() && json[next] == ':') {
      const std::string key = json.substr(start + 1, end - start - 1);
      if (allowed.find(key) == allowed.end()) throw std::runtime_error("unknown protocol field: " + key);
    }
    start = end;
  }
}

std::vector<S2::Generator> generatorCandidates() {
  static const char *args[] = {"s2", "control", "simulation=off"};
  S2::Options options(3, args, false);
  S2::Devices devices(options);
  return devices.generators;
}
void stopBothAndAcknowledge(int generator) {
  static const char *args[] = {"s2", "control", "simulation=off"};
  S2::Options options(3, args, false);
  S2::Devices devices(options);
  S2::DefaultStreamFactory factory;
  S2::Generator &selected = devices.GetGenerator(generator);
  selected.Open(devices, factory);
  selected.Send(encode(61, 0, 0).c_str());
  selected.Send(encode(61, 1, 0).c_str());
}
class RedirectControlOutput {
  std::streambuf *previous;
public:
  RedirectControlOutput() : previous(std::cout.rdbuf(std::cerr.rdbuf())) {}
  ~RedirectControlOutput() { std::cout.flush(); std::cout.rdbuf(previous); }
};

}

int main() {
  bool connected=false, armed=false, outputKnown=true, outputOn=false; int selectedGenerator=0; std::string line;
  while (std::getline(std::cin, line)) {
    const std::string id=stringField(line,"id"), op=stringField(line,"op");
    try {
      rejectUnknownFields(line);
      if (id.empty() || op.empty() || numberField(line,"v",0)!=1) throw std::runtime_error("invalid protocol envelope");
      if (op=="health" || op=="status") { respond(id,true,"\"result\":{\"connected\":"+std::string(connected?"true":"false")+",\"armed\":"+(armed?"true":"false")+",\"output_state\":\""+(outputKnown?(outputOn?"known_on":"known_off"):"unknown")+"\"}"); continue; }
      if (op=="discover") {
        auto candidates=generatorCandidates(); std::ostringstream result; result << "\"result\":{\"transport\":\"serial\",\"devices\":[";
        for(size_t index=0;index<candidates.size();++index){if(index)result<<",";result<<"{\"generator\":"<<candidates[index].id<<",\"path\":\""<<escape(candidates[index].filename)<<"\",\"identity_verified\":false}";}
        result << "]}"; respond(id,true,result.str()); continue;
      }
      if (op=="connect") {
        int generator=static_cast<int>(numberField(line,"generator",0));
        connected=false; armed=false; outputKnown=false;
        stopBothAndAcknowledge(generator);
        selectedGenerator=generator; connected=true; armed=true; outputKnown=true; outputOn=false;
        respond(id,true,"\"result\":{\"connected\":true,\"armed\":true,\"stop_acknowledged\":true,\"identity_verified\":false,\"output_state\":\"known_off\"}");continue;
      }
      if (op=="rearm") {
        if(!connected)throw std::runtime_error("not connected");
        armed=false; outputKnown=false;
        stopBothAndAcknowledge(selectedGenerator);
        armed=true;outputKnown=true;outputOn=false;
        respond(id,true,"\"result\":{\"armed\":true,\"stop_acknowledged\":true,\"output_state\":\"known_off\"}");continue;
      }
      if (!connected && !boolField(line,"simulation",false)) throw std::runtime_error("not connected");
      if ((op=="configure"||op=="start")&&!armed) throw std::runtime_error("explicit re-arm required");
      std::vector<std::string> commands=translate(op,line); bool inspection=boolField(line,"inspection",false), simulation=boolField(line,"simulation",false); bool dispatched=false;
      if (!inspection) {
        std::vector<std::string> args={"s2","control","generator="+std::to_string(static_cast<int>(numberField(line,"generator",selectedGenerator))),"channel="+std::to_string(static_cast<int>(numberField(line,"channel",1)))};
        if(simulation)args.push_back("simulation=on"); if(op=="configure"){double f=numberField(line,"frequency_hz",-1),a=numberField(line,"amplitude_vpp",-1);std::string w=stringField(line,"waveform");if(f>0)args.push_back("frequency="+std::to_string(f)+"Hz");if(a>=0)args.push_back("amplitude="+std::to_string(a)+"V");if(!w.empty())args.push_back("waveform="+w);}else if(op=="start")args.push_back("output=on");else if(op=="stop"||op=="emergency_stop"||op=="stop_all")args.push_back("output=off");
        if(!simulation&&(op=="stop"||op=="emergency_stop"||op=="stop_all")){
          try { stopBothAndAcknowledge(selectedGenerator); } catch (...) { outputKnown=false;armed=false;throw; }
          dispatched=true;
        } else {
          std::vector<const char*> argv;for(auto &a:args)argv.push_back(a.c_str());S2::Options options(static_cast<int>(argv.size()),argv.data(),false);S2::DefaultStreamFactory sf;S2::DefaultProgressMonitor pm(S2::Quiet,std::cerr);
          RedirectControlOutput redirect;
          int rc=S2::Control(options,pm,sf);if(rc!=0){outputKnown=false;armed=false;throw std::runtime_error("device command was not acknowledged");}dispatched=true;
        }
      }
      if(op=="start"){outputOn=true;outputKnown=true;}if(op=="stop"||op=="emergency_stop"||op=="stop_all"){outputOn=false;outputKnown=true;}
      respond(id,true,"\"result\":{\"requested\":true,\"native_commands\":"+array(commands)+",\"physical_dispatched\":"+(dispatched?"true":"false")+",\"acknowledged\":"+(dispatched?"true":"false")+",\"output_state\":\""+(outputKnown?(outputOn?"known_on":"known_off"):"unknown")+"\"}");
    } catch (const std::exception &e) { std::cerr << "somabridge-s2: " << e.what() << std::endl; respond(id,false,"\"error\":{\"code\":\"DEVICE_ERROR\",\"message\":\""+escape(e.what())+"\",\"output_state\":\""+(outputKnown?(outputOn?"known_on":"known_off"):"unknown")+"\",\"rearm_required\":"+(!outputKnown?"true":"false")+"}"); }
  }
  return 0;
}
