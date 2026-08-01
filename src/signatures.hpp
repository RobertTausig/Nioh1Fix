#pragma once

#include "runtime.hpp"
#include <string_view>

namespace nioh1fix::runtime {
consteval std::uint8_t HexDigit(char value) {
    return value <= '9' ? value - '0' : value - 'A' + 10;
}
template <std::size_t N> consteval auto Hex(const char (&text)[N]) {
    static_assert((N - 1) % 2 == 0);
    std::array<std::uint8_t, (N - 1) / 2> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(HexDigit(text[2 * i]) * 16 +
                                             HexDigit(text[2 * i + 1]));
    return bytes;
}

inline constexpr auto kPresent = Hex(
    "807D00007418488B4D0833D24885C9751A488B8FB02F0000448D4201EB10"
    "488B8FB02F00008B978C2F00004533C0488B01FF5040488B4F30");
inline constexpr auto kLimiter = Hex(
    "48895C240848896C2410488974241848897C242041564883EC20803900488B"
    "F94863EA75318B410485C07413");
inline constexpr auto kGameplay = Hex(
    "4863059130D300488D0C40488D0576D79200F30F100488C3");
inline constexpr auto kMotionSlots = Hex(
    "488B4C1F08FF5048488BC8488D542420E8EBD287FFE836185200488B441F10"
    "0F28C8488B4C1F08FF9040010000");
inline constexpr auto kLinkedMotion = Hex(
    "40534883EC404883B97001000000488BD90F297C24200F28F974490F297424"
    "30E89B175200488B53180F28F0488B8378010000488B8B70010000F30F59F7"
    "488B5208FF9050010000");
inline constexpr auto kMotionComponent = Hex(
    "0F2978B8440F294898440F295088E86A065200440F28D0F3450F59D0E89CDA0600");
inline constexpr auto kInput = Hex(
    "4883EC28488B0D2DF3CA014885C97415E80B020000488B0D1CF3CA014883C4"
    "28E94BDAFFFF4883C428C3");
inline constexpr auto kCamera = Hex(
    "488B0DEEBF0401F3440F103DF557D3004439A1AC0000007505F3410F59FF44"
    "39A1B00000007505F3450F59C7F3450F58D3F3450F58CC8B81B40000000F57C0");
inline constexpr auto kAim = Hex(
    "F30F589FB0000000F30F58A700010000F30F5E1D83A6D200F30F5E257BA6D2"
    "00F30F58DEF30F58E7F30F5AC40F54C5660F5AC00F2FC17635F30F100D1B15"
    "F100F30F598F90010000");
inline constexpr auto kGrass = Hex(
    "488B8708040000488D8FC02E18004C8DA8000500004885C075074C8D2D36D9"
    "F800F30F107134F30F107930E897C6A0FF4C8BC00F28DE0F28CF498BCDE8F6"
    "BEA2FF488BB70804000041BE00000000");
inline constexpr auto kScl = Hex(
    "8B5120F6C2017705F6C202766DF30F1061248BC283E0EF894120A8047610410F"
    "28C1F3410F5EC0F30F594134EB05F30F104128F30F10512C0F28CCF30F58C8"
    "0F2FD1F30F11492477060F2F493076120F");
inline constexpr auto kOcean = Hex(
    "48895C24104889742418574883EC408B812401000033D28B9958010000FFC0F7"
    "B1200100008B8154010000488BF1448BC283C003488B913804000083C3034489"
    "8124010000488B49100F297424300F28F14A8B14C20FAFD8E8D334F3FF8B8E24");
inline constexpr auto kCloudPlane = Hex(
    "4883EC38F30F1059780F28E1F30F1049740F2FD976110F28D4F30F59517CF3"
    "0F58D10F2FD3EB170F2FCB0F28D1761E0F28C4F30F59417CF30F5CD00F2FDA"
    "F30F1151747208F30F1159740F28D30F57C00F2FD00F860A030000F30F1089A00000");
inline constexpr auto kCloudCircle = Hex(
    "0F570D39E31D010F28C1F30F5981D0000000F30F5881C8000000F30F1181C8"
    "000000F30F5989D4000000F30F5889CC000000F30F1189CC000000C3");
inline constexpr auto kCloudParticle = Hex(
    "488BC4535557415441554881ECD0000000440F2948984533E4F3440F100DCE99"
    "1D01488BD9440F2950880F57C08B817C010000440F295C2470440F2964246044"
    "0F28E1440F296C2450440F29742440398180010000761A69816C010000CD0D01");
inline constexpr std::array<std::uint8_t, 6> kLimiterPatch{0xC3,0x90,0x90,0x90,0x90,0x90};
inline constexpr std::array<HookSpec, 8> kHooks{{
    {kCamera,44,10,64,-1,{10,9},2,"camera","Scaled the verified gameplay camera's controller and mouse input by the presentation interval."},
    {kAim,32,8,192,2,{3,4},2,"aiming-camera","Scaled the verified firearm and bow aiming camera input by the presentation interval."},
    {kGrass,51,9,128,1,{3,0},1,"grass-wind","Scaled the verified grass and bush wind phase by the presentation interval."},
    {kScl,30,9,256,3,{0,0},1,"SCL-animation","Scaled the live seconds-based SCL interface animator by the presentation interval."},
    {kOcean,0,5,320,4,{1,0},1,"statistical-ocean","Scaled the verified statistical-ocean animation update by the presentation interval."},
    {kCloudPlane,0,9,384,5,{1,0},1,"cloud-plane","Scaled the verified cloud-plane animation update by the presentation interval."},
    {kCloudCircle,0,7,448,6,{1,0},1,"cloud-circle","Scaled the verified cloud-circle animation update by the presentation interval."},
    {kCloudParticle,0,5,512,7,{1,0},1,"cloud-particle","Scaled the verified cloud-particle animation update by the presentation interval."}
}};
} // namespace nioh1fix::runtime
