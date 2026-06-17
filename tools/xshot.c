// xshot — capture an X11 window (by WM name substring) to a PNG.
// dev tool for verifying terminals on this box (no grim/scrot/imagemagick here).
//   xshot <name-substring> <out.png>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t CRC[256];
static void crc_init(void){for(uint32_t n=0;n<256;n++){uint32_t c=n;for(int k=0;k<8;k++)c=(c&1)?0xedb88320u^(c>>1):c>>1;CRC[n]=c;}}
static uint32_t crc32b(uint32_t c,const uint8_t*p,size_t n){c^=0xffffffffu;for(size_t i=0;i<n;i++)c=CRC[(c^p[i])&0xff]^(c>>8);return c^0xffffffffu;}
static void be32(FILE*f,uint32_t v){fputc(v>>24,f);fputc(v>>16,f);fputc(v>>8,f);fputc(v,f);}
static void chunk(FILE*f,const char*t,const uint8_t*d,size_t n){
  be32(f,n);
  uint8_t*tmp=malloc(n+4);memcpy(tmp,t,4);memcpy(tmp+4,d,n);
  fwrite(tmp,1,n+4,f);be32(f,crc32b(0,tmp,n+4));free(tmp);
}
static int write_png(const char*path,int w,int h,const uint8_t*rgb){
  crc_init();
  size_t rawn=(size_t)h*(w*3+1);uint8_t*raw=malloc(rawn);size_t o=0;
  for(int y=0;y<h;y++){raw[o++]=0;memcpy(raw+o,rgb+(size_t)y*w*3,w*3);o+=w*3;}
  size_t zn=rawn+rawn/65535*5+64;uint8_t*z=malloc(zn);size_t zo=0;
  z[zo++]=0x78;z[zo++]=0x01;size_t pos=0;
  while(pos<rawn){size_t n=rawn-pos>65535?65535:rawn-pos;
    z[zo++]=(pos+n>=rawn)?1:0;z[zo++]=n&0xff;z[zo++]=n>>8;z[zo++]=~n&0xff;z[zo++]=(~n>>8)&0xff;
    memcpy(z+zo,raw+pos,n);zo+=n;pos+=n;}
  uint32_t a=1,b=0;for(size_t i=0;i<rawn;i++){a=(a+raw[i])%65521;b=(b+a)%65521;}
  z[zo++]=b>>8;z[zo++]=b;z[zo++]=a>>8;z[zo++]=a;
  FILE*f=fopen(path,"wb");if(!f)return 0;
  uint8_t sig[8]={137,80,78,71,13,10,26,10};fwrite(sig,1,8,f);
  uint8_t ih[13];uint32_t W=w,H=h;ih[0]=W>>24;ih[1]=W>>16;ih[2]=W>>8;ih[3]=W;ih[4]=H>>24;ih[5]=H>>16;ih[6]=H>>8;ih[7]=H;ih[8]=8;ih[9]=2;ih[10]=0;ih[11]=0;ih[12]=0;
  chunk(f,"IHDR",ih,13);chunk(f,"IDAT",z,zo);chunk(f,"IEND",(uint8_t*)"",0);
  fclose(f);free(raw);free(z);return 1;
}
static char*winname(Display*d,Window w){
  char*n=NULL;XFetchName(d,w,&n);
  if(n)return n;
  Atom net=XInternAtom(d,"_NET_WM_NAME",False),utf8=XInternAtom(d,"UTF8_STRING",False);
  Atom type;int fmt;unsigned long nit,bytes;unsigned char*p=NULL;
  if(XGetWindowProperty(d,w,net,0,256,False,utf8,&type,&fmt,&nit,&bytes,&p)==Success&&p)return (char*)p;
  return NULL;
}
static Window find(Display*d,Window w,const char*sub){
  char*n=winname(d,w);Window found=0;
  if(n){if(strstr(n,sub))found=w;XFree(n);}
  if(found)return found;
  Window root,parent,*ch;unsigned int nc;
  if(XQueryTree(d,w,&root,&parent,&ch,&nc)){
    for(unsigned i=0;i<nc&&!found;i++)found=find(d,ch[i],sub);
    if(ch)XFree(ch);
  }
  return found;
}
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: xshot <name> <out.png>\n");return 2;}
  Display*d=XOpenDisplay(NULL);if(!d){fprintf(stderr,"no display\n");return 1;}
  Window w=find(d,DefaultRootWindow(d),argv[1]);
  if(!w){fprintf(stderr,"window '%s' not found\n",argv[1]);return 1;}
  XWindowAttributes wa;XGetWindowAttributes(d,w,&wa);
  XImage*img=XGetImage(d,w,0,0,wa.width,wa.height,AllPlanes,ZPixmap);
  if(!img){fprintf(stderr,"XGetImage failed\n");return 1;}
  uint8_t*rgb=malloc((size_t)wa.width*wa.height*3);
  for(int y=0;y<wa.height;y++)for(int x=0;x<wa.width;x++){
    unsigned long p=XGetPixel(img,x,y);size_t o=((size_t)y*wa.width+x)*3;
    rgb[o]=(p>>16)&0xff;rgb[o+1]=(p>>8)&0xff;rgb[o+2]=p&0xff;
  }
  int ok=write_png(argv[2],wa.width,wa.height,rgb);
  fprintf(stderr,"xshot %s %dx%d ok=%d\n",argv[2],wa.width,wa.height,ok);
  return !ok;
}
