#pragma once
#include "runtime.hpp"
namespace nioh1fix::runtime {
consteval std::uint8_t HexDigit(char value) {
    return value <= '9' ? value - '0' : value - 'A' + 10;
}
template <std::size_t Size> struct PatternStorage {
    std::array<std::uint8_t, Size> bytes{}, mask{};
    constexpr operator BytePattern() const { return {bytes, mask}; }
    constexpr const std::uint8_t* data() const { return bytes.data(); }
    constexpr std::size_t size() const { return Size; }
};
template <std::size_t N> consteval auto Pattern(const char (&text)[N]) {
    static_assert((N - 1) % 2 == 0);
    PatternStorage<(N - 1) / 2> pattern{};
    for (std::size_t i = 0; i < pattern.bytes.size(); ++i) {
        if (text[2 * i] == '?') continue;
        pattern.bytes[i] = static_cast<std::uint8_t>(
            HexDigit(text[2 * i]) * 16 + HexDigit(text[2 * i + 1]));
        pattern.mask[i] = 0xFF;
    }
    return pattern;
}
inline constexpr auto kPresent = Pattern(
    "807D00007418488B4D0833D24885C9751A488B8FB02F0000448D4201EB10"
    "488B8FB02F00008B978C2F00004533C0488B01FF5040488B4F30");
inline constexpr auto kLimiter = Pattern(
    "48895C240848896C2410488974241848897C242041564883EC20803900488B"
    "F94863EA75318B410485C07413");
inline constexpr auto kGameplay = Pattern(
    "486305????????488D0C40488D05????????F30F100488C3");
inline constexpr auto kMotionSlots = Pattern(
    "488B4C1F08FF5048488BC8488D542420E8????????E8????????488B441F10"
    "0F28C8488B4C1F08FF9040010000");
inline constexpr auto kLinkedMotion = Pattern(
    "40534883EC404883B97001000000488BD90F297C24200F28F974490F297424"
    "30E8????????488B53180F28F0488B8378010000488B8B70010000F30F59F7"
    "488B5208FF9050010000");
inline constexpr auto kMotionComponent = Pattern(
    "0F2978B8440F294898440F295088E8????????440F28D0F3450F59D0E8????????");
inline constexpr auto kInput = Pattern(
    "4883EC28488B0D????????4885C97415E8????????488B0D????????4883C4"
    "28E9????????4883C428C3");
inline constexpr auto kCamera = Pattern(
    "488B0D????????F3440F103D????????4439A1AC0000007505F3410F59FF44"
    "39A1B00000007505F3450F59C7F3450F58D3F3450F58CC8B81B40000000F57C0");
inline constexpr auto kAim = Pattern(
    "F30F589FB0000000F30F58A700010000F30F5E1D????????F30F5E25????????"
    "F30F58DEF30F58E7F30F5AC40F54C5660F5AC00F2FC17635F30F100D????????"
    "F30F598F90010000");
inline constexpr auto kGrass = Pattern(
    "488B8708040000488D8FC02E18004C8DA8000500004885C075074C8D2D"
    "????????F30F107134F30F107930E8????????4C8BC00F28DE0F28CF498BCDE8"
    "????????488BB70804000041BE00000000");
inline constexpr auto kScl = Pattern(
    "8B5120F6C2017705F6C202766DF30F1061248BC283E0EF894120A8047610410F"
    "28C1F3410F5EC0F30F594134EB05F30F104128F30F10512C0F28CCF30F58C8"
    "0F2FD1F30F11492477060F2F493076120F");
inline constexpr auto kTextScroll = Pattern(
    "4883EC2880792F004C8BC90F846202000080792C000F84580200000FB74134"
    "6685C0740E66FFC86689413433C04883C428C38B49504533C0B20185C97411"
    "83E901742183E901743183F9017441EB4DF3410F104144410F2F413C724041"
    "C74150");
inline constexpr std::size_t kTextScrollOverwriteSize = 8;
inline constexpr auto kOcean = Pattern(
    "48895C24104889742418574883EC408B812401000033D28B9958010000FFC0F7"
    "B1200100008B8154010000488BF1448BC283C003488B913804000083C3034489"
    "8124010000488B49100F297424300F28F14A8B14C20FAFD8E8????????8B8E24");
inline constexpr auto kCloudPlane = Pattern(
    "4883EC38F30F1059780F28E1F30F1049740F2FD976110F28D4F30F59517CF3"
    "0F58D10F2FD3EB170F2FCB0F28D1761E0F28C4F30F59417CF30F5CD00F2FDA"
    "F30F1151747208F30F1159740F28D30F57C00F2FD00F860A030000F30F1089A00000");
inline constexpr auto kCloudCircle = Pattern(
    "0F570D????????0F28C1F30F5981D0000000F30F5881C8000000F30F1181C8"
    "000000F30F5989D4000000F30F5889CC000000F30F1189CC000000C3");
inline constexpr auto kCloudParticle = Pattern(
    "488BC4535557415441554881ECD0000000440F2948984533E4F3440F100D????????"
    "488BD9440F2950880F57C08B817C010000440F295C2470440F2964246044"
    "0F28E1440F296C2450440F29742440398180010000761A69816C010000CD0D01");
inline constexpr auto kClothPrimary = Pattern(
    "40535657415541564883EC40488B41584533D2488BF10F297424200F28F14889"
    "8C2488000000418BDA8B38897C2470443951187637");
inline constexpr auto kClothSecondary = Pattern(
    "48895C242056574155415641574883EC3033DB0F297424200F28F28BF24C8BF1"
    "895424608BFB395918763D");
inline constexpr auto kModelMatrixCopy = Pattern(
    "488B97F0000000E8A7C67500488B4720488BD74C8B83F000000048F7D8488B"
    "CB4D1BC9E8ABB8FFFF");
inline constexpr auto kClothMatrixCopy = Pattern(
    "488B97F0000000E8D7C87500488B4720488BD74C8B83F000000048F7D8488B"
    "CB4D1BC9E87BBAFFFF");
inline constexpr std::size_t kMatrixCopyCallOffset = 7;
inline constexpr std::array<std::uint8_t, 6> kLimiterPatch{0xC3,0x90,0x90,0x90,0x90,0x90};
inline constexpr std::array<HookSpec, kHookCount> kHooks{{
    {kCamera,44,10,64,-1,{7,8},2,"camera","Scaled the verified gameplay camera rotation response while preserving raw lock-on selection input."},
    {kAim,32,8,192,2,{3,4},2,"aiming-camera","Scaled the verified firearm and bow aiming camera input by the presentation interval."},
    {kGrass,51,9,128,1,{3,0},1,"grass-wind","Scaled the verified grass and bush wind phase by the presentation interval."},
    {kScl,51,5,256,3,{0,0},1,"SCL-animation","Scaled the live SCL interface animator by the presentation interval."},
    {kOcean,0,5,320,4,{1,0},1,"statistical-ocean","Scaled the verified statistical-ocean animation update by the presentation interval."},
    {kCloudPlane,0,9,384,5,{1,0},1,"cloud-plane","Scaled the verified cloud-plane animation update by the presentation interval."},
    {kCloudCircle,0,7,448,6,{1,0},1,"cloud-circle","Scaled the verified cloud-circle animation update by the presentation interval."},
    {kCloudParticle,0,5,512,7,{1,0},1,"cloud-particle","Scaled the verified cloud-particle animation update by the presentation interval."},
    {kClothPrimary,0,6,640,9,{0,0},0,"cloth-primary-diagnostics","Instrumented the primary cloth simulation pass."},
    {kClothSecondary,0,5,704,10,{0,0},0,"cloth-secondary-diagnostics","Instrumented the secondary cloth simulation pass."}
}};
} // namespace nioh1fix::runtime
