#ifndef UVHTTP_H2C_INTERNAL_H
#define UVHTTP_H2C_INTERNAL_H
#include "http_internal.h"
namespace uvhttp::detail {
inline bool decodeSettings(std::string_view encoded, std::string &output) {
    output.clear();
    if (encoded.size() % 4 == 1 || encoded.size() > 16384) return false;
    unsigned accumulator=0, bits=0;
    for (unsigned char character : encoded) {
        unsigned digit;
        if (character>='A' && character<='Z') digit=character-'A';
        else if (character>='a' && character<='z') digit=character-'a'+26;
        else if (character>='0' && character<='9') digit=character-'0'+52;
        else if (character=='-') digit=62;
        else if (character=='_') digit=63;
        else return false;
        accumulator=(accumulator<<6)|digit; bits+=6;
        if(bits>=8) { bits-=8; output.push_back(static_cast<char>((accumulator>>bits)&255)); }
    }
    if (bits && (accumulator & ((1u<<bits)-1))) return false;
    if (output.size()%6) return false;
    for(std::size_t offset=0;offset<output.size();offset+=6) {
        auto byte=[&](std::size_t index){return static_cast<unsigned char>(output[offset+index]);};
        unsigned id=(byte(0)<<8)|byte(1);
        std::uint32_t value=(std::uint32_t(byte(2))<<24)|(byte(3)<<16)|(byte(4)<<8)|byte(5);
        if ((id==2 && value>1) || (id==4 && value>0x7fffffff) || (id==5 && (value<16384 || value>16777215))) return false;
    }
    return true;
}
}
#endif
