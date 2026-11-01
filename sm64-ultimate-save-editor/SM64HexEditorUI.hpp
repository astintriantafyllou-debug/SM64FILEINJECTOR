#pragma once
#include "SM64SystemCore.hpp"
#include <array>
#include <string>
class UI{sm64::Core&c;int slot=0,selected=0,profile=0;bool expert=false,autoRepair=true,word=true,all=false;unsigned addr=0x80207700;std::array<char,3>edit{};std::string note,codes;void open();void save();void dashboard();void beginner();void hex();void validation();void exports();void cheats();public:UI(sm64::Core&);void render();};
