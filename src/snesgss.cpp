//---------------------------------------------------------------------------


#define VERSION_STR		"SNES GSS v1.4"

#define CONFIG_NAME		"snesgss.cfg"
#define PROJECT_SIGNATURE 	"[SNESGSS Module]"
#define INSTRUMENT_SIGNATURE 	"[SNESGSS Instrument]"

#define EX_VOL_SIGNATURE	"[EX VOL]"

#define UPDATE_RATE_HZ		160
#define DEFAULT_SPEED		(UPDATE_RATE_HZ/(120*(1.0/60.0)*4)) //default speed for 120 BPM

#define ENABLE_SONG_COMPRESSION

#if defined(WIN32) || defined(_WIN32) 
  #define PATH_SEPARATOR '\\'
#else 
  #define PATH_SEPARATOR '/'
#endif 

#include "snesgss.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "readwrite.h"
#include "3band_eq.h"
#include "spc700.h"
#include "gme/gme.h"
#include "brr/brr.h"
#include "brr/brr_encoder.h"


const std::string NoteNames[12]={"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};


SnesGssExporter::SnesGssExporter()
{
	BRRTemp=NULL;
	BRRTempSize=0;
	BRRTempAllocSize=0;

	memset(SPCMem,0,sizeof(SPCMem));
	memset(SPCTemp,0,sizeof(SPCTemp));

	SPCInstrumentsSize=0;
	SPCMusicSize=0;
	SPCEffectsSize=0;
	InstrumentsCount=0;
	SPCMusicLargestSize=0;

	UpdateSampleData=true;

	ModuleClear();
}
//---------------------------------------------------------------------------

SnesGssExporter::~SnesGssExporter()
{
	int i;

	if(BRRTemp) free(BRRTemp);

	for(i=0;i<MAX_INSTRUMENTS;++i)
		if(insList[i].source)
			free(insList[i].source);
}

//---------------------------------------------------------------------------

float SnesGssExporter::get_volume_scale(int vol)
{
	if(vol<128) return 1.0f+(float)(vol-128)/128.0f; else return 1.0f+(float)(vol-128)/64.0f;
}
//---------------------------------------------------------------------------

void SnesGssExporter::BRREncode(int ins)
{
	const char resample_list[]="nlcsb";
	int i,smp,ptr,off,blocks,src_length,new_length,sum;
	int new_loop_start,padding,loop_size,new_loop_size;
	int src_loop_start,src_loop_end;
	short *source,*sample,*temp;
	float volume,s,delta,fade,ramp;
	bool loop_enable,loop_flag,end_flag;
	char resample_type;
	float scale;
	EQSTATE eq;
	double in_sample,out_sample,band_low,band_high,rate;

	new_loop_start = 0;

	if(BRRTemp)
	{
		free(BRRTemp);

		BRRTemp=NULL;
		BRRTempSize=0;
		BRRTempLoop=-1;
	}

	//get the source sample, downsample it if needed

	src_length=insList[ins].length;

	if(!insList[ins].source||src_length<16) return;//sample is shorter than one BRR block

	source=(short*)malloc(src_length*sizeof(short));

	memcpy(source,insList[ins].source,src_length*sizeof(short));

	//apply EQ if it is not reset, before any downsampling, as it needed to compensate downsampling effects as well

	if(insList[ins].eq_low!=0||insList[ins].eq_mid!=0||insList[ins].eq_high!=0)
	{
		rate=insList[ins].source_rate;

		band_low=rate/50.0;//880 for 44100
		band_high=rate/8.82;//5000 for 44100

		init_3band_state(&eq,band_low,band_high,rate);

		eq.lg = (double)(64+insList[ins].eq_low)/64.0f;
		eq.mg = (double)(64+insList[ins].eq_mid)/64.0f;
		eq.hg = (double)(64+insList[ins].eq_high)/64.0f;

		for(i=0;i<src_length;++i)
		{
			in_sample=(double)source[i]/32768.0;

			out_sample=do_3band(&eq,in_sample);

			out_sample*=32768.0;

			if(out_sample<-32768) out_sample=-32768;
			if(out_sample> 32767) out_sample= 32767;

			source[i]=(short int)out_sample;
		}
	}

	//get scale factor for downsampling

	resample_type=resample_list[insList[ins].resample_type];

	src_loop_start=insList[ins].loop_start;
	src_loop_end  =insList[ins].loop_end+1;//loop_end is the last sample of the loop, to calculate length it needs to be next to the last

	switch(insList[ins].downsample_factor)
	{
	case 1:  scale=.5f;  break;
	case 2:  scale=.25f; break;
	default: scale=1.0f;
	}

	if(scale!=1.0f)
	{
		new_length=(float(src_length)*scale);

		source=resample(source,src_length,new_length,resample_type);

		src_length    =new_length;
		src_loop_start=(float(src_loop_start)*scale);
		src_loop_end  =(float(src_loop_end  )*scale);
	}

	//align the sample as required

	loop_enable=insList[ins].loop_enable;

	if(!loop_enable)//no loop, just pad the source with zeroes to 16-byte boundary
	{
		new_length=(src_length+15)/16*16;

		sample=(short*)malloc(new_length*sizeof(short));

		ptr=0;

		for(i=0;i<src_length;++i) sample[ptr++]=source[i];

		for(i=src_length;i<new_length;++i) sample[ptr++]=0;//pad with zeroes

		BRRTempLoop=-1;
	}
	else
	{
		if(!insList[ins].loop_unroll)//resample the loop part, takes less memory, but lower quality of the loop
		{
			new_loop_start=(src_loop_start+15)/16*16;//align the loop start point to 16 samples

			padding=new_loop_start-src_loop_start;//calculate padding, how many zeroes to insert at beginning

			loop_size=src_loop_end-src_loop_start;//original loop length

			new_loop_size=loop_size/16*16;//calculate new loop size, aligned to 16 samples

			if((loop_size&15)>=8) new_loop_size+=16;//align to closest point, to minimize detune

			new_length=new_loop_start+new_loop_size;//calculate new source length

			sample=(short*)malloc(new_length*sizeof(short));

			ptr=0;

			for(i=0;i<padding;++i) sample[ptr++]=0;//add the padding bytes

			for(i=0;i<src_loop_start;++i) sample[ptr++]=source[i];//copy the part before loop

			if(new_loop_size==loop_size)//just copy the loop part
			{
				for(i=0;i<new_loop_size;++i) sample[ptr++]=source[src_loop_start+i];
			}
			else
			{
				temp=(short*)malloc(loop_size*sizeof(short));//temp copy of the looped part, as resample function frees up the source

				memcpy(temp,&source[src_loop_start],loop_size*sizeof(short));

				temp=resample(temp,loop_size,new_loop_size,resample_type);

				for(i=0;i<new_loop_size;++i) sample[ptr++]=temp[i];

				free(temp);
			}

			BRRTempLoop=new_loop_start/16;//loop point in blocks
		}
		else//unroll the loop, best quality in trade for higher memory use
		{
			new_loop_start=(src_loop_start+15)/16*16;//align the loop start point to 16 samples

			padding=new_loop_start-src_loop_start;//calculate padding, how many zeroes to insert at beginning

			loop_size=src_loop_end-src_loop_start;//original loop length

			new_length=new_loop_start;

			sample=(short*)malloc(new_length*sizeof(short));

			ptr=0;

			for(i=0;i<padding;++i) sample[ptr++]=0;//add the padding bytes

			for(i=0;i<src_loop_start;++i) sample[ptr++]=source[i];//copy the part before loop

			while(1)
			{
				if(new_length<ptr+loop_size)
				{
					new_length=ptr+loop_size;

					sample=(short*)realloc(sample,new_length*sizeof(short));
				}

				for(i=0;i<loop_size;++i) sample[ptr++]=source[src_loop_start+i];

				new_length=ptr;

				if(!(new_length&15)||new_length>=65536) break;
			}

			BRRTempLoop=new_loop_start/16;//loop point in blocks
		}
	}

	free(source);

	//apply volume

	volume=get_volume_scale(insList[ins].source_volume);

	for(i=0;i<new_length;++i)
	{
		smp=(int)(((float)sample[i])*volume);

		if(smp<-32768) smp=-32768;
		if(smp> 32767) smp= 32767;

		sample[i]=smp;
	}

	//smooth out the loop transition

	if(loop_enable&&insList[ins].ramp_enable)
	{
		ptr=new_length-16;

		fade=1.0f;
		ramp=0.0f;
		delta=((float)sample[new_loop_start])/16.0f;

		for(i=0;i<16;++i)
		{
			s=(float)sample[ptr];

			s=s*fade+ramp;
			fade-=1.0f/16.0f;
			ramp+=delta;

			sample[ptr++]=(short)s;
		}
	}

	//convert to brr

	BRRTempAllocSize=16384;

	BRRTemp=(unsigned char*)malloc(BRRTempAllocSize);

	ptr=0;
	off=0;

	blocks=new_length/16;

	sum=0;

	//add initial block if there is any non-zero value in the first sample block
	//it is not clear if it is really needed

	for(i=0;i<16;++i) sum+=sample[i];

	if(sum)
	{
		memset(BRRTemp+ptr,0,9);

		if(loop_enable) BRRTemp[0]=0x02;//loop flag is always set for a looped sample

		ptr+=9;
	}

	//this ia a magic line
	ADPCMBlockMash(sample+(blocks-1)*16,false,true);
	//prevents clicks at the loop point and sound glitches somehow
	//tested few times it really affects the result
	//it seems that it caused by the squared error calculation in the function

	for(i=0;i<blocks;++i)
	{
		loop_flag=loop_enable&&((i==BRRTempLoop)?true:false);//loop flag is only set for the loop position

		end_flag =(i==blocks-1)?true:false;//end flag is set for the last block

		memset(BRR,0,9);

		ADPCMBlockMash(sample+off,loop_flag,end_flag);

		if(loop_enable) BRR[0]|=0x02;//loop flag is always set for a looped sample

		if(end_flag)  BRR[0]|=0x01;//end flag

		memcpy(BRRTemp+ptr,BRR,9);

		off+=16;
		ptr+=9;

		if(ptr>=BRRTempAllocSize-9)
		{
			BRRTempAllocSize+=16384;
			BRRTemp=(unsigned char*)realloc(BRRTemp,BRRTempAllocSize);
		}
	}

	free(sample);

	BRRTempSize=ptr;//actual encoded sample size in bytes

	if(sum&&BRRTempLoop>=0) ++BRRTempLoop;
}

void SnesGssExporter::InstrumentClear(int ins)
{
	if(insList[ins].source) free(insList[ins].source);

	insList[ins].source=NULL;
	insList[ins].source_length=0;
	insList[ins].source_rate=0;
	insList[ins].source_volume=128;

	insList[ins].env_ar=15;
	insList[ins].env_dr=7;
	insList[ins].env_sl=7;
	insList[ins].env_sr=0;

	insList[ins].length=0;

	insList[ins].loop_start=0;
	insList[ins].loop_end=0;
	insList[ins].loop_enable=false;

	insList[ins].wav_loop_start=-1;
	insList[ins].wav_loop_end=-1;

	insList[ins].eq_low=0;
	insList[ins].eq_mid=0;
	insList[ins].eq_high=0;

	insList[ins].resample_type=4;//band
	insList[ins].downsample_factor=0;

	insList[ins].name="none";
}



void SnesGssExporter::ModuleClear(void)
{
	int i;

	for(i=0;i<MAX_SONGS;++i) ResetSongStruct(&songList[i]);

	for(i=0;i<MAX_INSTRUMENTS;++i) InstrumentClear(i);


	for(i=0;i<8;++i) ChannelMute[i]=false;
}



void SnesGssExporter::ResetSongStruct(songStruct *song)
{
	int row,chn;

	for(row=0;row<MAX_ROWS;++row)
	{
		for(chn=0;chn<8;++chn)
		{
			song->row[row].chn[chn].value =255;
			song->row[row].chn[chn].volume=255;
		}

		song->row[row].speed = 0;
		song->row[row].marker = 0;
		song->row[row].name="";
	}

	song->measure=4;
	song->length=MAX_ROWS-1;
	song->effect = 0;
	song->compiled_size = 0;
	song->name="untitled";
}



int SnesGssExporter::SongCalculateDuration(int song)
{
	int row,n,length,speed,duration;

	length=SongFindLastRow(&songList[song]);

	duration=0;
	speed=DEFAULT_SPEED;

	for(row=0;row<length;++row)
	{
		if(songList[song].row[row].speed) speed=songList[song].row[row].speed;

		duration+=speed;
	}

	if(!duration) return 0;

	if(duration<UPDATE_RATE_HZ) return 1;

	return duration/UPDATE_RATE_HZ;
}



char *gss_text;
int gss_ptr;
int gss_size;



void gss_init(char *text,int size)
{
	gss_text=text;
	gss_size=size;
	gss_ptr=0;
}



int gss_find_tag(std::string tag)
{
	char* text;
	int len,size;

	text=gss_text;
	size=gss_size;

	len=strlen(tag.c_str());

	while(size)
	{
		if(!memcmp(&text[gss_ptr],tag.c_str(),len)) return gss_ptr+len;

		++gss_ptr;

		if(gss_ptr>=gss_size) gss_ptr=0;

		--size;
	}

	return -1;
}



std::string gss_load_str(std::string tag)
{
	static char str[1024];
	char c,*text;
	int ptr,len,size;

	text=gss_text;
	size=gss_size;

	len=strlen(tag.c_str());

	while(size)
	{
		if(!memcmp(&text[gss_ptr],tag.c_str(),len))
		{
			text=&text[gss_ptr+len];

			ptr=0;

			while(size)
			{
				c=*text++;

				if(c<32) break;

				str[ptr++]=c;

				--size;
			}

			str[ptr]=0;

			return str;
		}

		++gss_ptr;

		if(gss_ptr>=gss_size) gss_ptr=0;

		--size;
	}

	return "";
}



int gss_load_int(std::string tag)
{
	char c,*text;
	int len,n,size,sign;

	text=gss_text;
	size=gss_size;

	len=strlen(tag.c_str());

	while(size)
	{
		if(!memcmp(&text[gss_ptr],tag.c_str(),len))
		{
			text=&text[gss_ptr+len];

			n=0;
			sign=1;

			while(size)
			{
				c=*text++;

				if(c=='-')
				{
					sign=-1;
					--size;
					continue;
				}

				if(c<'0'||c>'9') break;

				n=n*10+(c-'0');

				--size;
			}

			return n*sign;
		}

		++gss_ptr;

		if(gss_ptr>=gss_size) gss_ptr=0;

		--size;
	}

	return 0;
}



int gss_hex_to_byte(char n)
{
	if(n>='0'&&n<='9') return n-'0';
	if(n>='a'&&n<='f') return n-'a'+10;
	if(n>='A'&&n<='F') return n-'A'+10;

	return -1;
}



short* gss_load_short_data(std::string tag)
{
	unsigned short *data,n;
	char *text,c;
	int len,alloc_size,ptr,size;

	text=gss_text;
	size=gss_size;

	alloc_size=65536;

	data=(unsigned short*)malloc(alloc_size);

	ptr=0;

	len=strlen(tag.c_str());

	while(size)
	{
		if(!memcmp(&text[gss_ptr],tag.c_str(),len))
		{
			text=&text[gss_ptr+len];

			while(size)
			{
				if(*text<=32) break;

				n =gss_hex_to_byte(*text++)<<12;
				n|=gss_hex_to_byte(*text++)<<8;
				n|=gss_hex_to_byte(*text++)<<4;
				n|=gss_hex_to_byte(*text++);

				size-=4;

				data[ptr++]=n;

				if(ptr*2>=alloc_size-4)
				{
					alloc_size+=65536;

					data=(unsigned short*)realloc(data,alloc_size);
				}
			}

			data=(unsigned short*)realloc(data,ptr*2);

			return (short*)data;
		}

		++gss_ptr;

		if(gss_ptr>=gss_size) gss_ptr=0;

		--size;
	}

	free(data);

	return NULL;
}



int gss_parse_num_len(char* text,int len,int def)
{
	int c,num;

	num=0;

	while(len)
	{
		c=*text++;

		if(c=='.') return def;

		if(!(c>='0'&&c<='9')) return -1;

		num=num*10+(c-'0');

		--len;
	}

	return num;
}



void SnesGssExporter::InstrumentDataParse(int id,int ins)
{
	std::string insname;

	std::stringstream insnamestream;
	insnamestream << "Instrument" << id;
	insname = insnamestream.str();

	insList[ins].name=gss_load_str(insname+"Name=");

	insList[ins].env_ar=gss_load_int(insname+"EnvAR=");
	insList[ins].env_dr=gss_load_int(insname+"EnvDR=");
	insList[ins].env_sl=gss_load_int(insname+"EnvSL=");
	insList[ins].env_sr=gss_load_int(insname+"EnvSR=");

	insList[ins].length=gss_load_int(insname+"Length=");

	insList[ins].loop_start =gss_load_int(insname+"LoopStart=");
	insList[ins].loop_end   =gss_load_int(insname+"LoopEnd=");
	insList[ins].loop_enable=gss_load_int(insname+"LoopEnable=")?true:false;

	insList[ins].source_length=gss_load_int(insname+"SourceLength=");
	insList[ins].source_rate  =gss_load_int(insname+"SourceRate=");
	insList[ins].source_volume=gss_load_int(insname+"SourceVolume=");

	insList[ins].wav_loop_start=gss_load_int(insname+"WavLoopStart=");
	insList[ins].wav_loop_end  =gss_load_int(insname+"WavLoopEnd=");

	insList[ins].eq_low           =gss_load_int(insname+"EQLow=");
	insList[ins].eq_mid           =gss_load_int(insname+"EQMid=");
	insList[ins].eq_high          =gss_load_int(insname+"EQHigh=");
	insList[ins].resample_type    =gss_load_int(insname+"ResampleType=");
	insList[ins].downsample_factor=gss_load_int(insname+"DownsampleFactor=");
	insList[ins].ramp_enable      =gss_load_int(insname+"RampEnable=")?true:false;
	insList[ins].loop_unroll      =gss_load_int(insname+"LoopUnroll=")?true:false;

	insList[ins].source=gss_load_short_data(insname+"SourceData=");
}



bool SnesGssExporter::ModuleOpenFile(std::string filename)
{
	FILE *file;
	char *text;
	noteFieldStruct *n;
	int ins,song,row,chn,ptr,size,note;
	std::string songname,name;
	bool ex_vol;

	file=fopen(filename.c_str(),"rb");

	if(!file) return false;

	fseek(file,0,SEEK_END);
	size=ftell(file);
	fseek(file,0,SEEK_SET);

	text=(char*)malloc(size+1);

	fread(text,size,1,file);
	fclose(file);

	gss_init(text,size);

	//check signature

	if(gss_find_tag(PROJECT_SIGNATURE)<0)
	{
		free(text);
		return false;
	}

	if(gss_find_tag(EX_VOL_SIGNATURE)<0) ex_vol=false; else ex_vol=true;

	ModuleClear();

	//parse instruments

	for(ins=0;ins<MAX_INSTRUMENTS;++ins) InstrumentDataParse(ins,ins);

	//parse song settings

	for(song=0;song<MAX_SONGS;++song)
	{
		std::stringstream songnamestream;
		songnamestream << "Song" << song;
		songname = songnamestream.str();

		songList[song].name      =gss_load_str(songname+"Name=");
		songList[song].length    =gss_load_int(songname+"Length=");
		songList[song].loop_start=gss_load_int(songname+"LoopStart=");
		songList[song].measure   =gss_load_int(songname+"Measure=");
		songList[song].effect    =gss_load_int(songname+"Effect=")?true:false;
	}

	//parse song text

	for(song=0;song<MAX_SONGS;++song)
	{
		std::stringstream songtag;
		songtag << "[Song" << song << "]";

		ptr=gss_find_tag(songtag.str());

		if(ptr<0) continue;

		while(ptr<size) if(text[ptr++]=='\n') break;

		while(1)
		{
			row=gss_parse_num_len(text+ptr,4,0);

			if(row<0) break;

			songList[song].row[row].marker=(text[ptr+4]!=' '?true:false);

			songList[song].row[row].speed=gss_parse_num_len(text+ptr+5,2,0);

			ptr+=7;

			for(chn=0;chn<8;++chn)
			{
				n=&songList[song].row[row].chn[chn];

				if(text[ptr]=='.')
				{
					note=0;
				}
				else
				{
					if(text[ptr]=='-')
					{
						n->note=1;
					}
					else
					{
						switch(text[ptr])
						{
						case 'C': note=0; break;
						case 'D': note=2; break;
						case 'E': note=4; break;
						case 'F': note=5; break;
						case 'G': note=7; break;
						case 'A': note=9; break;
						case 'B': note=11; break;
						default:  note=-1;
						}

						if(text[ptr+1]=='#') ++note;

						n->note=note+(text[ptr+2]-'0')*12+2;
					}
				}

				ptr+=3;

				n->instrument=gss_parse_num_len(text+ptr,2,0);

				ptr+=2;

				if(ex_vol)
				{
					n->volume=gss_parse_num_len(text+ptr,2,255);

					ptr+=2;
				}

				if(text[ptr]=='.') n->effect=0; else n->effect=text[ptr];

				ptr+=1;

				n->value=gss_parse_num_len(text+ptr,2,255);

				if(!ex_vol)
				{
					if(n->effect=='V')//convert old volume command into volume column value
					{
						n->volume=n->value;
						n->effect=0;
						n->value=255;
					}

					if(n->effect=='M') n->effect='V';//modulation has been renamed into vibrato
				}

				ptr+=2;
			}

			name="";

			while(ptr<size)
			{
				if(text[ptr]<' ') break;

				name+=text[ptr];

				++ptr;
			}

			songList[song].row[row].name=name;

			while(ptr<size)
			{
				if(text[ptr]>=' ') break;

				++ptr;
			}
		}
	}

	free(text);

	UpdateSampleData=true;

	return true;
}



bool SnesGssExporter::ModuleOpen(std::string filename)
{
	if(ModuleOpenFile(filename))
	{
		SPCCompile(&songList[0],0,false,false,-1);
		CompileAllSongs();
		return true;
	}
	else
	{
		std::cerr << "Can't open module";
		return false;
	}
}



void SnesGssExporter::InstrumentDataWrite(FILE *file,int id,int ins)
{
	int i;

	fprintf(file,"Instrument%iName=%s\n",id,insList[ins].name.c_str());

	fprintf(file,"Instrument%iEnvAR=%i\n",id,insList[ins].env_ar);
	fprintf(file,"Instrument%iEnvDR=%i\n",id,insList[ins].env_dr);
	fprintf(file,"Instrument%iEnvSL=%i\n",id,insList[ins].env_sl);
	fprintf(file,"Instrument%iEnvSR=%i\n",id,insList[ins].env_sr);

	fprintf(file,"Instrument%iLength=%i\n",id,insList[ins].length);

	fprintf(file,"Instrument%iLoopStart=%i\n" ,id,insList[ins].loop_start);
	fprintf(file,"Instrument%iLoopEnd=%i\n"   ,id,insList[ins].loop_end);
	fprintf(file,"Instrument%iLoopEnable=%i\n",id,insList[ins].loop_enable?1:0);

	fprintf(file,"Instrument%iSourceLength=%i\n",id,insList[ins].source_length);
	fprintf(file,"Instrument%iSourceRate=%i\n"  ,id,insList[ins].source_rate);
	fprintf(file,"Instrument%iSourceVolume=%i\n",id,insList[ins].source_volume);

	fprintf(file,"Instrument%iWavLoopStart=%i\n",id,insList[ins].wav_loop_start);
	fprintf(file,"Instrument%iWavLoopEnd=%i\n"  ,id,insList[ins].wav_loop_end);

	fprintf(file,"Instrument%iEQLow=%i\n"           ,id,insList[ins].eq_low);
	fprintf(file,"Instrument%iEQMid=%i\n"           ,id,insList[ins].eq_mid);
	fprintf(file,"Instrument%iEQHigh=%i\n"          ,id,insList[ins].eq_high);
	fprintf(file,"Instrument%iResampleType=%i\n"    ,id,insList[ins].resample_type);
	fprintf(file,"Instrument%iDownsampleFactor=%i\n",id,insList[ins].downsample_factor);
	fprintf(file,"Instrument%iRampEnable=%i\n"      ,id,insList[ins].ramp_enable?1:0);
	fprintf(file,"Instrument%iLoopUnroll=%i\n"      ,id,insList[ins].loop_unroll?1:0);

	fprintf(file,"Instrument%iSourceData=",id);

	if(insList[ins].source)
	{
		for(i=0;i<insList[ins].source_length;++i) fprintf(file,"%4.4x",(unsigned short)insList[ins].source[i]);
	}


	fprintf(file,"\n\n");
}



bool SnesGssExporter::SongIsRowEmpty(songStruct *s,int row,bool marker)
{
	noteFieldStruct *n;
	int sum,chn;

	sum=s->row[row].speed;

	if(marker&&s->row[row].marker) ++sum;

	for(chn=0;chn<8;++chn)
	{
		n=&s->row[row].chn[chn];

		sum+=n->note;
		sum+=n->instrument;
		sum+=(n->volume==255?0:1);
		sum+=n->effect;
		sum+=(n->value==255?0:1);
	}

	return sum?false:true;
}



bool SnesGssExporter::SongIsEmpty(songStruct *s)
{
	int row;

	for(row=0;row<s->length;++row) if(!SongIsRowEmpty(s,row,true)) return false;

	return true;
}



bool SnesGssExporter::ModuleSave(std::string filename)
{
	FILE *file;
	noteFieldStruct *n;
	int ins,song,row,chn;

	file=fopen(filename.c_str(),"wt");

	if(!file) return false;

	fprintf(file,"%s\n\n",PROJECT_SIGNATURE);
	fprintf(file,"%s\n\n",EX_VOL_SIGNATURE);

	for(ins=0;ins<MAX_INSTRUMENTS;++ins) InstrumentDataWrite(file,ins,ins);

	for(song=0;song<MAX_SONGS;++song)
	{
		fprintf(file,"Song%iName=%s\n"     ,song,songList[song].name.c_str());
		fprintf(file,"Song%iLength=%i\n"   ,song,songList[song].length);
		fprintf(file,"Song%iLoopStart=%i\n",song,songList[song].loop_start);
		fprintf(file,"Song%iMeasure=%i\n\n",song,songList[song].measure);
		fprintf(file,"Song%iEffect=%i\n\n" ,song,songList[song].effect?1:0);
	}

	for(song=0;song<MAX_SONGS;++song)
	{
		fprintf(file,"[Song%i]\n",song);

		for(row=0;row<MAX_ROWS;++row)
		{
			if(SongIsRowEmpty(&songList[song],row,true)) continue;

			fprintf(file,"%4.4i%c",row,songList[song].row[row].marker?'*':' ');

			if(songList[song].row[row].speed) fprintf(file,"%2.2i",songList[song].row[row].speed); else fprintf(file,"..");

			for(chn=0;chn<8;++chn)
			{
				n=&songList[song].row[row].chn[chn];

				switch(n->note)
				{
				case 0: fprintf(file,"..."); break;
				case 1: fprintf(file,"---"); break;
				default: fprintf(file,"%s%i",NoteNames[(n->note-2)%12].c_str(),(n->note-2)/12);
				}

				if(n->instrument) fprintf(file,"%2.2i",n->instrument); else fprintf(file,"..");

				if(n->volume==255) fprintf(file,".."); else fprintf(file,"%2.2i",n->volume);

				fprintf(file,"%c",n->effect?n->effect:'.');

				if(n->value==255) fprintf(file,".."); else fprintf(file,"%2.2i",n->value);
			}

			fprintf(file,"%s\n",songList[song].row[row].name.c_str());
		}

		fprintf(file,"\n");
	}

	fclose(file);

	return true;
}


int SnesGssExporter::InsCalculateBRRSize(int ins,bool loop_only)
{
	int i,brr_len,brr_loop_len,div,loop_size;
	int loop_cnt,new_loop_start;
	int loop_start,loop_end;

	if(!insList[ins].source) return 0;

	switch(insList[ins].downsample_factor)
	{
	case 1:  div=2; break;
	case 2:  div=4; break;
	default: div=1;
	}

	if(!insList[ins].loop_enable)
	{
		brr_len=(insList[ins].length/div+15)/16*9;

		brr_loop_len=0;
	}
	else
	{
		loop_start= insList[ins].loop_start /div;
		loop_end  =(insList[ins].loop_end+1)/div;

		loop_size=loop_end-loop_start;

		if(!insList[ins].loop_unroll)
		{
			brr_loop_len=loop_size/16*9;

			brr_len=(loop_start+15)/16*9+brr_loop_len;

			if((loop_size&15)>=8) brr_len+=9;
		}
		else
		{
			new_loop_start=(loop_start+15)/16*16;

			loop_cnt=new_loop_start;

			while(1)
			{
				loop_cnt+=loop_size;

				if(!(loop_cnt&15)||loop_cnt>=65536) break;
			}

			brr_loop_len=(loop_cnt-new_loop_start)/16*9;

			brr_len=loop_cnt/16*9;
		}
	}

	for(i=0;i<16;++i)
	{
		if(insList[ins].source[i]!=0)
		{
			brr_len+=9;
			break;
		}
	}

	return loop_only?brr_loop_len:brr_len;
}



void SnesGssExporter::SwapInstrumentNumberAll(int ins1,int ins2)
{
	noteFieldStruct *n;
	int song,row,chn;

	for(song=0;song<MAX_SONGS;++song)
	{
		for(row=0;row<MAX_ROWS;++row)
		{
			for(chn=0;chn<8;++chn)
			{
				n=&songList[song].row[row].chn[chn];

				if(n->instrument==ins1) n->instrument=ins2; else if(n->instrument==ins2) n->instrument=ins1;
			}
		}
	}
}


int SnesGssExporter::SamplesCompile(unsigned int adr,int one_sample)
{
	int i,ins,size,dir_ptr,adsr_ptr,sample_adr,sample_loop,ins_count,prev,match;

	InstrumentsCount=0;

	if(one_sample<0)
	{
		for(ins=0;ins<MAX_INSTRUMENTS;++ins)
		{
			if(insList[ins].source&&insList[ins].length>=16)
			{
				InstrumentRenumberList[ins]=InstrumentsCount;//new instrument number

				++InstrumentsCount;
			}
			else
			{
				InstrumentRenumberList[ins]=-1;//no sample in this slot
			}
		}
	}
	else
	{
		for(ins=0;ins<MAX_INSTRUMENTS;++ins) InstrumentRenumberList[ins]=-1;

		InstrumentRenumberList[one_sample]=0;//new instrument number

		InstrumentsCount=1;
	}

	dir_ptr=adr;
	adr+=4*InstrumentsCount;//dir size

	adsr_ptr=adr;
	adr+=2*InstrumentsCount;//adsr list size

	size=adr-dir_ptr;

	for(ins=0;ins<MAX_INSTRUMENTS;++ins)
	{
		if(InstrumentRenumberList[ins]<0) continue;

		BRREncode(ins);

		sample_adr=adr;

		match=-1;

		if(one_sample<0)
		{
			for(prev=0;prev<ins;++prev)
			{
				if(insList[prev].BRR_size==BRRTempSize)
				{
					if(!memcmp(&SPCMem[insList[prev].BRR_adr],BRRTemp,BRRTempSize))
					{
						match=prev;
						break;
					}
				}
			}
		}

		if(match<0)//new sample
		{
			insList[ins].BRR_adr=sample_adr;
			insList[ins].BRR_size=BRRTempSize;

			for(i=0;i<BRRTempSize;++i)
			{
				SPCMem[adr]=BRRTemp[i];

				++adr;

				if(adr>=sizeof(SPCMem)) return -1;
			}

			if(BRRTempLoop>0) sample_loop=sample_adr+BRRTempLoop*9; else sample_loop=sample_adr;

			SPCMem[dir_ptr+0]=sample_adr&255;
			SPCMem[dir_ptr+1]=sample_adr>>8;
			SPCMem[dir_ptr+2]=sample_loop&255;
			SPCMem[dir_ptr+3]=sample_loop>>8;

			size+=BRRTempSize;
		}
		else//duplicated sample, just copy the parameters
		{
			if(BRRTempLoop>0) sample_loop=insList[match].BRR_adr+BRRTempLoop*9; else sample_loop=insList[match].BRR_adr;

			SPCMem[dir_ptr+0]=insList[match].BRR_adr&255;
			SPCMem[dir_ptr+1]=insList[match].BRR_adr>>8;
			SPCMem[dir_ptr+2]=sample_loop&255;
			SPCMem[dir_ptr+3]=sample_loop>>8;
		}

		dir_ptr+=4;

		SPCMem[adsr_ptr+0]=0x80|insList[ins].env_ar|(insList[ins].env_dr<<4);
		SPCMem[adsr_ptr+1]=insList[ins].env_sr|(insList[ins].env_sl<<5);

		adsr_ptr+=2;
	}

	return size;
}




bool SnesGssExporter::SongIsChannelEmpty(songStruct *s,int chn)
{
	int row;

	for(row=0;row<s->length;++row) if(s->row[row].chn[chn].note) return false;

	return true;
}



int SnesGssExporter::SongFindLastRow(songStruct *s)
{
	int row;

	for(row=MAX_ROWS-1;row>=0;--row)
	{
		if(!SongIsRowEmpty(s,row,false)) return row;
	}

	return 0;
}



void SnesGssExporter::SongCleanUp(songStruct *s)
{
	noteFieldStruct *n;
	int row,chn,song_length,prev_ins,prev_vol;

	song_length=SongFindLastRow(s);

	for(chn=0;chn<8;++chn)
	{
		prev_ins=-1;
		prev_vol=-1;

		for(row=0;row<song_length;++row)
		{
			n=&s->row[row].chn[chn];

			if(n->instrument)
			{
				if(n->instrument!=prev_ins) prev_ins=n->instrument; else n->instrument=0;
			}

			if(n->volume!=prev_vol)
			{
				if(n->volume!=255) prev_vol=n->volume;
			}
			else
			{
				n->volume=255;
			}

			if(row==s->loop_start&&s->loop_start>0)//set instrument and volume at the loop point, just in case
			{
				if(!n->instrument&&prev_ins>0) n->instrument=prev_ins;

				if(prev_vol>=0) n->volume=prev_vol;
			}
		}
	}
}



int SnesGssExporter::DelayCompile(int adr,int delay)
{
	const int max_short_wait=148;//max duration of the one-byte delay

	while(delay)
	{
		if(delay<max_short_wait*2)//can be encoded as one or two one-byte delays
		{
			if(delay>=max_short_wait)
			{
				SPCChnMem[adr++]=max_short_wait;

				delay-=max_short_wait;
			}
			else
			{
				SPCChnMem[adr++]=delay;

				delay=0;
			}
		}
		else//long delay, encoded with 3 bytes
		{
			if(delay>=65535)
			{
				SPCChnMem[adr++]=246;
				SPCChnMem[adr++]=255;
				SPCChnMem[adr++]=255;

				delay-=65535;
			}
			else
			{
				SPCChnMem[adr++]=246;
				SPCChnMem[adr++]=delay&255;
				SPCChnMem[adr++]=delay>>8;

				delay=0;
			}
		}
	}

	return adr;
}



int SnesGssExporter::ChannelCompile(songStruct *s,int chn,int start_row,int compile_adr,int &play_adr)
{
	const int keyoff_gap_duration=2;//number of frames between keyoff and keyon, to prevent clicks
	noteFieldStruct *n;
	int row,adr,ins,speed,speed_acc,loop_adr,ins_change,effect,value,loop_start,length,size,ins_last;
	int new_play_adr,ref_len,best_ref_len,new_size,min_size,effect_volume;
	bool change,insert_gap,key_is_on,porta_active,new_note_keyoff;

	play_adr=0;
	loop_adr=0;
	adr     =0;

	speed=20;
	speed_acc=0;
	ins_change=0;
	ins_last=1;
	insert_gap=false;
	effect=0;
	effect_volume=255;
	value=0;
	key_is_on=false;
	porta_active=false;

	length    =s->length;
	loop_start=s->loop_start;

	if(!loop_start&&length==MAX_ROWS-1)//if loop markers are in the default positions, loop the song on the next to the last non-empty row, making it seem to be not looped
	{
		length=SongFindLastRow(s)+2;
		loop_start=length-1;
	}

	for(row=0;row<length;++row)
	{
		change=false;

		if(row==loop_start||row==start_row)//break the ongoing delays at the start and loop points
		{
			adr=DelayCompile(adr,speed_acc);

			speed_acc=0;
		}

		if(row==start_row)
		{
			play_adr=adr;

			if(row)//force instrument change at starting row in the middle of the song
			{
				ins_change=ins_last;
				change=true;
			}
		}

		if(row==loop_start) loop_adr=adr;

		if(s->row[row].speed) speed=s->row[row].speed;

		n=&s->row[row].chn[chn];

		if(n->instrument)
		{
			ins_change=n->instrument;

			ins_last=ins_change;
		}

		if(n->effect&&n->value!=255)
		{
			effect=n->effect;
			value =n->value;

			change=true;
		}

		if(n->volume!=255)
		{
			effect_volume=n->volume;

			change=true;
		}

		if(n->note) change=true;

		if(change)
		{
			if(effect=='P') porta_active=(value?true:false);//detect portamento early to prevent inserting the keyoff gap

			if(n->note>1&&key_is_on&&!porta_active&&speed_acc>=keyoff_gap_duration)//if it is a note (not keyoff) and there is enough empty space, insert keyoff with a gap before keyon
			{
				speed_acc-=keyoff_gap_duration;

				insert_gap=true;
			}
			else
			{
				insert_gap=false;
			}

			adr=DelayCompile(adr,speed_acc);

			speed_acc=0;

			if(n->note==1)
			{
				SPCChnMem[adr++]=245;//keyoff

				key_is_on=false;
			}

			if(effect=='T')//detune
			{
				SPCChnMem[adr++]=249;
				SPCChnMem[adr++]=value;

				effect=0;
			}

			if(effect=='D')//slide down
			{
				SPCChnMem[adr++]=250;
				SPCChnMem[adr++]=-value;

				effect=0;
			}

			if(effect=='U')//slide up
			{
				SPCChnMem[adr++]=250;
				SPCChnMem[adr++]=value;

				effect=0;
			}

			if(effect=='P')//portamento
			{
				SPCChnMem[adr++]=251;
				SPCChnMem[adr++]=value;

				effect=0;

				//porta_active=(value?true:false);
			}

			if(effect=='V')//vibrato
			{
				SPCChnMem[adr++]=252;
				SPCChnMem[adr++]=(value/10%10)|((value%10)<<4);//rearrange decimal digits into hex nibbles, in reversed order

				effect=0;
			}

			if(effect=='S')//pan
			{
				SPCChnMem[adr++]=248;
				SPCChnMem[adr++]=(value*256/100);//recalculate 0..50..99 to 0..128..255

				effect=0;
			}

			new_note_keyoff=(key_is_on&&insert_gap&&!porta_active)?true:false;

			if((n->note<2&&!new_note_keyoff)&&effect_volume!=255)//volume, only insert it here when there is no new note with preceding keyoff, otherwise insert it after keyoff
			{
				SPCChnMem[adr++]=247;
				SPCChnMem[adr++]=((float)effect_volume*1.29f);//rescale 0..99 to 0..127

				effect_volume=255;
			}

			if(n->note>1)
			{
				if(new_note_keyoff)
				{
					SPCChnMem[adr++]=245;//keyoff

					adr=DelayCompile(adr,keyoff_gap_duration);

					key_is_on=false;
				}

				if(ins_change)
				{
					SPCChnMem[adr++]=254;

					ins=InstrumentRenumberList[ins_change-1];

					if(ins<0) ins=8; else ins+=9;//first 9 entries of the sample dir reserved for the BRR streamer, sample 8 is the sync same, it is always empty

					SPCChnMem[adr++]=ins;

					ins_change=0;
				}

				if(effect_volume!=255)//volume, inserted after keyoff to prevent clicks
				{
					SPCChnMem[adr++]=247;
					SPCChnMem[adr++]=((float)effect_volume*1.29f);//rescale 0..99 to 0..127

					effect_volume=255;
				}

				SPCChnMem[adr++]=(n->note-2)+150;

				key_is_on=true;
			}
		}

		speed_acc+=speed;
	}

	n=&s->row[loop_start].chn[chn];

	if(n->note>1) speed_acc-=keyoff_gap_duration;//if there is a new note at the loop point, prevent click by inserting a keyoff with gap just at the end of the channel

	adr=DelayCompile(adr,speed_acc);

	if(n->note>1)
	{
		SPCChnMem[adr++]=245;//keyoff

		adr=DelayCompile(adr,keyoff_gap_duration);
	}

	play_adr+=compile_adr;
	loop_adr+=compile_adr;

	SPCChnMem[adr++]=255;
	SPCChnMem[adr++]=loop_adr&255;
	SPCChnMem[adr++]=loop_adr>>8;

	size=adr;



#ifndef ENABLE_SONG_COMPRESSION

	memcpy(&SPCMem[compile_adr],SPCChnMem,size);

#else

	min_size=65536;
	best_ref_len=64;

	for(ref_len=5;ref_len<=64;++ref_len)
	{
		new_play_adr=play_adr;

		new_size=ChannelCompress(compile_adr,new_play_adr,loop_adr,size,ref_len);

		if(new_size<min_size)
		{
			min_size=new_size;
			best_ref_len=ref_len;
		}
	}

	size=ChannelCompress(compile_adr,play_adr,loop_adr,size,best_ref_len);

	memcpy(&SPCMem[compile_adr],SPCChnPack,size);

#endif

	return size;
}



void SnesGssExporter::ChannelCompressFlush(int compile_adr)
{
	int i,ref_len,ref_off;

	ref_len=CompressSeqPtr;
	CompressSeqPtr=0;

	if(!ref_len) return;

	ref_off=-1;

	for(i=0;i<CompressOutPtr-ref_len;++i)
	{
		if(!memcmp(&SPCChnPack[i],CompressSeqBuf,ref_len))
		{
			ref_off=compile_adr+i;
			break;
		}
	}

	if(ref_off<0)
	{
		memcpy(&SPCChnPack[CompressOutPtr],CompressSeqBuf,ref_len);

		CompressOutPtr+=ref_len;
	}
	else
	{
		SPCChnPack[CompressOutPtr++]=253;
		SPCChnPack[CompressOutPtr++]=ref_off&255;
		SPCChnPack[CompressOutPtr++]=ref_off>>8;
		SPCChnPack[CompressOutPtr++]=ref_len;
	}
}



int SnesGssExporter::ChannelCompress(int compile_adr,int &play_adr,int loop_adr,int src_size,int ref_max)
{
	int i,tag,len,src_ptr,new_loop_adr,new_play_adr;

	memset(SPCChnPack,0,sizeof(SPCChnPack));

	CompressSeqPtr=0;
	CompressOutPtr=0;

	src_ptr=0;

	new_loop_adr=-1;
	new_play_adr=-1;

	while(src_ptr<src_size)
	{
		if(new_loop_adr<0)
		{
			if(src_ptr==loop_adr-compile_adr)
			{
				ChannelCompressFlush(compile_adr);

				new_loop_adr=compile_adr+CompressOutPtr;
			}
		}

		if(new_play_adr<0)
		{
			if(src_ptr==play_adr-compile_adr)
			{
				ChannelCompressFlush(compile_adr);

				new_play_adr=compile_adr+CompressOutPtr;
			}
		}

		tag=SPCChnMem[src_ptr];

		switch(tag)
		{
		case 247:
		case 248:
		case 249:
		case 250:
		case 251:
		case 252:
		case 254: len=2; break;
		case 253: len=4; break;
		case 246:
		case 255: len=3; break;
		default:  len=1;
		}

		for(i=0;i<len;++i) CompressSeqBuf[CompressSeqPtr++]=SPCChnMem[src_ptr++];

		if(CompressSeqPtr>=ref_max) ChannelCompressFlush(compile_adr);
	}

	ChannelCompressFlush(compile_adr);

	if(new_play_adr<0) new_play_adr=compile_adr;
	if(new_loop_adr<0) new_loop_adr=compile_adr;

	SPCChnPack[CompressOutPtr-2]=new_loop_adr&255;
	SPCChnPack[CompressOutPtr-1]=new_loop_adr>>8;

	play_adr=new_play_adr;

	return CompressOutPtr;//compressed data size
}



int SnesGssExporter::SongCompile(songStruct *s_original,int start_row,int start_adr,bool mute)
{
	static songStruct s;
	noteFieldStruct *n,*m;
	int row,chn,adr,channels_all,channels_off,chn_size,play_adr,section_start,section_end,repeat_row;
	bool active[8],find_ins,find_vol,repeat;

	s_original->compiled_size=0;

	if(start_row>=s_original->length) return 0;//don't compile and play song if starting position is outside the song

	memcpy(&s,s_original,sizeof(songStruct));

	SongCleanUp(&s);//cleanup instrument numbers and volume effect

	//expand repeating sections

	for(chn=0;chn<8;++chn)
	{
		section_start=0;
		section_end=0;

		repeat=false;
		repeat_row=0;

		for(row=0;row<MAX_ROWS;++row)
		{
			if(s.row[row].marker)
			{
				section_start=section_end;
				section_end=row;
				repeat=false;
			}

			n=&s.row[row].chn[chn];

			if(n->effect=='R')
			{
				repeat=true;
				repeat_row=section_start;
			}

			if(repeat)
			{
				m=&s.row[repeat_row].chn[chn];

				n->note=m->note;
				n->instrument=m->instrument;

				if(m->effect!='R') n->effect=m->effect; else n->effect=0;

				n->value=m->value;

				++repeat_row;

				if(repeat_row>=section_end) repeat_row=section_start;
			}
		}
	}

	//modify current row if the song needs to be played from the middle

	if(start_row>0)
	{
		for(chn=0;chn<8;++chn)
		{
			n=&s.row[start_row].chn[chn];

			find_ins=true;
			find_vol=true;

			for(row=start_row-1;row>=0;--row)
			{
				if(!find_ins&&!find_vol) break;

				m=&s.row[row].chn[chn];

				if(find_ins&&m->instrument)
				{
					if(!n->instrument) n->instrument=m->instrument;

					find_ins=false;
				}

				if(find_vol&&m->volume!=255)
				{
					if(n->volume==255) n->volume=m->volume;

					find_vol=false;
				}
			}
		}
	}

	//count active channels

	channels_all=0;

	for(chn=0;chn<8;++chn)
	{
		if(SongIsChannelEmpty(&s,chn)||(ChannelMute[chn]&&mute))
		{
			active[chn]=false;
		}
		else
		{
			active[chn]=true;

			++channels_all;
		}
	}

	//set default instrument number

	for(chn=0;chn<8;++chn)
	{
		for(row=0;row<MAX_ROWS;++row)
		{
			n=&s.row[row].chn[chn];

			if(n->note>1)
			{
				if(!n->instrument) n->instrument=1;

				break;
			}
		}
	}

	//compile channels

	adr=start_adr+1+channels_all*2;

	channels_off=start_adr+1;

	for(chn=0;chn<8;++chn)
	{
		if(!active[chn]) continue;

		chn_size=ChannelCompile(&s,chn,start_row,adr,play_adr);//play_adr gets changed according to the start_row

		SPCMem[channels_off+0]=play_adr&255;
		SPCMem[channels_off+1]=play_adr>>8;

		channels_off+=2;

		adr+=chn_size;
	}

	SPCMem[start_adr]=channels_all;

	s_original->compiled_size=adr-start_adr;

	return s_original->compiled_size;
}



int SnesGssExporter::EffectsCompile(int start_adr)
{
	int adr,song,size,effects_all,effects_off;

	effects_all=0;

	for(song=0;song<MAX_SONGS;++song) if(songList[song].effect) ++effects_all;

	adr=start_adr+1+effects_all*2;

	SPCMem[start_adr]=effects_all;

	effects_off=start_adr+1;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(!songList[song].effect) continue;

		if(SongIsEmpty(&songList[song])) continue;

		SPCMem[effects_off+0]=adr&255;
		SPCMem[effects_off+1]=adr>>8;

		effects_off+=2;

		size=SongCompile(&songList[song],0,adr,false);

		if(size<0) return -1;

		adr+=size;
	}

	return adr-start_adr;
}



bool SnesGssExporter::SPCCompile(songStruct *s,int start_row,bool mute,bool effects,int one_sample)
{
	const int header_size=2;
	int ptr,code_adr,sample_adr,adsr_adr,music_adr,effects_adr;

	SPCMusicSize=0;
	SPCEffectsSize=0;

	//compile SPC700 RAM snapshot

	code_adr=0x200;//start address for the driver in the SPC700 RAM

	memcpy(&SPCMem[code_adr],spc700_data+header_size,spc700_size-header_size);//SPC700 driver code, header_size is for the extra header used by sneslib

	sample_adr=code_adr+spc700_size-header_size;

	if(UpdateSampleData||one_sample>=0)//only recompile samples when needed
	{
		memset(&SPCMem[sample_adr],0,65536-sample_adr);

		if(one_sample<0) UpdateSampleData=false; else UpdateSampleData=true;

		SPCInstrumentsSize=0;

		SPCInstrumentsSize=SamplesCompile(sample_adr,one_sample);//puts the dir, adsr list, and sample data directly into the RAM snapshot

		if(SPCInstrumentsSize<0)
		{
			std::cerr << "SPC700 memory overflow, too much sample data";
			return false;
		}
	}

	adsr_adr=sample_adr+4*InstrumentsCount;

	effects_adr=sample_adr+SPCInstrumentsSize;

	if(effects)
	{
		SPCEffectsSize=EffectsCompile(effects_adr);

		if(SPCEffectsSize<0)
		{
			std::cerr << "SPC700 memory overflow, too much sound effects data";
			return false;
		}
	}

	music_adr=effects_adr+SPCEffectsSize;

	SPCMusicSize=SongCompile(s,start_row,music_adr,mute);

	if(SPCMusicSize<0)
	{
		std::cerr << "SPC700 memory overflow, too much music data";
		return false;
	}

	if(!effects)//if the effects aren't compiled, it is not for export
	{
		SPCMem[0x204]=0;//skip the bra mainLoopInit, to run preinitialized version of the driver
		SPCMem[0x205]=0;
	}

	SPCMem[0x208]=(adsr_adr-9*2)&255;
	SPCMem[0x209]=(adsr_adr-9*2)>>8;

	SPCMem[0x20a]=effects_adr&255;
	SPCMem[0x20b]=effects_adr>>8;

	SPCMem[0x20c]=music_adr&255;
	SPCMem[0x20d]=music_adr>>8;

	//compile SPC file

	memset(SPCTemp,0,sizeof(SPCTemp));

	memcpy(SPCTemp,"SNES-SPC700 Sound File Data v0.30",32);//header

	SPCTemp[0x21]=26;
	SPCTemp[0x22]=26;
	SPCTemp[0x23]=27;//no ID tag
	SPCTemp[0x24]=30;//version minor

	SPCTemp[0x25]=code_adr&255;//PC=driver start address
	SPCTemp[0x26]=code_adr>>8;
	SPCTemp[0x27]=0;//A
	SPCTemp[0x28]=0;//X
	SPCTemp[0x29]=0;//Y
	SPCTemp[0x2a]=0;//PSW
	SPCTemp[0x2b]=0;//SP (LSB)

	memcpy(&SPCTemp[0x100],SPCMem,sizeof(SPCMem));

	//10100h   128 DSP Registers
	//10180h    64 unused
	//101C0h    64 Extra RAM (Memory region used when the IPL ROM region is setto read-only)

	SPCMemTopAddr=music_adr;

	return true;
}


char*  SnesGssExporter::MakeNameForAlias(std::string name)
{
	static char alias[1024];
	int i,c;

	strcpy(alias,name.c_str());

	for(i=0;i<(int)strlen(alias);++i)
	{
		c=alias[i];

		if(c>='a'&&c<='z') c-=32;
		if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9'))) c='_';

		alias[i]=c;
	}

	return alias;
}



bool SnesGssExporter::ExportAll(std::string dir)
{
	unsigned char header[2];
	FILE *file;
	int song,size,spc700_size,song_id,sounds_all,songs_all;

	//compile driver code, samples and sound effects into single binary

	ResetSongStruct(&tempSong);

	SPCCompile(&tempSong,0,false,true,-1);

	file=fopen((dir+"spc700.bin").c_str(),"wb");

	spc700_size=SPCMemTopAddr-0x200;

	if(file)
	{
		header[0]=spc700_size&255;//header
		header[1]=spc700_size>>8;

		fwrite(header,sizeof(header),1,file);
		fwrite(&SPCMem[0x200],spc700_size,1,file);

		fclose(file);
	}

	//compile each song into a separate binary

	song_id=1;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(songList[song].effect) continue;

		if(SongIsEmpty(&songList[song])) continue;

		size=SongCompile(&songList[song],0,SPCMemTopAddr,false);

		if(size<0) return false;

		std::stringstream filename;
		filename << dir << "music_" << song_id << ".bin";

		file=fopen(filename.str().c_str(),"wb");

		if(file)
		{
			header[0]=size&255;//header
			header[1]=size>>8;

			fwrite(header,sizeof(header),1,file);
			fwrite(&SPCMem[SPCMemTopAddr],size,1,file);

			fclose(file);
		}

		++song_id;
	}

	//generate resource inclusion file

	file=fopen((dir+"sounds.asm").c_str(),"wt");

	if(!file) return false;

	fprintf(file,";this file generated with SNES GSS tool\n\n");

	song_id=0;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(SongIsEmpty(&songList[song])) continue;

		if(!songList[song].effect) continue;

		fprintf(file,".define SFX_%s\t%i\n",MakeNameForAlias(songList[song].name),song_id);

		++song_id;
	}

	fprintf(file,"\n");

	song_id=0;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(SongIsEmpty(&songList[song])) continue;

		if(songList[song].effect) continue;

		fprintf(file,".define MUS_%s\t%i\n",MakeNameForAlias(songList[song].name),song_id);

		++song_id;
	}

	fprintf(file,"\n");

	fprintf(file,".section \".roDataSoundCode1\" superfree\n");

	fprintf(file,"spc700_code_1:\t.incbin \"spc700.bin\" skip 0 read %i\n",spc700_size<32768?spc700_size:32768);

	if(spc700_size<=32768) fprintf(file,"spc700_code_2:\n");

	fprintf(file,".ends\n\n");

	if(spc700_size>32768)
	{
		fprintf(file,".section \".roDataSoundCode2\" superfree\n");

		fprintf(file,"spc700_code_2:\t.incbin \"spc700.bin\" skip 32768\n");

		fprintf(file,".ends\n\n");
	}

	song_id=1;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(songList[song].effect) continue;

		if(SongIsEmpty(&songList[song])) continue;

		fprintf(file,".section \".roDataMusic%i\" superfree\n",song_id);

		fprintf(file,"music_%i_data:\t.incbin \"music_%i.bin\"\n",song_id,song_id);

		fprintf(file,".ends\n\n");

		++song_id;
	}

	fclose(file);

	//generate C header with externs and meta data for music and sound effects

	sounds_all=0;
	songs_all=0;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(SongIsEmpty(&songList[song])) continue;

		if(songList[song].effect) ++sounds_all; else ++songs_all;
	}

	file=fopen((dir+"sounds.h").c_str(),"wt");

	if(!file) return false;

	fprintf(file,"//this file generated with SNES GSS tool\n\n");

	fprintf(file,"#define SOUND_EFFECTS_ALL\t%i\n\n",sounds_all);
	fprintf(file,"#define MUSIC_ALL\t%i\n\n",songs_all);

	if(sounds_all)
	{
		fprintf(file,"//sound effect aliases\n\n");
		fprintf(file,"enum {\n");

		song_id=0;

		for(song=0;song<MAX_SONGS;++song)
		{
			if(SongIsEmpty(&songList[song])) continue;

			if(!songList[song].effect) continue;

			fprintf(file,"\tSFX_%s=%i",MakeNameForAlias(songList[song].name),song_id);

			if(song_id<sounds_all-1) fprintf(file,",\n");

			++song_id;
		}

		fprintf(file,"\n};\n\n");

		fprintf(file,"//sound effect names\n\n");
		fprintf(file,"const char* const soundEffectsNames[SOUND_EFFECTS_ALL]={\n");

		song_id=0;

		for(song=0;song<MAX_SONGS;++song)
		{
			if(SongIsEmpty(&songList[song])) continue;

			if(!songList[song].effect) continue;

			fprintf(file,"\t\"%s\"",songList[song].name.c_str());

			if(song_id<sounds_all-1) fprintf(file,",\t//%i\n",song_id);

			++song_id;
		}

		fprintf(file,"\t//%i\n};\n\n",song_id-1);
	}

	if(songs_all)
	{
		fprintf(file,"//music effect aliases\n\n");
		fprintf(file,"enum {\n");

		song_id=0;

		for(song=0;song<MAX_SONGS;++song)
		{
			if(SongIsEmpty(&songList[song])) continue;

			if(songList[song].effect) continue;

			fprintf(file,"\tMUS_%s=%i",MakeNameForAlias(songList[song].name),song_id);

			if(song_id<songs_all-1) fprintf(file,",\n");

			++song_id;
		}

		fprintf(file,"\n};\n\n");

		fprintf(file,"//music names\n\n");
		fprintf(file,"const char* const musicNames[MUSIC_ALL]={\n");

		song_id=0;

		for(song=0;song<MAX_SONGS;++song)
		{
			if(SongIsEmpty(&songList[song])) continue;

			if(songList[song].effect) continue;

			fprintf(file,"\t\"%s\"",songList[song].name.c_str());

			if(song_id<songs_all-1) fprintf(file,",\t//%i\n",song_id);

			++song_id;
		}

		fprintf(file,"\t//%i\n};\n\n",song_id-1);
	}

	fprintf(file,"extern const unsigned char spc700_code_1[];\n");
	fprintf(file,"extern const unsigned char spc700_code_2[];\n");

	song_id=1;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(SongIsEmpty(&songList[song])) continue;

		if(songList[song].effect) continue;

		fprintf(file,"extern const unsigned char music_%i_data[];\n",song_id);

		++song_id;
	}

	fprintf(file,"\n");

	fprintf(file,"const unsigned char* const musicData[MUSIC_ALL]={\n");

	song_id=1;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(SongIsEmpty(&songList[song])) continue;

		if(songList[song].effect) continue;

		fprintf(file,"\tmusic_%i_data",song_id);

		if(song_id<songs_all) fprintf(file,",\n");

		++song_id;
	}

	fprintf(file,"\n};\n");

	fclose(file);

	return true;
}


void SnesGssExporter::CompileAllSongs(void)
{
	int song;

	SPCMusicLargestSize=0;

	for(song=0;song<MAX_SONGS;++song)
	{
		if(SongIsEmpty(&songList[song])) continue;

		SongCompile(&songList[song],0,SPCMemTopAddr,false);

		if(songList[song].compiled_size>SPCMusicLargestSize) SPCMusicLargestSize=songList[song].compiled_size;
	}
}
//---------------------------------------------------------------------------

void SnesGssExporter::CleanupSongs()
{
	int song;
	for (song = 0; song < MAX_SONGS; song++)
		SongCleanUp(&songList[song]);
}
//---------------------------------------------------------------------------

int SnesGssExporter::CleanupInstruments()
{
	bool use[MAX_INSTRUMENTS];
	int ins,row,chn,song,cnt;

	for(ins=0;ins<MAX_INSTRUMENTS;++ins) use[ins]=false;

	for(song=0;song<MAX_SONGS;++song)
	{
		for(row=0;row<songList[song].length;++row)
		{
			for(chn=0;chn<8;++chn)
			{
				use[songList[song].row[row].chn[chn].instrument]=true;
			}
		}
	}

	cnt=0;

	for(ins=0;ins<MAX_INSTRUMENTS;++ins)
	{
		if(!use[ins])
		{
			if(insList[ins-1].source)
			{
				InstrumentClear(ins-1);

				++cnt;
			}
		}
	}

	UpdateSampleData=true;

	return cnt;
}
//---------------------------------------------------------------------------

void SnesGssExporter::ExportSPC(int songId, std::string filename)
{
	// ::TODO call::

	FILE *file;

	SPCCompile(&songList[songId],0,false,false,-1);

	file=fopen(filename.c_str(),"wb");

	if(!file) return;

	fwrite(SPCTemp,sizeof(SPCTemp),1,file);
	fclose(file);
}
//---------------------------------------------------------------------------

void SnesGssExporter::Export(std::string dirname)
{
	bool r = ExportAll(dirname+PATH_SEPARATOR);
	if (!r)
	{
		std::cerr << "Some export error\n";
		exit(EXIT_FAILURE);
	}
}
//---------------------------------------------------------------------------

void SnesGssExporter::InstrumentExtractSourceWave(int insId, std::string filename)
{
	// ::TODO call::

	FILE *file;
	unsigned char wave[44];
	int size;

	if (insId >= MAX_INSTRUMENTS) return; 

	file=fopen(filename.c_str(),"wb");

	if(!file) return;

	size=44+insList[insId].source_length*2;

	memcpy(wave,"RIFF",4);//riff signature
	wr_dword_lh(&wave[4],size-8);//filesize-8
	memcpy(&wave[8],"WAVEfmt ",8);//format id
	wr_dword_lh(&wave[16],16);//header size
	wr_word_lh(&wave[20],1);//PCM
	wr_word_lh(&wave[22],1);//mono
	wr_dword_lh(&wave[24],insList[insId].source_rate);//samplerate
	wr_dword_lh(&wave[28],insList[insId].source_rate);//byterate (samplerate*channels*bytespersample)
	wr_dword_lh(&wave[32],2);//channels*bytespersample
	wr_dword_lh(&wave[34],16);//bits per sample
	memcpy(&wave[36],"data",4);//data id
	wr_dword_lh(&wave[40],insList[insId].source_length*2);//data size in bytes

	fwrite(wave,sizeof(wave),1,file);
	fwrite(insList[insId].source,insList[insId].source_length*2,1,file);

	fclose(file);
}
//---------------------------------------------------------------------------

void SnesGssExporter::PrintMemoryUse()
{
	// ::TODO call::

	int free,stream;

	stream=28*9*9+64;//64 is IPL size
	free=65536-512-(spc700_size-2)-SPCInstrumentsSize-SPCMusicSize-SPCEffectsSize-stream;

	printf("Direct Page and Stack: 512 bytes\n");
	printf("Driver code: %d bytes\n", spc700_size-2);
	printf("Instruments: %d bytes (Dir:%d, ADSR:%d, Samples:%d)\n", SPCInstrumentsSize, 4 * InstrumentsCount, 2 * InstrumentsCount, SPCInstrumentsSize-6*InstrumentsCount);
	printf("Sound effects: %d bytes\n", SPCEffectsSize);
	printf("Music data: %d bytes (largest)\n", SPCMusicLargestSize);
	printf("BRR streaming buffer: %d bytes\n", stream);
	printf("Free memory: %d bytes\n", free);
}
//---------------------------------------------------------------------------

