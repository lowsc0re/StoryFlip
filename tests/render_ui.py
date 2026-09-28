from pathlib import Path
import re
source=Path('storyflip/storyflip.c')
if not source.exists(): source=Path('storyflip.c')
src=source.read_text()
def fn(name):
 m=re.search(r'static [^\n;]+\b'+name+r'\([^;]*?\) \{',src); start=m.start(); end=m.end(); depth=1
 while depth:
  depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[start:end]
enums=re.search(r'typedef enum \{.*?\} Action;',src,re.S).group()
model=re.search(r'typedef struct \{\n    Screen screen;.*?\} Model;',src,re.S).group()
names=re.search(r'static const char\* home_names\[\].*?;',src).group()
code=r'''
#include <u8g2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define SF_NAME 256
#define SF_ROWS 4
#define SF_VISIBLE 4
#define UNUSED(x) ((void)(x))
#include "sf_fonts.h"
#include "sf_i18n.h"
#define T(key) sf_tr(a->language,key)
#define SF_DIRECTORY 256u
#define SF_FAVORITE 1u
typedef u8g2_t Canvas;
enum { ColorWhite, ColorBlack };
enum { FontPrimary, FontSecondary };
enum { AlignLeft, AlignCenter, AlignRight, AlignTop, AlignBottom };
static bool sf_copy(char* d, size_t cap, const char* s) { if(strlen(s)>=cap)return false;strcpy(d,s);return true; }
static void canvas_clear(Canvas* c) {u8g2_ClearBuffer(c);}
static void canvas_set_color(Canvas* c, int v) {u8g2_SetDrawColor(c,v);}
static void canvas_set_font(Canvas* c, int v) {u8g2_SetFont(c,v==FontPrimary?u8g2_font_helvB08_tr:u8g2_font_haxrcorp4089_tr);u8g2_SetFontMode(c,1);}
static unsigned canvas_string_width(Canvas* c,const char* s){return u8g2_GetUTF8Width(c,s);}
static void canvas_draw_str(Canvas* c,int x,int y,const char* s){u8g2_DrawUTF8(c,x,y,s);}
static void canvas_draw_str_aligned(Canvas* c,int x,int y,int h,int v,const char* s){(void)v;unsigned w=canvas_string_width(c,s);if(h==AlignCenter)x-=w/2;if(h==AlignRight)x-=w;canvas_draw_str(c,x,y,s);}
static void canvas_draw_frame(Canvas* c,int x,int y,int w,int h){u8g2_DrawFrame(c,x,y,w,h);}
static void canvas_draw_box(Canvas* c,int x,int y,int w,int h){u8g2_DrawBox(c,x,y,w,h);}
static void canvas_draw_rframe(Canvas* c,int x,int y,int w,int h,int r){u8g2_DrawRFrame(c,x,y,w,h,r);}
static void canvas_draw_rbox(Canvas* c,int x,int y,int w,int h,int r){u8g2_DrawRBox(c,x,y,w,h,r);}
static void canvas_draw_line(Canvas* c,int x,int y,int a,int b){u8g2_DrawLine(c,x,y,a,b);}
static void canvas_draw_circle(Canvas* c,int x,int y,int r){u8g2_DrawCircle(c,x,y,r,U8G2_DRAW_ALL);}
'''
from PIL import Image
code += 'typedef struct {const uint8_t* data; unsigned width,height;} Icon;\n'
for path in sorted((source.parent/'assets').glob('*.png')):
 im=Image.open(path).convert('L');w,h=im.size
 data=[sum(((im.getpixel((x+b,y))<128) if x+b<w else False)<<b for b in range(8)) for y in range(h) for x in range(0,w,8)]
 name=path.stem
 code+='static const uint8_t bitmap_'+name+'[] = {'+','.join(map(str,data))+'};\n'
 code+=f'static const Icon I_{name}={{bitmap_{name},{w},{h}}};\n'
code += r''' 
static void canvas_draw_icon(Canvas* c,int x,int y,const Icon* i){u8g2_DrawXBM(c,x,y,i->width,i->height,i->data);}
static void canvas_set_bitmap_mode(Canvas* c,bool alpha){u8g2_SetBitmapMode(c,alpha);}
static void canvas_draw_disc(Canvas* c,int x,int y,int r){u8g2_DrawDisc(c,x,y,r,U8G2_DRAW_ALL);}
static void canvas_invert_color(Canvas* c){u8g2_SetDrawColor(c,!c->draw_color);}
static void canvas_set_custom_u8g2_font(Canvas* c,const uint8_t* f){u8g2_SetFont(c,f);u8g2_SetFontMode(c,1);}
'''
code+=enums+'\n'+model+'\n'+names+'\n'+'\n'.join(fn(n) for n in ['draw_text','draw'])
code+=r'''
static void dump(Canvas* c,const char* name){
 FILE* f=fopen(name,"wb");fprintf(f,"P1\n128 64\n");
 uint8_t* buf=u8g2_GetBufferPtr(c);
 for(int y=0;y<64;y++){for(int x=0;x<128;x++)fprintf(f,"%u ",(buf[(y/8)*128+x]>>(y%8))&1);fputc('\n',f);}fclose(f);
}
int main(void){
 Canvas c;u8g2_Setup_null(&c,U8G2_R0,NULL,NULL);
 static uint8_t buffer[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 c.u8x8.display_info=&info;
 u8g2_SetupBuffer(&c,buffer,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 for(unsigned lang=0;lang<3;lang++) {
 Model m={0};m.language=lang;char path[128];
 #define SAVE(name) snprintf(path,sizeof(path),"tests/ui-%u-" name ".pbm",lang);draw(&c,&m);dump(&c,path)
 #define TR(k) sf_tr(lang,k)
 m.screen=Home;m.home=0;SAVE("home");
 for(unsigned selected=0; selected<6; selected++){m.home=selected;snprintf(path,sizeof(path),"tests/home-%u-%u.pbm",lang,selected);draw(&c,&m);dump(&c,path);}
 m.screen=Splash;SAVE("splash");
 m.screen=List;strcpy(m.title,TR("Sammlung"));m.total=250;m.rows=4;
 strcpy(m.lines[0],"Beispielgeschichte");m.flags[0]=SF_FAVORITE|(5<<4);
 strcpy(m.lines[1],"Abenteuer");strcpy(m.lines[2],"Geschichten");strcpy(m.lines[3],"Sammlung A");m.flags[3]=SF_DIRECTORY;SAVE("list");
 strcpy(m.title,TR("Select folder"));strcpy(m.lines[0],TR("Use this folder"));strcpy(m.lines[1],"Sammlung A");strcpy(m.lines[2],"Sammlung B");
 m.flags[0]=SF_DIRECTORY|512u;m.flags[1]=m.flags[2]=m.flags[3]=SF_DIRECTORY;strcpy(m.lines[3],"Weitere Dateien");m.total=15;SAVE("root");
 m.screen=Settings;strcpy(m.title,TR("Einstellungen"));m.selection=0;m.total=9;
 strcpy(m.lines[0],TR("Language"));strcpy(m.lines[1],TR("Index / Diagnose"));strcpy(m.lines[2],TR("Suchindex erneuern"));strcpy(m.lines[3],TR("Verlauf loeschen"));SAVE("settings");
 m.screen=Language;m.rows=3;m.selection=lang;strcpy(m.title,TR("Language"));strcpy(m.lines[0],"English");strcpy(m.lines[1],"Deutsch");strcpy(m.lines[2],"Français");SAVE("languages");
 m.screen=Confirm;m.selection=0;strcpy(m.title,TR("Bestaetigen"));m.rows=3;
 strcpy(m.lines[0],TR("Vollstaendigen Suchindex"));strcpy(m.lines[1],TR("erstellen? Das kann dauern."));strcpy(m.lines[2],TR("Current root will be indexed."));
 strcpy(m.footer,TR("OK: Starten   Back: Abbruch"));SAVE("confirm");
 m.screen=Emulate;strcpy(m.title,TR("SLIX Emulation"));
 strcpy(m.lines[0],"Eine lange Beispielgeschichte.nfc");strcpy(m.lines[1],TR("Emulation aktiv"));strcpy(m.lines[2],TR("An Lesegerät halten."));strcpy(m.footer,TR("Back: Stoppen"));SAVE("emulate");
 uint8_t before[1024];memcpy(before,buffer,1024);m.tick=500;draw(&c,&m);if(memcmp(before,buffer,1024))abort();
 m.screen=Home;m.home=1;m.tick=0;draw(&c,&m);memcpy(before,buffer,1024);m.tick=500;draw(&c,&m);if(memcmp(before,buffer,1024))abort();
 m.screen=About;m.rows=2;strcpy(m.title,"StoryFlip");strcpy(m.lines[0],"Version 0.4.2");strcpy(m.lines[1],"Vibecode Version");strcpy(m.footer,TR("OK / Back: Zurueck"));SAVE("about");
 }
 return 0;
}
'''
Path('tests/render.generated.c').write_text(code)
