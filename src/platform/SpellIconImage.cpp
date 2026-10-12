#include "SpellIconImage.hpp"
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cstring>

namespace {
bool Wic(const std::vector<uint8_t>& bytes,SpellIconImage& image) {
    // Windows supplies JPEG/PNG decoding; no downloaded decoder or converted assets enter the package.
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* factory=nullptr;IWICStream* stream=nullptr;IWICBitmapDecoder* decoder=nullptr;
    IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;bool success=false;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),DWORD(bytes.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder)) &&
        SUCCEEDED(decoder->GetFrame(0,&frame)) && SUCCEEDED(frame->GetSize(&image.width,&image.height)) &&
        image.width>0 && image.height>0 && image.width<=512 && image.height<=512 &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) {
        image.pixels.resize(size_t(image.width)*image.height*4);
        success=SUCCEEDED(converter->CopyPixels(nullptr,image.width*4,UINT(image.pixels.size()),image.pixels.data()));
    }
    if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();
    if(stream)stream->Release();if(factory)factory->Release();if(SUCCEEDED(apartment))CoUninitialize();
    return success;
}
}
bool SpellIconImage::Decode(const std::vector<uint8_t>& bytes) {
    width=height=0;pixels.clear();if(bytes.empty() || bytes.size()>8*1024*1024)return false;
    auto word=[&](size_t at) {uint32_t value=0;if(at+4<=bytes.size())std::memcpy(&value,bytes.data()+at,4);return value;};
    bool success=false;
    if(word(0)==0x31504C42 && bytes.size()>=156) {
        width=word(12);height=word(16);unsigned alpha=word(8),kind=word(4),offset=word(28),length=word(92);
        if(!width || !height || width>512 || height>512 || offset<156 || offset>bytes.size() || length>bytes.size()-offset)return false;
        if(kind==0 && bytes.size()>=160) {
            // BLP1 stores a shared JPEG prefix followed by separate mip payloads.
            unsigned prefix=word(156);
            if(prefix<=bytes.size()-160 && offset>=160+prefix && length) {
                std::vector<uint8_t> jpeg(bytes.begin()+160,bytes.begin()+160+prefix);
                jpeg.insert(jpeg.end(),bytes.begin()+offset,bytes.begin()+offset+length);success=Wic(jpeg,*this);
                // Warcraft's BLP JPEG color components are BGR; ordinary JPEG/PNG remain RGB.
                if(success)for(size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
            }
        } else if(kind==1 && offset>=1180 && (alpha==0 || alpha==1 || alpha==4 || alpha==8)) {
            size_t count=size_t(width)*height,alphaBytes=(count*alpha+7)/8;
            if(length>=count+alphaBytes && bytes.size()>=1180) {
                pixels.resize(count*4);
                // Palettes are BGRA; packed alpha follows the complete index plane, least significant bit first.
                for(size_t i=0;i<count;++i) {
                    size_t palette=156+size_t(bytes[offset+i])*4;
                    pixels[i*4]=bytes[palette+2];pixels[i*4+1]=bytes[palette+1];pixels[i*4+2]=bytes[palette];
                    unsigned a=alpha ? (bytes[offset+count+i*alpha/8]>>(i*alpha%8))&((1u<<alpha)-1) : 255;
                    pixels[i*4+3]=uint8_t(alpha ? a*255/((1u<<alpha)-1) : a);
                }
                success=true;
            }
        }
    } else success=Wic(bytes,*this);
    if(!success) {width=height=0;pixels.clear();}return success;
}
