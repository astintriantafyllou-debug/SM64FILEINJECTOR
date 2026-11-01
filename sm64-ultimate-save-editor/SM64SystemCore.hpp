#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace sm64 {
constexpr size_t Size=512, Block=56; constexpr std::array<size_t,4>P{0,0x70,0xE0,0x150},B{0x38,0xA8,0x118,0x188};
constexpr std::array<const char*,15> Courses{"Bob-omb Battlefield","Whomp's Fortress","Jolly Roger Bay","Cool, Cool Mountain","Big Boo's Haunt","Hazy Maze Cave","Lethal Lava Land","Shifting Sand Land","Dire, Dire Docks","Snowman's Land","Wet-Dry World","Tall, Tall Mountain","Tiny-Huge Island","Tick Tock Clock","Rainbow Ride"};
enum class Profile{Raw,EverDriveV1,EverDriveV2,EverDriveX,ED64Plus,Retrode2,EEP,SAV,SRA};
struct BlockState{bool magic{},checksum{};uint16_t stored{},computed{};}; struct Report{std::array<BlockState,8>slots{};std::array<BlockState,2>menu{};bool valid{};std::vector<std::string>notes;};
class Core { public: using Bytes=std::array<uint8_t,Size>; Core(); bool load(const std::filesystem::path&,bool,std::string&);bool save(const std::filesystem::path&,std::string&);bool exportFile(const std::filesystem::path&,Profile,std::string&);Bytes bytes()const{return data_;}uint8_t byte(size_t i)const{return data_.at(i);}void set(size_t,uint8_t);void setMirrored(int,size_t,uint8_t);Report validate()const;std::vector<std::string> repair();void normalize();std::string gameShark(uint32_t,bool,bool)const; private:Bytes data_{},base_{};static uint16_t sum(const uint8_t*,size_t);static void fix(uint8_t*,size_t);static bool write(const std::filesystem::path&,const std::vector<uint8_t>&,std::string&);};}
