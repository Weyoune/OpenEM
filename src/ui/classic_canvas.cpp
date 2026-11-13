// SPDX-License-Identifier: GPL-3.0-only
// Implementation of ClassicCanvas: the 320x240 indexed-colour framebuffer
// that all classic-mode screens draw into, plus asset loading from the
// user's KB/*.vga files and the mouse cursor read from EM.EXE.
#include "ui/classic_canvas.hpp"
#include <algorithm>
#include <cstdlib>

namespace em::ui {

// Loads the base palette (KB/21.vga), the bitmap fonts and the mouse cursor.
// The cursor lives in the data block of the game's graphics routines, which
// starts at file offset 0xD9F0 of EM.EXE (see classic_font.cpp): at +0x563 is
// the offset of the cursor descriptor (u16 width, u16 height, u16 offset of
// the pixel data, all relative to that block), and at
// +0x55F/+0x561 the signed hot-spot offsets (i16 dx, dy) applied when the
// cursor is drawn. Only the 11x11 cursor of the known game version is
// accepted; anything else throws FormatError.
ClassicCanvas::ClassicCanvas(const std::filesystem::path& data)
    : data_(data), base_palette_(decode_palette(read_file(data / "KB" / "21.vga"))),
      palette_(base_palette_), font_(read_file(data / "EM.EXE")) {
    const Bytes exe=read_file(data/"EM.EXE");
    constexpr size_t base=0xD9F0;
    const size_t descriptor=base+le16(exe,base+0x563);
    const int width=le16(exe,descriptor),height=le16(exe,descriptor+2);
    const size_t pixels=base+le16(exe,descriptor+4);
    if(width!=11||height!=11||pixels+size_t(width*height)>exe.size())throw FormatError("EM.EXE: incompatible original cursor");
    cursor_=Image(width,height);std::copy_n(exe.begin()+std::ptrdiff_t(pixels),width*height,cursor_.pixels.begin());
    cursor_dx_=int16_t(le16(exe,base+0x55F));cursor_dy_=int16_t(le16(exe,base+0x561));
}

void ClassicCanvas::cursor(int x,int y){
    x+=cursor_dx_;y+=cursor_dy_;
    for(int row=0;row<cursor_.height;row++)for(int col=0;col<cursor_.width;col++){
        const auto pixel=cursor_.at(col,row);const int dx=x+col,dy=y+row;
        if(pixel&&dx>=0&&dx<pixels_.width&&dy>=0&&dy<pixels_.height)pixels_.at(dx,dy)=pixel;
    }
}

const Image& ClassicCanvas::asset(int id) {
    auto it = assets_.find(id);
    if (it == assets_.end()) {
        const Bytes source=read_file(data_ / "KB" / (std::to_string(id)+".vga"));
        if(classify_vga(source)==VgaKind::ImageWithPalette)asset_palettes_.emplace(id,decode_palette(source,6+le16(source,4)));
        it=assets_.emplace(id,decode_vga(source)).first;
    }
    return it->second;
}

void ClassicCanvas::reset_palette() { palette_ = base_palette_; }
void ClassicCanvas::set_dac(int index, int r, int g, int b) {
    auto& rgb = palette_.rgb.at(size_t(index));
    const int components[] = {r,g,b};
    for (int c=0;c<3;c++) { const int v=components[c]&63;rgb[size_t(c)]=uint8_t((v<<2)|(v>>4)); }
}
void ClassicCanvas::background(int id) {
    (void)asset(id);
    const auto found=asset_palettes_.find(id);
    palette_=found==asset_palettes_.end()?base_palette_:found->second;
    std::fill(pixels_.pixels.begin(),pixels_.pixels.end(),0);
    blit(id,0,0);
}

void ClassicCanvas::blit(int id,int x,int y,int sx,int sy,int width,int height,bool transparent,uint8_t key) {
    const auto& source=asset(id);
    if(width<0)width=source.width-sx;if(height<0)height=source.height-sy;
    for(int row=0;row<height;row++) {
        const int source_y=sy+row,dest_y=y+row;
        if(source_y<0||source_y>=source.height||dest_y<0||dest_y>=pixels_.height)continue;
        for(int column=0;column<width;column++) {
            const int source_x=sx+column,dest_x=x+column;
            if(source_x<0||source_x>=source.width||dest_x<0||dest_x>=pixels_.width)continue;
            auto color=source.pixels[size_t(source_y)*source.width+source_x];
            if(id==4&&button_edge_&&((source_y==180&&source_x>=18&&source_x<=71)||(source_x==18&&source_y>=181&&source_y<=188)))color=*button_edge_;
            if(!transparent||color!=key) pixels_.at(dest_x,dest_y)=color;
        }
    }
}

void ClassicCanvas::rect(int left,int top,int right,int bottom,uint8_t color) {
    left=std::max(left,0);top=std::max(top,0);right=std::min(right,pixels_.width-1);bottom=std::min(bottom,pixels_.height-1);
    for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++)pixels_.at(x,y)=color;
}
void ClassicCanvas::line(int x0,int y0,int x1,int y1,uint8_t color) {
    // The original line routine starts its error accumulator at zero instead
    // of the usual half step of Bresenham's algorithm; reproduced so the
    // stepped pixels land exactly where the game puts them.
    const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=std::abs(y1-y0),sy=y0<y1?1:-1;
    int error=0;
    for(;;) {
        if(x0>=0&&x0<320&&y0>=0&&y0<240)pixels_.at(x0,y0)=color;
        if(x0==x1&&y0==y1)break;
        if(dx>dy){x0+=sx;error+=dy;if(error>=dx){error-=dx;y0+=sy;}}
        else{y0+=sy;error+=dx;if(error>=dy){error-=dy;x0+=sx;}}
    }
}
void ClassicCanvas::outline(int left,int top,int right,int bottom,uint8_t color) {
    line(left,top,right,top,color);line(left,bottom,right,bottom,color);line(left,top,left,bottom,color);line(right,top,right,bottom,color);
}
void ClassicCanvas::frame(int x,int y,int columns,int rows) {
    if(columns<2||rows<2||columns>14||rows>13)throw FormatError("classic frame: invalid dimensions");
    for(int row=0;row<rows;row++)for(int col=0;col<columns;col++) {
        const int tile=(row==0?0:row==rows-1?6:3)+(col==0?0:col==columns-1?2:1);
        blit(4,x+24*col,y+19*row,48,20*tile,24,19);
    }
}
void ClassicCanvas::text(ClassicFontId face,int x,int baseline,std::string_view value,uint8_t color,int shadow,int right) {
    if(right>=0)x+=(right-x)/2-font_.measure(face,value)/2;
    // The game's text routine draws "black" ink (colour 0) with the opaque
    // black palette index 255, because index 0 is the transparent key of the
    // glyph blitter. The same substitution applies to the shadow.
    if(shadow>=0)font_.draw(pixels_,face,x+1,baseline+1,value,uint8_t(shadow==0?255:shadow));
    font_.draw(pixels_,face,x,baseline,value,color==0?255:color);
}
void ClassicCanvas::text_native(ClassicFontId face,int x,int baseline,std::string_view value,uint8_t color,int shadow,int right) {
    if(right>=0)x+=(right-x)/2-font_.measure_native(face,value)/2;
    if(shadow>=0)font_.draw_native(pixels_,face,x+1,baseline+1,value,uint8_t(shadow==0?255:shadow));
    font_.draw_native(pixels_,face,x,baseline,value,color==0?255:color);
}
std::vector<uint32_t> ClassicCanvas::argb() const {
    std::vector<uint32_t> result(pixels_.pixels.size());
    for(size_t i=0;i<result.size();i++){const auto& rgb=palette_.rgb[pixels_.pixels[i]];result[i]=0xff000000u|uint32_t(rgb[0])<<16|uint32_t(rgb[1])<<8|rgb[2];}
    return result;
}
Bytes ClassicCanvas::bmp_bytes() const {
    constexpr size_t stride=320*3,size=54+stride*240;
    Bytes bytes(size,0);bytes[0]='B';bytes[1]='M';
    auto put=[&](size_t at,uint32_t value,int count=4){for(int n=0;n<count;n++)bytes.at(at+size_t(n))=uint8_t(value>>(8*n));};
    put(2,uint32_t(size));put(10,54);put(14,40);put(18,320);put(22,240);put(26,1,2);put(28,24,2);put(34,uint32_t(size-54));
    for(int y=0;y<240;y++)for(int x=0;x<320;x++) {
        const auto& rgb=palette_.rgb[pixels_.pixels[size_t(y)*320+x]];const size_t at=54+size_t(239-y)*stride+size_t(x)*3;
        bytes[at]=rgb[2];bytes[at+1]=rgb[1];bytes[at+2]=rgb[0];
    }
    return bytes;
}
}  // namespace em::ui
